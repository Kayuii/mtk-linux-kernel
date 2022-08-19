/*
** Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/os/linux/hif/sdio/include/hif.h#1
*/

/*! \file   "hif.h"
    \brief  Functions for the driver to register bus and setup the IRQ

    Functions for the driver to register bus and setup the IRQ
*/

/*
** Log: hif.h
**
** 09 17 2012 cm.chang
** [BORA00002149] [MT6630 Wi-Fi] Initial software development
** Duplicate source from MT6620 v2.3 driver branch
** (Davinci label: MT6620_WIFI_Driver_V2_3_120913_1942_As_MT6630_Base)
 *
 * 11 01 2010 yarco.yang
 * [WCXRP00000149] [MT6620 WI-Fi][Driver]Fine tune performance on MT6516 platform
 * Add GPIO debug function
 *
 * 10 19 2010 jeffrey.chang
 * [WCXRP00000120] [MT6620 Wi-Fi][Driver] Refine linux kernel module to the license of
 * MTK propietary and enable MTK HIF by default
 * Refine linux kernel module to the license of MTK and enable MTK HIF
 *
 * 08 18 2010 jeffrey.chang
 * NULL
 * support multi-function sdio
 *
 * 08 17 2010 cp.wu
 * NULL
 * add ENE SDIO host workaround for x86 linux platform.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * initial import for Linux port
**  \main\maintrunk.MT5921\4 2009-10-20 17:38:28 GMT mtk01090
**  Refine driver unloading and clean up procedure. Block requests, stop main thread and clean up queued requests,
**  and then stop hw.
**  \main\maintrunk.MT5921\3 2009-09-28 20:19:20 GMT mtk01090
**  Add private ioctl to carry OID structures. Restructure public/private ioctl interfaces to Linux kernel.
**  \main\maintrunk.MT5921\2 2009-08-18 22:57:05 GMT mtk01090
**  Add Linux SDIO (with mmc core) support.
**  Add Linux 2.6.21, 2.6.25, 2.6.26.
**  Fix compile warning in Linux.
**  \main\maintrunk.MT5921\2 2008-09-22 23:18:17 GMT mtk01461
**  Update driver for code review
** Revision 1.1  2007/07/05 07:25:33  MTK01461
** Add Linux initial code, modify doc, add 11BB, RF init code
**
** Revision 1.3  2007/06/27 02:18:51  MTK01461
** Update SCAN_FSM, Initial(Can Load Module), Proc(Can do Reg R/W), TX API
**
*/

#ifndef _HIF_H
#define _HIF_H

#if MTK_WCN_HIF_SDIO
#include "hif_sdio.h"
#endif

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#if defined(_HIF_SDIO)
#define HIF_NAME "SDIO"
#else
#error "No HIF defined!"
#endif

#define SDIO_X86_WORKAROUND_WRITE_MCR   0x00C4
#define HIF_NUM_OF_QM_RX_PKT_NUM        512

#define HIF_TX_INIT_CMD_PORT            TX_RING_FWDL_IDX_3

#if defined(_HIF_SDIO) && defined(WINDOWS_CE)
#define HIF_IST_LOOP_COUNT 1
#else
#define HIF_IST_LOOP_COUNT 32
#endif

#define HIF_TX_BUFF_COUNT_TC0           8
#define HIF_TX_BUFF_COUNT_TC1           160
#define HIF_TX_BUFF_COUNT_TC2           8
#define HIF_TX_BUFF_COUNT_TC3           8
#define HIF_TX_BUFF_COUNT_TC4           26
#define HIF_TX_BUFF_COUNT_TC5           8

#define HIF_TX_PAGE_SIZE_IN_POWER_OF_2      11
#define HIF_TX_PAGE_SIZE                    2048	/* in unit of bytes */

#define HIF_CR4_FWDL_SECTION_NUM            2
#define HIF_IMG_DL_STATUS_PORT_IDX          0

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/* host interface's private data structure, which is attached to os glue
** layer info structure.
 */
typedef struct _GL_HIF_INFO_T {
#if MTK_WCN_HIF_SDIO
	MTK_WCN_HIF_SDIO_CLTCTX cltCtx;

	const MTK_WCN_HIF_SDIO_FUNCINFO *prFuncInfo;
#else
	struct sdio_func *func;
#endif
	BOOLEAN fgIntReadClear;
	BOOLEAN fgMbxReadClear;
	QUE_T rFreeQueue;
} GL_HIF_INFO_T, *P_GL_HIF_INFO_T;

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

WLAN_STATUS glRegisterBus(probe_card pfProbe, remove_card pfRemove);

VOID glUnregisterBus(remove_card pfRemove);

VOID glSetHifInfo(P_GLUE_INFO_T prGlueInfo, ULONG ulCookie);

VOID glClearHifInfo(P_GLUE_INFO_T prGlueInfo);

BOOL glBusInit(PVOID pvData);

VOID glBusRelease(PVOID pData);

INT_32 glBusSetIrq(PVOID pvData, PVOID pfnIsr, PVOID pvCookie);

VOID glBusFreeIrq(PVOID pvData, PVOID pvCookie);

VOID glSetPowerState(IN P_GLUE_INFO_T prGlueInfo, IN UINT_32 ePowerMode);

void glGetDev(PVOID ctx, struct device **dev);

void glGetHifDev(P_GL_HIF_INFO_T prHif, struct device **dev);

#if !CFG_SDIO_INTR_ENHANCE
VOID halRxSDIOReceiveRFBs(IN P_ADAPTER_T prAdapter);

WLAN_STATUS halRxReadBuffer(IN P_ADAPTER_T prAdapter, IN OUT P_SW_RFB_T prSwRfb);

#else
VOID halRxSDIOEnhanceReceiveRFBs(IN P_ADAPTER_T prAdapter);

WLAN_STATUS halRxEnhanceReadBuffer(IN P_ADAPTER_T prAdapter, IN UINT_32 u4DataPort,
	IN UINT_16 u2RxLength, IN OUT P_SW_RFB_T prSwRfb);
#endif /* CFG_SDIO_INTR_ENHANCE */

#if CFG_SDIO_RX_AGG
VOID halRxSDIOAggReceiveRFBs(IN P_ADAPTER_T prAdapter);
#endif

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#endif /* _HIF_H */
