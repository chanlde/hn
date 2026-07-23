#include "usbd_cdc_vcp.h"
#include "usbd_cdc_if.h"
#include "cmsis_compiler.h"

#define VCP_RX_BUF_SIZE     4096
#define VCP_TX_BUF_SIZE     4096
#define USB_FS_MAX_PACKET   64

extern USBD_HandleTypeDef hUsbDeviceFS;

/* RX ringbuffer
 * 32字节对齐，满足 STM32H7 DCache 行大小要求 */
static uint8_t rxBuf[VCP_RX_BUF_SIZE] __attribute__((aligned(32)));
static volatile uint32_t rxHead = 0;
static volatile uint32_t rxTail = 0;

/* TX ringbuffer */
static uint8_t txBuf[VCP_TX_BUF_SIZE] __attribute__((aligned(32)));
static volatile uint32_t txHead = 0;
static volatile uint32_t txTail = 0;

static volatile uint8_t  txBusy      = 0;
/* Fix1: 记录本次DMA/USB传输的字节数，传输完成后才推进 txTail */
static volatile uint32_t txPendingLen = 0;

static uint32_t ring_next(uint32_t pos, uint32_t size)
{
    return (pos + 1) % size;
}

void USBD_CDC_VCP_Init(void)
{
    rxHead = rxTail = 0;
    txHead = txTail = 0;
    txBusy      = 0;
    txPendingLen = 0;
}

/* ================= RX ================= */

void USBD_CDC_VCP_RxPush(uint8_t *buf, uint32_t len)
{
    for(uint32_t i = 0; i < len; i++)
    {
        uint32_t next = ring_next(rxHead, VCP_RX_BUF_SIZE);
        if(next == rxTail)
            break;
        rxBuf[rxHead] = buf[i];
        /* Fix4: 确保数据写入在 rxHead 更新前对消费者可见 */
        __DMB();
        rxHead = next;
    }
}

T_DjiReturnCode USBD_CDC_ReadData(
        uint8_t *buf,
        uint32_t len,
        uint32_t *realLen)
{
    uint32_t count = 0;

    while(count < len)
    {
        /* Fix4: 先读 rxHead 快照，再 DMB，再读 rxBuf，保证读顺序正确 */
        uint32_t head = rxHead;
        __DMB();
        if(rxTail == head)
            break;
        buf[count++] = rxBuf[rxTail];
        rxTail = ring_next(rxTail, VCP_RX_BUF_SIZE);
    }

    if(realLen)
        *realLen = count;

    return DJI_RETURN_OK;
}

/* ================= TX ================= */

/* 必须在 IRQ 关闭或 ISR 上下文中调用，防止并发 */
static void vcp_try_tx(void)
{
    if(txBusy)
        return;

    if(txHead == txTail)
        return;

    uint32_t len;
    if(txHead > txTail)
        len = txHead - txTail;
    else
        len = VCP_TX_BUF_SIZE - txTail;

    if(len > USB_FS_MAX_PACKET)
        len = USB_FS_MAX_PACKET;

    /* Fix3: 若 USB 使用 DMA，需在传输前将 DCache 刷回内存，
     *       否则 DMA 可能读到 Cache 中尚未写回的旧值。
     *       CMSIS 内部会自动对齐到 32 字节 Cache 行。 */
    SCB_CleanDCache_by_Addr((uint32_t *)&txBuf[txTail], (int32_t)len);

    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, &txBuf[txTail], len);

    if(USBD_CDC_TransmitPacket(&hUsbDeviceFS) == USBD_OK)
    {
        /* Fix1: 先记录本次传输长度并置 busy；
         *       txTail 必须等到 TxCplt 后才推进，
         *       此时 DMA/USB FIFO 仍在读取该段内存。 */
        txPendingLen = len;
        txBusy = 1;
    }
}

T_DjiReturnCode USBD_CDC_WriteData(
        const uint8_t *buf,
        uint32_t len,
        uint32_t *realLen)
{
    uint32_t count = 0;

    /* Fix2: 禁用 IRQ，防止写环形缓冲区与 TxCplt 中断之间的 TOCTOU 竞态。
     *       保存 PRIMASK 原值以支持嵌套调用场景。 */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    while(count < len)
    {
        uint32_t next = ring_next(txHead, VCP_TX_BUF_SIZE);
        if(next == txTail)
            break;
        txBuf[txHead] = buf[count++];
        txHead = next;
    }

    vcp_try_tx();

    __set_PRIMASK(primask);

    if(realLen)
        *realLen = count;

    return DJI_RETURN_OK;
}

void USBD_CDC_VCP_TxCplt(void)
{
    /* Fix1: 传输真正完成，现在才可以安全地推进 txTail */
    txTail = (txTail + txPendingLen) % VCP_TX_BUF_SIZE;
    txPendingLen = 0;
    txBusy = 0;
    vcp_try_tx();
}
