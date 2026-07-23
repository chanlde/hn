/**
 ******************************************************************************
 * @file    usbd_cdc.c
 * @author  MCD Application Team
 * @brief   This file provides the high layer firmware functions to manage the
 *          following functionalities of the USB CDC Class:
 *           - Initialization and Configuration of high and low layer
 *           - Enumeration as CDC Device (and enumeration for each implemented memory interface)
 *           - OUT/IN data transfer
 *           - Command IN transfer (class requests management)
 *           - Error management
 *
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2015 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 *  @verbatim
 *
 *          ===================================================================
 *                                CDC Class Driver Description
 *          ===================================================================
 *           This driver manages the "Universal Serial Bus Class Definitions for Communications Devices
 *           Revision 1.2 November 16, 2007" and the sub-protocol specification of "Universal Serial Bus
 *           Communications Class Subclass Specification for PSTN Devices Revision 1.2 February 9, 2007"
 *           This driver implements the following aspects of the specification:
 *             - Device descriptor management
 *             - Configuration descriptor management
 *             - Enumeration as CDC device with 2 data endpoints (IN and OUT) and 1 command endpoint (IN)
 *             - Requests management (as described in section 6.2 in specification)
 *             - Abstract Control Model compliant
 *             - Union Functional collection (using 1 IN endpoint for control)
 *             - Data interface class
 *
 *           These aspects may be enriched or modified for a specific user application.
 *
 *            This driver doesn't implement the following aspects of the specification
 *            (but it is possible to manage these features with some modifications on this driver):
 *             - Any class-specific aspect relative to communication classes should be managed by user application.
 *             - All communication classes other than PSTN are not managed
 *
 *  @endverbatim
 *
 ******************************************************************************
 */

/* BSPDependencies
- "stm32xxxxx_{eval}{discovery}{nucleo_144}.c"
- "stm32xxxxx_{eval}{discovery}_io.c"
EndBSPDependencies */

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc.h"
#include "usbd_ctlreq.h"

/** @addtogroup STM32_USB_DEVICE_LIBRARY
 * @{
 */

/** @defgroup USBD_CDC
 * @brief usbd core module
 * @{
 */

/** @defgroup USBD_CDC_Private_TypesDefinitions
 * @{
 */
/**
 * @}
 */

/** @defgroup USBD_CDC_Private_Defines
 * @{
 */
/**
 * @}
 */

/** @defgroup USBD_CDC_Private_Macros
 * @{
 */

/**
 * @}
 */

/** @defgroup USBD_CDC_Private_FunctionPrototypes
 * @{
 */

static uint8_t USBD_CDC_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_CDC_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_CDC_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_CDC_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_CDC_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_CDC_EP0_RxReady(USBD_HandleTypeDef *pdev);
#ifndef USE_USBD_COMPOSITE
static uint8_t *USBD_CDC_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_CDC_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_CDC_GetOtherSpeedCfgDesc(uint16_t *length);
uint8_t *USBD_CDC_GetDeviceQualifierDescriptor(uint16_t *length);
#endif /* USE_USBD_COMPOSITE  */

#ifndef USE_USBD_COMPOSITE
/* USB Standard Device Descriptor */
__ALIGN_BEGIN static uint8_t USBD_CDC_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END = {
	USB_LEN_DEV_QUALIFIER_DESC,
	USB_DESC_TYPE_DEVICE_QUALIFIER,
	0x00,
	0x02,
	0x00,
	0x00,
	0x00,
	0x40,
	0x01,
	0x00,
};
#endif /* USE_USBD_COMPOSITE  */
/**
 * @}
 */

/** @defgroup USBD_CDC_Private_Variables
 * @{
 */

/* CDC interface class callbacks structure */
USBD_ClassTypeDef USBD_CDC = {
	USBD_CDC_Init,
	USBD_CDC_DeInit,
	USBD_CDC_Setup,
	NULL, /* EP0_TxSent */
	USBD_CDC_EP0_RxReady,
	USBD_CDC_DataIn,
	USBD_CDC_DataOut,
	NULL,
	NULL,
	NULL,
#ifdef USE_USBD_COMPOSITE
	NULL,
	NULL,
	NULL,
	NULL,
#else
	USBD_CDC_GetHSCfgDesc,
	USBD_CDC_GetFSCfgDesc,
	USBD_CDC_GetOtherSpeedCfgDesc,
	USBD_CDC_GetDeviceQualifierDescriptor,
#endif /* USE_USBD_COMPOSITE  */
};

#ifndef USE_USBD_COMPOSITE
/* USB CDC device Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_CDC_CfgDesc[USB_CDC_CONFIG_DESC_SIZ] __ALIGN_END = {
	/* Configuration Descriptor */
	0x09,                        /* bLength: Configuration Descriptor size */
	USB_DESC_TYPE_CONFIGURATION, /* bDescriptorType: Configuration */
	USB_CDC_CONFIG_DESC_SIZ,     /* wTotalLength */
	0x00,
	0x01, /* bNumInterfaces: 2 interfaces */
	0x01, /* bConfigurationValue: Configuration value */
	0x00, /* iConfiguration: Index of string descriptor
	         describing the configuration */
#if (USBD_SELF_POWERED == 1U)
	0xC0, /* bmAttributes: Bus Powered according to user configuration */
#else
	0x80, /* bmAttributes: Bus Powered according to user configuration */
#endif              /* USBD_SELF_POWERED */
	USBD_MAX_POWER, /* MaxPower (mA) */

	/*---------------------------------------------------------------------------*/

	/* Data class interface descriptor */
	0x09,                    /* bLength: Endpoint Descriptor size */
	USB_DESC_TYPE_INTERFACE, /* bDescriptorType: */
	0x00,                    /* bInterfaceNumber: Number of Interface */
	0x00,                    /* bAlternateSetting: Alternate setting */
	0x02,                    /* bNumEndpoints: Two endpoints used */
	0xFF,                    /* bInterfaceClass: CDC */
	0x00,                    /* bInterfaceSubClass */
	0x00,                    /* bInterfaceProtocol */
	0x00,                    /* iInterface */

	/* Endpoint OUT Descriptor */
	0x07,                                /* bLength: Endpoint Descriptor size */
	USB_DESC_TYPE_ENDPOINT,              /* bDescriptorType: Endpoint */
	CDC_OUT_EP,                          /* bEndpointAddress */
	0x02,                                /* bmAttributes: Bulk */
	LOBYTE(CDC_DATA_FS_MAX_PACKET_SIZE), /* wMaxPacketSize */
	HIBYTE(CDC_DATA_FS_MAX_PACKET_SIZE),
	0x00, /* bInterval */

	/* Endpoint IN Descriptor */
	0x07,                                /* bLength: Endpoint Descriptor size */
	USB_DESC_TYPE_ENDPOINT,              /* bDescriptorType: Endpoint */
	CDC_IN_EP,                           /* bEndpointAddress */
	0x02,                                /* bmAttributes: Bulk */
	LOBYTE(CDC_DATA_FS_MAX_PACKET_SIZE), /* wMaxPacketSize */
	HIBYTE(CDC_DATA_FS_MAX_PACKET_SIZE),
	0x00 /* bInterval */
};
#endif /* USE_USBD_COMPOSITE  */

static uint8_t CDCInEpAdd = CDC_IN_EP;
static uint8_t CDCOutEpAdd = CDC_OUT_EP;
static uint8_t CDCCmdEpAdd = CDC_CMD_EP;

/**
 * @}
 */

/** @defgroup USBD_CDC_Private_Functions
 * @{
 */

/**
 * @brief  USBD_CDC_Init
 *         Initialize the CDC interface
 * @param  pdev: device instance
 * @param  cfgidx: Configuration index
 * @retval status
 */
static uint8_t USBD_CDC_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
	UNUSED(cfgidx);
	USBD_CDC_HandleTypeDef *hcdc;

	hcdc = (USBD_CDC_HandleTypeDef *)USBD_malloc(sizeof(USBD_CDC_HandleTypeDef));

	if (hcdc == NULL)
	{
		pdev->pClassDataCmsit[pdev->classId] = NULL;
		return (uint8_t)USBD_EMEM;
	}

	(void)USBD_memset(hcdc, 0, sizeof(USBD_CDC_HandleTypeDef));
	hcdc->CmdOpCode = 0xFFU;

	pdev->pClassDataCmsit[pdev->classId] = (void *)hcdc;
	pdev->pClassData = pdev->pClassDataCmsit[pdev->classId];

#ifdef USE_USBD_COMPOSITE
	/* Get the Endpoints addresses allocated for this class instance */
	CDCInEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
	CDCOutEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
	CDCCmdEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_INTR, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

	if (pdev->dev_speed == USBD_SPEED_HIGH)
	{
		/* Open EP IN */
		(void)USBD_LL_OpenEP(pdev, CDCInEpAdd, USBD_EP_TYPE_BULK,
		    CDC_DATA_HS_IN_PACKET_SIZE);

		pdev->ep_in[CDCInEpAdd & 0xFU].is_used = 1U;

		/* Open EP OUT */
		(void)USBD_LL_OpenEP(pdev, CDCOutEpAdd, USBD_EP_TYPE_BULK,
		    CDC_DATA_HS_OUT_PACKET_SIZE);

		pdev->ep_out[CDCOutEpAdd & 0xFU].is_used = 1U;

		/* Set bInterval for CDC CMD Endpoint */
		pdev->ep_in[CDCCmdEpAdd & 0xFU].bInterval = CDC_HS_BINTERVAL;
	}
	else
	{
		/* Open EP IN */
		(void)USBD_LL_OpenEP(pdev, CDCInEpAdd, USBD_EP_TYPE_BULK,
		    CDC_DATA_FS_IN_PACKET_SIZE);

		pdev->ep_in[CDCInEpAdd & 0xFU].is_used = 1U;

		/* Open EP OUT */
		(void)USBD_LL_OpenEP(pdev, CDCOutEpAdd, USBD_EP_TYPE_BULK,
		    CDC_DATA_FS_OUT_PACKET_SIZE);

		pdev->ep_out[CDCOutEpAdd & 0xFU].is_used = 1U;

		/* Set bInterval for CMD Endpoint */
		pdev->ep_in[CDCCmdEpAdd & 0xFU].bInterval = CDC_FS_BINTERVAL;
	}

	/* Open Command IN EP */
	(void)USBD_LL_OpenEP(pdev, CDCCmdEpAdd, USBD_EP_TYPE_INTR, CDC_CMD_PACKET_SIZE);
	pdev->ep_in[CDCCmdEpAdd & 0xFU].is_used = 1U;

	hcdc->RxBuffer = NULL;

	/* Init  physical Interface components */
	((USBD_CDC_ItfTypeDef *)pdev->pUserData[pdev->classId])->Init();

	/* Init Xfer states */
	hcdc->TxState = 0U;
	hcdc->RxState = 0U;

	if (hcdc->RxBuffer == NULL)
	{
		return (uint8_t)USBD_EMEM;
	}

	if (pdev->dev_speed == USBD_SPEED_HIGH)
	{
		/* Prepare Out endpoint to receive next packet */
		(void)USBD_LL_PrepareReceive(pdev, CDCOutEpAdd, hcdc->RxBuffer,
		    CDC_DATA_HS_OUT_PACKET_SIZE);
	}
	else
	{
		/* Prepare Out endpoint to receive next packet */
		(void)USBD_LL_PrepareReceive(pdev, CDCOutEpAdd, hcdc->RxBuffer,
		    CDC_DATA_FS_OUT_PACKET_SIZE);
	}

	return (uint8_t)USBD_OK;
}

/**
 * @brief  USBD_CDC_Init
 *         DeInitialize the CDC layer
 * @param  pdev: device instance
 * @param  cfgidx: Configuration index
 * @retval status
 */
static uint8_t USBD_CDC_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
	UNUSED(cfgidx);

#ifdef USE_USBD_COMPOSITE
	/* Get the Endpoints addresses allocated for this CDC class instance */
	CDCInEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
	CDCOutEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
	CDCCmdEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_INTR, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

	/* Close EP IN */
	(void)USBD_LL_CloseEP(pdev, CDCInEpAdd);
	pdev->ep_in[CDCInEpAdd & 0xFU].is_used = 0U;

	/* Close EP OUT */
	(void)USBD_LL_CloseEP(pdev, CDCOutEpAdd);
	pdev->ep_out[CDCOutEpAdd & 0xFU].is_used = 0U;

	/* Close Command IN EP */
	(void)USBD_LL_CloseEP(pdev, CDCCmdEpAdd);
	pdev->ep_in[CDCCmdEpAdd & 0xFU].is_used = 0U;
	pdev->ep_in[CDCCmdEpAdd & 0xFU].bInterval = 0U;

	/* DeInit  physical Interface components */
	if (pdev->pClassDataCmsit[pdev->classId] != NULL)
	{
		((USBD_CDC_ItfTypeDef *)pdev->pUserData[pdev->classId])->DeInit();
		(void)USBD_free(pdev->pClassDataCmsit[pdev->classId]);
		pdev->pClassDataCmsit[pdev->classId] = NULL;
		pdev->pClassData = NULL;
	}

	return (uint8_t)USBD_OK;
}

/* Config request codes (按 cp210x.c 定义，使用宏而非硬编码) */
#define CP210X_IFC_ENABLE      0x00
#define CP210X_SET_BAUDDIV     0x01
#define CP210X_GET_BAUDDIV     0x02
#define CP210X_SET_LINE_CTL    0x03
#define CP210X_GET_LINE_CTL    0x04
#define CP210X_SET_BREAK       0x05
#define CP210X_IMM_CHAR        0x06
#define CP210X_SET_MHS         0x07
#define CP210X_GET_MDMSTS      0x08
#define CP210X_SET_XON         0x09
#define CP210X_SET_XOFF        0x0A
#define CP210X_SET_EVENTMASK   0x0B
#define CP210X_GET_EVENTMASK   0x0C
#define CP210X_SET_CHAR        0x0D
#define CP210X_GET_CHARS       0x0E
#define CP210X_GET_PROPS       0x0F
#define CP210X_GET_COMM_STATUS 0x10
#define CP210X_RESET           0x11
#define CP210X_PURGE           0x12
#define CP210X_SET_FLOW        0x13
#define CP210X_GET_FLOW        0x14
#define CP210X_EMBED_EVENTS    0x15
#define CP210X_GET_EVENTSTATE  0x16
#define CP210X_SET_CHARS       0x19
#define CP210X_GET_BAUDRATE    0x1D
#define CP210X_SET_BAUDRATE    0x1E
#define CP210X_VENDOR_SPECIFIC 0xFF

/* Vendor-specific extended values (for CP210X_VENDOR_SPECIFIC bRequest) */
#define CP210X_READ_2NCONFIG  0x000E
#define CP210X_READ_LATCH     0x00C2
#define CP210X_GET_PARTNUM    0x370B
#define CP210X_GET_PORTCONFIG 0x370C
#define CP210X_GET_DEVICEMODE 0x3711
#define CP210X_WRITE_LATCH    0x37E1

/* 常用响应尺寸宏 */
#define CP210X_2NCONFIG_SZ        0x02A6
#define CP210X_COMM_STATUS_SZ     0x13
#define CP210X_FLOW_CTL_SZ        0x10
#define CP210X_PIN_MODE_SZ        0x02
#define CP210X_DUAL_PORT_CFG_SZ   0x0f
#define CP210X_SINGLE_PORT_CFG_SZ 0x0d
#define CP210X_GPIO_WRITE_SZ      0x02

#define CP210X_USB_VID            0x10C4U
#define CP210X_USB_PID            0xEA60U
#define CP210X_DEV_RELEASE        0x0100U
#define CP210X_CFG_ATTR_BUS_PWR   0x80U
#define CP210X_CFG_MAX_POWER      0x32U

/* 内部结构（按小端发送，内容按兼容性返回，可全部置零或根据当前状态返回） */
struct cp210x_comm_status
{
	uint32_t ulErrors;
	uint32_t ulHoldReasons;
	uint32_t ulAmountInInQueue;
	uint32_t ulAmountInOutQueue;
	uint8_t bEofReceived;
	uint8_t bWaitForImmediate;
	uint8_t bReserved;
} __attribute__((packed));

struct cp210x_flow_ctl
{
	uint32_t ulControlHandshake;
	uint32_t ulFlowReplace;
	uint32_t ulXonLimit;
	uint32_t ulXoffLimit;
} __attribute__((packed));

/* CP210x 状态: 波特率和线路控制（跨控制请求持久保存） */
static uint32_t cp210x_baudrate = 115200U;
static uint8_t cp210x_2nconfig[CP210X_2NCONFIG_SZ];
static uint16_t cp210x_line_ctl = 0x0800U; /* 默认: 8位数据, 无校验, 1位停止 */

static void CP210X_Build2NConfig(void)
{
	(void)USBD_memset(cp210x_2nconfig, 0, sizeof(cp210x_2nconfig));

	/* CP2102N configuration array header and descriptor mirror (AN978 table 1.1). */
	cp210x_2nconfig[0]  = LOBYTE(CP210X_2NCONFIG_SZ);
	cp210x_2nconfig[1]  = HIBYTE(CP210X_2NCONFIG_SZ);
	cp210x_2nconfig[2]  = 0x01U; /* configVersion */
	cp210x_2nconfig[3]  = 0xFFU; /* enableBootloader */
	cp210x_2nconfig[4]  = 0xFFU; /* enableConfigUpdate */
	cp210x_2nconfig[5]  = USB_LEN_DEV_DESC;
	cp210x_2nconfig[6]  = USB_DESC_TYPE_DEVICE;
	cp210x_2nconfig[7]  = 0x00U;
	cp210x_2nconfig[8]  = 0x02U;
	cp210x_2nconfig[12] = USB_MAX_EP0_SIZE;
	cp210x_2nconfig[13] = LOBYTE(CP210X_USB_VID);
	cp210x_2nconfig[14] = HIBYTE(CP210X_USB_VID);
	cp210x_2nconfig[15] = LOBYTE(CP210X_USB_PID);
	cp210x_2nconfig[16] = HIBYTE(CP210X_USB_PID);
	cp210x_2nconfig[17] = LOBYTE(CP210X_DEV_RELEASE);
	cp210x_2nconfig[18] = HIBYTE(CP210X_DEV_RELEASE);
	cp210x_2nconfig[19] = 0x01U;
	cp210x_2nconfig[20] = 0x02U;
	cp210x_2nconfig[21] = 0x03U;
	cp210x_2nconfig[22] = 0x01U;

	cp210x_2nconfig[23] = USB_LEN_CFG_DESC;
	cp210x_2nconfig[24] = USB_DESC_TYPE_CONFIGURATION;
	cp210x_2nconfig[25] = LOBYTE(USB_CDC_CONFIG_DESC_SIZ);
	cp210x_2nconfig[26] = HIBYTE(USB_CDC_CONFIG_DESC_SIZ);
	cp210x_2nconfig[27] = 0x01U;
	cp210x_2nconfig[28] = 0x01U;
	cp210x_2nconfig[30] = CP210X_CFG_ATTR_BUS_PWR;
	cp210x_2nconfig[31] = CP210X_CFG_MAX_POWER;

	cp210x_2nconfig[32] = USB_LEN_IF_DESC;
	cp210x_2nconfig[33] = USB_DESC_TYPE_INTERFACE;
	cp210x_2nconfig[36] = 0x02U;
	cp210x_2nconfig[37] = 0xFFU;

	cp210x_2nconfig[41] = USB_LEN_EP_DESC;
	cp210x_2nconfig[42] = USB_DESC_TYPE_ENDPOINT;
	cp210x_2nconfig[43] = CDC_OUT_EP;
	cp210x_2nconfig[44] = USBD_EP_TYPE_BULK;
	cp210x_2nconfig[45] = LOBYTE(CDC_DATA_FS_MAX_PACKET_SIZE);
	cp210x_2nconfig[46] = HIBYTE(CDC_DATA_FS_MAX_PACKET_SIZE);

	cp210x_2nconfig[48] = USB_LEN_EP_DESC;
	cp210x_2nconfig[49] = USB_DESC_TYPE_ENDPOINT;
	cp210x_2nconfig[50] = CDC_IN_EP;
	cp210x_2nconfig[51] = USBD_EP_TYPE_BULK;
	cp210x_2nconfig[52] = LOBYTE(CDC_DATA_FS_MAX_PACKET_SIZE);
	cp210x_2nconfig[53] = HIBYTE(CDC_DATA_FS_MAX_PACKET_SIZE);

	cp210x_2nconfig[55] = 0x04U;
	cp210x_2nconfig[56] = USB_DESC_TYPE_STRING;
	cp210x_2nconfig[57] = 0x09U;
	cp210x_2nconfig[58] = 0x04U;
	cp210x_2nconfig[580] = 0x52U;
	cp210x_2nconfig[581] = 0x05U;
	cp210x_2nconfig[583] = 0xFFU;
	cp210x_2nconfig[584] = 0xFFU;
	cp210x_2nconfig[586] = 0xFFU;
	cp210x_2nconfig[587] = 0xFEU;
	cp210x_2nconfig[589] = 0x52U;
	cp210x_2nconfig[590] = 0x05U;
	cp210x_2nconfig[592] = 0xFFU;
	cp210x_2nconfig[593] = 0xFFU;
	cp210x_2nconfig[595] = 0xFFU;
	cp210x_2nconfig[596] = 0xFBU;
	cp210x_2nconfig[598] = 0x0FU;
}

/**
 * @brief  USBD_CDC_Setup
 *         Handle the CDC specific requests
 * @param  pdev: instance
 * @param  req: usb requests
 * @retval status
 */
static uint8_t USBD_CDC_Setup(USBD_HandleTypeDef *pdev,
    USBD_SetupReqTypedef *req)
{
	USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
	uint16_t len;
	uint8_t ifalt = 0U;
	uint16_t status_info = 0U;
	USBD_StatusTypeDef ret = USBD_OK;

	if (hcdc == NULL)
	{
		return (uint8_t)USBD_FAIL;
	}

	switch (req->bmRequest & USB_REQ_TYPE_MASK)
	{
	case USB_REQ_TYPE_CLASS:
		if (req->wLength != 0U)
		{
			if ((req->bmRequest & 0x80U) != 0U)
			{
				((USBD_CDC_ItfTypeDef *)pdev->pUserData[pdev->classId])->Control(req->bRequest, (uint8_t *)hcdc->data, req->wLength);

				len = MIN(CDC_REQ_MAX_DATA_SIZE, req->wLength);
				(void)USBD_CtlSendData(pdev, (uint8_t *)hcdc->data, len);
			}
			else
			{
				hcdc->CmdOpCode = req->bRequest;
				hcdc->CmdLength = (uint8_t)MIN(req->wLength, USB_MAX_EP0_SIZE);

				(void)USBD_CtlPrepareRx(pdev, (uint8_t *)hcdc->data, hcdc->CmdLength);
			}
		}
		else
		{
			((USBD_CDC_ItfTypeDef *)pdev->pUserData[pdev->classId])->Control(req->bRequest, (uint8_t *)req, 0U);
		}
		break;

	case USB_REQ_TYPE_STANDARD:
		switch (req->bRequest)
		{
		case USB_REQ_GET_STATUS:
			if (pdev->dev_state == USBD_STATE_CONFIGURED)
			{
				(void)USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
			}
			else
			{
				USBD_CtlError(pdev, req);
				ret = USBD_FAIL;
			}
			break;

		case USB_REQ_GET_INTERFACE:
			if (pdev->dev_state == USBD_STATE_CONFIGURED)
			{
				(void)USBD_CtlSendData(pdev, &ifalt, 1U);
			}
			else
			{
				USBD_CtlError(pdev, req);
				ret = USBD_FAIL;
			}
			break;

		case USB_REQ_SET_INTERFACE:
			if (pdev->dev_state != USBD_STATE_CONFIGURED)
			{
				USBD_CtlError(pdev, req);
				ret = USBD_FAIL;
			}
			break;

		case USB_REQ_CLEAR_FEATURE:
			break;

		default:
			USBD_CtlError(pdev, req);
			ret = USBD_FAIL;
			break;
		}
		break;

	case USB_REQ_TYPE_VENDOR:
	{
		uint8_t rcpt = req->bmRequest & USB_REQ_RECIPIENT_MASK;
		if ((rcpt == USB_REQ_RECIPIENT_INTERFACE) || (rcpt == USB_REQ_RECIPIENT_DEVICE))
		{
			uint16_t wLen = req->wLength;
			if (req->bRequest == CP210X_VENDOR_SPECIFIC)
			{
				/* 通过 wValue 区分 vendor-specific 子命令 */
				switch (req->wValue)
				{
				case CP210X_GET_PARTNUM:
					((uint8_t *)hcdc->data)[0] = 0x0AU; /* CP2102N */
					if (wLen >= 1U)
					{
						(void)USBD_CtlSendData(pdev, (uint8_t *)hcdc->data, 1U);
					}
					else
					{
						USBD_CtlError(pdev, req);
						ret = USBD_FAIL;
					}
					break;
				case CP210X_READ_LATCH:
				case CP210X_READ_2NCONFIG:
				case CP210X_GET_PORTCONFIG:
				case CP210X_GET_DEVICEMODE:
				{
					uint16_t respSz;
					if (req->wValue == CP210X_READ_2NCONFIG)
					{
						respSz = (uint16_t)CP210X_2NCONFIG_SZ;
						uint16_t sendSz = (wLen < respSz) ? wLen : respSz;
						CP210X_Build2NConfig();
						(void)USBD_CtlSendData(pdev, cp210x_2nconfig, sendSz);
						break;
					}
					else if (req->wValue == CP210X_GET_PORTCONFIG)
						respSz = (uint16_t)CP210X_SINGLE_PORT_CFG_SZ;
					else if (req->wValue == CP210X_GET_DEVICEMODE)
						respSz = (uint16_t)CP210X_PIN_MODE_SZ;
					else
						respSz = 1U;
					uint16_t sendSz = (wLen < respSz) ? wLen : respSz;
					if (sendSz > (uint16_t)sizeof(hcdc->data))
						sendSz = (uint16_t)sizeof(hcdc->data);
					(void)USBD_memset(hcdc->data, 0, sendSz);
					(void)USBD_CtlSendData(pdev, (uint8_t *)hcdc->data, sendSz);
					break;
				}
				case CP210X_WRITE_LATCH:
					/* HOST_TO_DEVICE: 接受数据后忽略 */
					if (wLen > 0U)
					{
						hcdc->CmdOpCode = 0xFEU; /* 标记: vendor OUT, 数据接收后忽略 */
						hcdc->CmdLength = (uint8_t)MIN(wLen, USB_MAX_EP0_SIZE);
						(void)USBD_CtlPrepareRx(pdev, (uint8_t *)hcdc->data, hcdc->CmdLength);
					}
					break;
				default:
					USBD_CtlError(pdev, req);
					ret = USBD_FAIL;
					break;
				}
			}
			else
			{
				/* bRequest 直接为 CP210x 配置命令码 */
				switch (req->bRequest)
				{
				case CP210X_IFC_ENABLE:
				case CP210X_SET_BREAK:
				case CP210X_IMM_CHAR:
				case CP210X_SET_MHS:
				case CP210X_SET_XON:
				case CP210X_SET_XOFF:
				case CP210X_RESET:
				case CP210X_PURGE:
				case CP210X_SET_EVENTMASK:
				case CP210X_SET_BAUDDIV:
					/* HOST_TO_DEVICE, 无数据阶段, ACK */
					break;
				case CP210X_SET_CHAR:
				case CP210X_SET_CHARS:
					if (wLen > 0U)
					{
						hcdc->CmdOpCode = 0x80U | req->bRequest;
						hcdc->CmdLength = (uint8_t)MIN(wLen, USB_MAX_EP0_SIZE);
						(void)USBD_CtlPrepareRx(pdev, (uint8_t *)hcdc->data, hcdc->CmdLength);
					}
					break;
				case CP210X_SET_LINE_CTL:
				{
					/* 线路控制编码于 wValue: [11:8]=数据位, [7:4]=校验, [3:0]=停止位 */
					USBD_CDC_LineCodingTypeDef lc;
					cp210x_line_ctl = req->wValue;
					lc.datatype   = (uint8_t)((req->wValue >> 8U) & 0x0FU);
					lc.paritytype = (uint8_t)((req->wValue >> 4U) & 0x0FU);
					lc.format     = (uint8_t)(req->wValue & 0x0FU);
					lc.bitrate    = cp210x_baudrate;
					(void)USBD_memcpy(hcdc->data, &lc, sizeof(lc));
					((USBD_CDC_ItfTypeDef *)pdev->pUserData[pdev->classId])->Control(
					    CDC_SET_LINE_CODING, (uint8_t *)hcdc->data, (uint16_t)sizeof(lc));
					break;
				}
				case CP210X_GET_LINE_CTL:
					/* 返回已存储的 16 位线路控制值（小端序） */
					((uint8_t *)hcdc->data)[0] = (uint8_t)(cp210x_line_ctl & 0xFFU);
					((uint8_t *)hcdc->data)[1] = (uint8_t)(cp210x_line_ctl >> 8U);
					(void)USBD_CtlSendData(pdev, (uint8_t *)hcdc->data, 2U);
					break;
				case CP210X_GET_BAUDDIV:
					(void)USBD_memset(hcdc->data, 0, 4U);
					(void)USBD_CtlSendData(pdev, (uint8_t *)hcdc->data, 4U);
					break;
				case CP210X_GET_MDMSTS:
					((uint8_t *)hcdc->data)[0] = 0x00U;
					(void)USBD_CtlSendData(pdev, (uint8_t *)hcdc->data, 1U);
					break;
				case CP210X_GET_EVENTMASK:
					(void)USBD_memset(hcdc->data, 0, 4U);
					(void)USBD_CtlSendData(pdev, (uint8_t *)hcdc->data, 4U);
					break;
				case CP210X_GET_CHARS:
					(void)USBD_memset(hcdc->data, 0, 6U);
					(void)USBD_CtlSendData(pdev, (uint8_t *)hcdc->data, 6U);
					break;
				case CP210X_GET_PROPS:
				{
					static const uint8_t props[66] = {
					    0x42U, 0x00U, 0x00U, 0x01U, 0x01U, 0x00U, 0x00U, 0x00U,
					    0x00U, 0x00U, 0x00U, 0x00U, 0x80U, 0x02U, 0x00U, 0x00U,
					    0x80U, 0x02U, 0x00U, 0x00U, 0xC0U, 0xC6U, 0x2DU, 0x10U,
					    0x01U, 0x00U, 0x00U, 0x00U, 0x3FU, 0x01U, 0x00U, 0x00U,
					    0x7FU, 0x00U, 0x00U, 0x00U, 0xFFU, 0xFFU, 0x07U, 0x10U,
					    0x0FU, 0x00U, 0x07U, 0x1FU, 0x80U, 0x02U, 0x00U, 0x00U,
					    0x80U, 0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
					    0x00U, 0x00U, 0x00U, 0x00U, 0x33U, 0x00U, 0x2EU, 0x00U,
					    0x30U, 0x00U
					};
					uint16_t sendSz = (wLen < (uint16_t)sizeof(props)) ? wLen : (uint16_t)sizeof(props);
					(void)USBD_CtlSendData(pdev, (uint8_t *)props, sendSz);
					break;
				}
				case CP210X_GET_COMM_STATUS:
					if (wLen >= (uint16_t)CP210X_COMM_STATUS_SZ)
					{
						(void)USBD_memset(hcdc->data, 0, CP210X_COMM_STATUS_SZ);
						(void)USBD_CtlSendData(pdev, (uint8_t *)hcdc->data, (uint16_t)CP210X_COMM_STATUS_SZ);
					}
					else
					{
						USBD_CtlError(pdev, req);
						ret = USBD_FAIL;
					}
					break;
				case CP210X_SET_FLOW:
					/* HOST_TO_DEVICE, 16字节数据: 准备接收 */
					if (wLen >= (uint16_t)CP210X_FLOW_CTL_SZ)
					{
						hcdc->CmdOpCode = 0x80U | CP210X_SET_FLOW;
						hcdc->CmdLength = (uint8_t)CP210X_FLOW_CTL_SZ;
						(void)USBD_CtlPrepareRx(pdev, (uint8_t *)hcdc->data, (uint16_t)CP210X_FLOW_CTL_SZ);
					}
					break;
				case CP210X_GET_FLOW:
					(void)USBD_memset(hcdc->data, 0, CP210X_FLOW_CTL_SZ);
					(void)USBD_CtlSendData(pdev, (uint8_t *)hcdc->data, (uint16_t)CP210X_FLOW_CTL_SZ);
					break;
				case CP210X_EMBED_EVENTS:
				case CP210X_GET_EVENTSTATE:
				{
					uint16_t respSz = (wLen > 0U) ? wLen : 4U;
					if (respSz > (uint16_t)sizeof(hcdc->data))
						respSz = (uint16_t)sizeof(hcdc->data);
					(void)USBD_memset(hcdc->data, 0, respSz);
					(void)USBD_CtlSendData(pdev, (uint8_t *)hcdc->data, respSz);
					break;
				}
				case CP210X_GET_BAUDRATE:
				{
					uint8_t *buf = (uint8_t *)hcdc->data;
					buf[0] = (uint8_t)(cp210x_baudrate & 0xFFU);
					buf[1] = (uint8_t)((cp210x_baudrate >> 8U) & 0xFFU);
					buf[2] = (uint8_t)((cp210x_baudrate >> 16U) & 0xFFU);
					buf[3] = (uint8_t)((cp210x_baudrate >> 24U) & 0xFFU);
					(void)USBD_CtlSendData(pdev, (uint8_t *)hcdc->data, 4U);
					break;
				}
				case CP210X_SET_BAUDRATE:
					/* HOST_TO_DEVICE, 4字节波特率数据: 准备接收 */
					if (wLen >= 4U)
					{
						hcdc->CmdOpCode = 0x80U | CP210X_SET_BAUDRATE;
						hcdc->CmdLength = 4U;
						(void)USBD_CtlPrepareRx(pdev, (uint8_t *)hcdc->data, 4U);
					}
					break;
				default:
					USBD_CtlError(pdev, req);
					ret = USBD_FAIL;
					break;
				}
			}
		}
		else
		{
			USBD_CtlError(pdev, req);
			ret = USBD_FAIL;
		}
		break;
	}

	default:
		USBD_CtlError(pdev, req);
		ret = USBD_FAIL;
		break;
	}

	return (uint8_t)ret;
}

/**
 * @brief  USBD_CDC_DataIn
 *         Data sent on non-control IN endpoint
 * @param  pdev: device instance
 * @param  epnum: endpoint number
 * @retval status
 */
static uint8_t USBD_CDC_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
	USBD_CDC_HandleTypeDef *hcdc;
	PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef *)pdev->pData;

	if (pdev->pClassDataCmsit[pdev->classId] == NULL)
	{
		return (uint8_t)USBD_FAIL;
	}

	hcdc = (USBD_CDC_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

	if ((pdev->ep_in[epnum & 0xFU].total_length > 0U) &&
	    ((pdev->ep_in[epnum & 0xFU].total_length % hpcd->IN_ep[epnum & 0xFU].maxpacket) == 0U))
	{
		/* Update the packet total length */
		pdev->ep_in[epnum & 0xFU].total_length = 0U;

		/* Send ZLP */
		(void)USBD_LL_Transmit(pdev, epnum, NULL, 0U);
	}
	else
	{
		hcdc->TxState = 0U;

		if (((USBD_CDC_ItfTypeDef *)pdev->pUserData[pdev->classId])->TransmitCplt != NULL)
		{
			((USBD_CDC_ItfTypeDef *)pdev->pUserData[pdev->classId])->TransmitCplt(hcdc->TxBuffer, &hcdc->TxLength, epnum);
		}
	}

	return (uint8_t)USBD_OK;
}

/**
 * @brief  USBD_CDC_DataOut
 *         Data received on non-control Out endpoint
 * @param  pdev: device instance
 * @param  epnum: endpoint number
 * @retval status
 */
static uint8_t USBD_CDC_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
	USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

	if (pdev->pClassDataCmsit[pdev->classId] == NULL)
	{
		return (uint8_t)USBD_FAIL;
	}

	/* Get the received data length */
	hcdc->RxLength = USBD_LL_GetRxDataSize(pdev, epnum);

	/* USB data will be immediately processed, this allow next USB traffic being
	NAKed till the end of the application Xfer */

	((USBD_CDC_ItfTypeDef *)pdev->pUserData[pdev->classId])->Receive(hcdc->RxBuffer, &hcdc->RxLength);

	return (uint8_t)USBD_OK;
}

/**
 * @brief  USBD_CDC_EP0_RxReady
 *         Handle EP0 Rx Ready event
 * @param  pdev: device instance
 * @retval status
 */
static uint8_t USBD_CDC_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
	USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

	if (hcdc == NULL)
	{
		return (uint8_t)USBD_FAIL;
	}

	if (hcdc->CmdOpCode != 0xFFU)
	{
		if ((hcdc->CmdOpCode & 0x80U) != 0U)
		{
			/* CP210x vendor OUT 命令: 处理已接收数据 */
			if ((hcdc->CmdOpCode & 0x7FU) == CP210X_SET_BAUDRATE)
			{
				/* 从接收缓冲区解析小端序波特率 */
				uint8_t *buf = (uint8_t *)hcdc->data;
				cp210x_baudrate = ((uint32_t)buf[0])        |
				                  ((uint32_t)buf[1] << 8U)  |
				                  ((uint32_t)buf[2] << 16U) |
				                  ((uint32_t)buf[3] << 24U);
				/* 将新波特率同步给用户应用 */
				if (pdev->pUserData[pdev->classId] != NULL)
				{
					USBD_CDC_LineCodingTypeDef lc;
					lc.bitrate    = cp210x_baudrate;
					lc.datatype   = (uint8_t)((cp210x_line_ctl >> 8U) & 0x0FU);
					lc.paritytype = (uint8_t)((cp210x_line_ctl >> 4U) & 0x0FU);
					lc.format     = (uint8_t)(cp210x_line_ctl & 0x0FU);
					(void)USBD_memcpy(hcdc->data, &lc, sizeof(lc));
					((USBD_CDC_ItfTypeDef *)pdev->pUserData[pdev->classId])->Control(
					    CDC_SET_LINE_CODING, (uint8_t *)hcdc->data, (uint16_t)sizeof(lc));
				}
			}
			/* SET_FLOW 及其他 vendor OUT 命令: 数据已接收, 无需进一步处理 */
			hcdc->CmdOpCode = 0xFFU;
		}
		else if (pdev->pUserData[pdev->classId] != NULL)
		{
			((USBD_CDC_ItfTypeDef *)pdev->pUserData[pdev->classId])->Control(hcdc->CmdOpCode, (uint8_t *)hcdc->data, (uint16_t)hcdc->CmdLength);
			hcdc->CmdOpCode = 0xFFU;
		}
		else
		{
			hcdc->CmdOpCode = 0xFFU;
		}
	}

	return (uint8_t)USBD_OK;
}
#ifndef USE_USBD_COMPOSITE
/**
 * @brief  USBD_CDC_GetFSCfgDesc
 *         Return configuration descriptor
 * @param  length : pointer data length
 * @retval pointer to descriptor buffer
 */
static uint8_t *USBD_CDC_GetFSCfgDesc(uint16_t *length)
{
	USBD_EpDescTypeDef *pEpCmdDesc = USBD_GetEpDesc(USBD_CDC_CfgDesc, CDC_CMD_EP);
	USBD_EpDescTypeDef *pEpOutDesc = USBD_GetEpDesc(USBD_CDC_CfgDesc, CDC_OUT_EP);
	USBD_EpDescTypeDef *pEpInDesc = USBD_GetEpDesc(USBD_CDC_CfgDesc, CDC_IN_EP);

	if (pEpCmdDesc != NULL)
	{
		pEpCmdDesc->bInterval = CDC_FS_BINTERVAL;
	}

	if (pEpOutDesc != NULL)
	{
		pEpOutDesc->wMaxPacketSize = CDC_DATA_FS_MAX_PACKET_SIZE;
	}

	if (pEpInDesc != NULL)
	{
		pEpInDesc->wMaxPacketSize = CDC_DATA_FS_MAX_PACKET_SIZE;
	}

	*length = (uint16_t)sizeof(USBD_CDC_CfgDesc);
	return USBD_CDC_CfgDesc;
}

/**
 * @brief  USBD_CDC_GetHSCfgDesc
 *         Return configuration descriptor
 * @param  length : pointer data length
 * @retval pointer to descriptor buffer
 */
static uint8_t *USBD_CDC_GetHSCfgDesc(uint16_t *length)
{
	USBD_EpDescTypeDef *pEpCmdDesc = USBD_GetEpDesc(USBD_CDC_CfgDesc, CDC_CMD_EP);
	USBD_EpDescTypeDef *pEpOutDesc = USBD_GetEpDesc(USBD_CDC_CfgDesc, CDC_OUT_EP);
	USBD_EpDescTypeDef *pEpInDesc = USBD_GetEpDesc(USBD_CDC_CfgDesc, CDC_IN_EP);

	if (pEpCmdDesc != NULL)
	{
		pEpCmdDesc->bInterval = CDC_HS_BINTERVAL;
	}

	if (pEpOutDesc != NULL)
	{
		pEpOutDesc->wMaxPacketSize = CDC_DATA_HS_MAX_PACKET_SIZE;
	}

	if (pEpInDesc != NULL)
	{
		pEpInDesc->wMaxPacketSize = CDC_DATA_HS_MAX_PACKET_SIZE;
	}

	*length = (uint16_t)sizeof(USBD_CDC_CfgDesc);
	return USBD_CDC_CfgDesc;
}

/**
 * @brief  USBD_CDC_GetOtherSpeedCfgDesc
 *         Return configuration descriptor
 * @param  length : pointer data length
 * @retval pointer to descriptor buffer
 */
static uint8_t *USBD_CDC_GetOtherSpeedCfgDesc(uint16_t *length)
{
	USBD_EpDescTypeDef *pEpCmdDesc = USBD_GetEpDesc(USBD_CDC_CfgDesc, CDC_CMD_EP);
	USBD_EpDescTypeDef *pEpOutDesc = USBD_GetEpDesc(USBD_CDC_CfgDesc, CDC_OUT_EP);
	USBD_EpDescTypeDef *pEpInDesc = USBD_GetEpDesc(USBD_CDC_CfgDesc, CDC_IN_EP);

	if (pEpCmdDesc != NULL)
	{
		pEpCmdDesc->bInterval = CDC_FS_BINTERVAL;
	}

	if (pEpOutDesc != NULL)
	{
		pEpOutDesc->wMaxPacketSize = CDC_DATA_FS_MAX_PACKET_SIZE;
	}

	if (pEpInDesc != NULL)
	{
		pEpInDesc->wMaxPacketSize = CDC_DATA_FS_MAX_PACKET_SIZE;
	}

	*length = (uint16_t)sizeof(USBD_CDC_CfgDesc);
	return USBD_CDC_CfgDesc;
}

/**
 * @brief  USBD_CDC_GetDeviceQualifierDescriptor
 *         return Device Qualifier descriptor
 * @param  length : pointer data length
 * @retval pointer to descriptor buffer
 */
uint8_t *USBD_CDC_GetDeviceQualifierDescriptor(uint16_t *length)
{
	*length = (uint16_t)sizeof(USBD_CDC_DeviceQualifierDesc);

	return USBD_CDC_DeviceQualifierDesc;
}
#endif /* USE_USBD_COMPOSITE  */
/**
 * @brief  USBD_CDC_RegisterInterface
 * @param  pdev: device instance
 * @param  fops: CD  Interface callback
 * @retval status
 */
uint8_t USBD_CDC_RegisterInterface(USBD_HandleTypeDef *pdev,
    USBD_CDC_ItfTypeDef *fops)
{
	if (fops == NULL)
	{
		return (uint8_t)USBD_FAIL;
	}

	pdev->pUserData[pdev->classId] = fops;

	return (uint8_t)USBD_OK;
}

/**
 * @brief  USBD_CDC_SetTxBuffer
 * @param  pdev: device instance
 * @param  pbuff: Tx Buffer
 * @param  length: length of data to be sent
 * @param  ClassId: The Class ID
 * @retval status
 */
#ifdef USE_USBD_COMPOSITE
uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *pdev,
    uint8_t *pbuff, uint32_t length, uint8_t ClassId)
{
	USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)pdev->pClassDataCmsit[ClassId];
#else
uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *pdev,
    uint8_t *pbuff, uint32_t length)
{
	USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
#endif /* USE_USBD_COMPOSITE */

	if (hcdc == NULL)
	{
		return (uint8_t)USBD_FAIL;
	}

	hcdc->TxBuffer = pbuff;
	hcdc->TxLength = length;

	return (uint8_t)USBD_OK;
}

/**
 * @brief  USBD_CDC_SetRxBuffer
 * @param  pdev: device instance
 * @param  pbuff: Rx Buffer
 * @retval status
 */
uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff)
{
	USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

	if (hcdc == NULL)
	{
		return (uint8_t)USBD_FAIL;
	}

	hcdc->RxBuffer = pbuff;

	return (uint8_t)USBD_OK;
}

/**
 * @brief  USBD_CDC_TransmitPacket
 *         Transmit packet on IN endpoint
 * @param  pdev: device instance
 * @param  ClassId: The Class ID
 * @retval status
 */
#ifdef USE_USBD_COMPOSITE
uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *pdev, uint8_t ClassId)
{
	USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)pdev->pClassDataCmsit[ClassId];
#else
uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *pdev)
{
	USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
#endif /* USE_USBD_COMPOSITE */

	USBD_StatusTypeDef ret = USBD_BUSY;

#ifdef USE_USBD_COMPOSITE
	/* Get the Endpoints addresses allocated for this class instance */
	CDCInEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_BULK, ClassId);
#endif /* USE_USBD_COMPOSITE */

	if (hcdc == NULL)
	{
		return (uint8_t)USBD_FAIL;
	}

	if (hcdc->TxState == 0U)
	{
		/* Tx Transfer in progress */
		hcdc->TxState = 1U;

		/* Update the packet total length */
		pdev->ep_in[CDCInEpAdd & 0xFU].total_length = hcdc->TxLength;

		/* Transmit next packet */
		(void)USBD_LL_Transmit(pdev, CDCInEpAdd, hcdc->TxBuffer, hcdc->TxLength);

		ret = USBD_OK;
	}

	return (uint8_t)ret;
}

/**
 * @brief  USBD_CDC_ReceivePacket
 *         prepare OUT Endpoint for reception
 * @param  pdev: device instance
 * @retval status
 */
uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *pdev)
{
	USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

#ifdef USE_USBD_COMPOSITE
	/* Get the Endpoints addresses allocated for this class instance */
	CDCOutEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

	if (pdev->pClassDataCmsit[pdev->classId] == NULL)
	{
		return (uint8_t)USBD_FAIL;
	}

	if (pdev->dev_speed == USBD_SPEED_HIGH)
	{
		/* Prepare Out endpoint to receive next packet */
		(void)USBD_LL_PrepareReceive(pdev, CDCOutEpAdd, hcdc->RxBuffer,
		    CDC_DATA_HS_OUT_PACKET_SIZE);
	}
	else
	{
		/* Prepare Out endpoint to receive next packet */
		(void)USBD_LL_PrepareReceive(pdev, CDCOutEpAdd, hcdc->RxBuffer,
		    CDC_DATA_FS_OUT_PACKET_SIZE);
	}

	return (uint8_t)USBD_OK;
}
/**
 * @}
 */

/**
 * @}
 */

/**
 * @}
 */
