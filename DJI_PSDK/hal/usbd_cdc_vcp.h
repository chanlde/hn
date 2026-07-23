#ifndef __USBD_CDC_VCP_H
#define __USBD_CDC_VCP_H

#include <stdint.h>

typedef enum
{
    DJI_RETURN_OK = 0,
    DJI_RETURN_ERROR = -1,
} T_DjiReturnCode;

T_DjiReturnCode USBD_CDC_WriteData(
        const uint8_t *buf,
        uint32_t len,
        uint32_t *realLen);

T_DjiReturnCode USBD_CDC_ReadData(
        uint8_t *buf,
        uint32_t len,
        uint32_t *realLen);

void USBD_CDC_VCP_Init(void);

/* USB回调接口 */
void USBD_CDC_VCP_RxPush(uint8_t *buf, uint32_t len);
void USBD_CDC_VCP_TxCplt(void);

#endif
