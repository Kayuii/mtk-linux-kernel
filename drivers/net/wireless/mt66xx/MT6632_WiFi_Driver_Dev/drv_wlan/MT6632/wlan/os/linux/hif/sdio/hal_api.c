/******************************************************************************
*[File]             hif_api.c
*[Version]          v1.0
*[Revision Date]    2015-09-08
*[Author]
*[Description]
*    The program provides SDIO HIF APIs
*[Copyright]
*    Copyright (C) 2015 MediaTek Incorporation. All Rights Reserved.
******************************************************************************/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "precomp.h"

#if MTK_WCN_HIF_SDIO
#include "hif_sdio.h"
#else
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>	/* sdio_readl(), etc */
#include <linux/mmc/sdio_ids.h>
#endif

#include <linux/mm.h>
#ifndef CONFIG_X86
#include <asm/memory.h>
#endif

#if defined(MT6632)
#include "mt6632_reg.h"
#endif

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define RX_RESPONSE_TIMEOUT (15000)

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

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

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* @brief Verify the CHIP ID
*
* @param prAdapter      a pointer to adapter private data structure.
*
*
* @retval TRUE          CHIP ID is the same as the setting compiled
* @retval FALSE         CHIP ID is different from the setting compiled
*/
/*----------------------------------------------------------------------------*/
BOOL halVerifyChipID(IN P_ADAPTER_T prAdapter)
{
	INT_32 i = 0;
	UINT_32 u4CIR = 0;
	P_MTK_WIFI_CHIP_INFO_T prChipInfo;

	ASSERT(prAdapter);

	if (prAdapter->fgIsReadRevID)
		return TRUE;

	HAL_MCR_RD(prAdapter, MCR_WCIR, &u4CIR);

	DBGLOG(INIT, TRACE, "Chip ID: 0x%lx\n", u4CIR & WCIR_CHIP_ID);
	DBGLOG(INIT, TRACE, "Revision ID: 0x%lx\n", ((u4CIR & WCIR_REVISION_ID) >> 16));

	prAdapter->eChipIdx = MTK_WIFI_CHIP_MAX;
	prChipInfo = g_prMTKWifiChipInfo;
	for (i = 0; i < MTK_WIFI_CHIP_MAX; i++) {
		if ((u4CIR & WCIR_CHIP_ID) == prChipInfo->chip_id) {
			prAdapter->eChipIdx = i;
			break;
		}
		prChipInfo++;
	}

	if (i >= MTK_WIFI_CHIP_MAX)
		return FALSE;

	prAdapter->ucRevID = (UINT_8) (((u4CIR & WCIR_REVISION_ID) >> 16) & 0xF);
	prAdapter->fgIsReadRevID = TRUE;

	return TRUE;
}

WLAN_STATUS
halRxWaitResponse(IN P_ADAPTER_T prAdapter, IN UINT_8 ucPortIdx, OUT PUINT_8 pucRspBuffer,
		  IN UINT_32 u4MaxRespBufferLen, OUT PUINT_32 pu4Length)
{
	UINT_32 u4Value = 0, u4PktLen = 0, i = 0;
	WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;
	UINT_32 u4Time, u4Current;
	P_RX_CTRL_T prRxCtrl;

	DEBUGFUNC("halRxWaitResponse");

	ASSERT(prAdapter);
	ASSERT(pucRspBuffer);

	prRxCtrl = &prAdapter->rRxCtrl;

	u4Time = (UINT_32) kalGetTimeTick();

	do {
		/* Read the packet length */
		HAL_MCR_RD(prAdapter, MCR_WRPLR, &u4Value);

		if ((u4Value & 0xFFFF) != 0) {
			u4PktLen = u4Value & 0xFFFF;
			i = 0;
		} else {
			u4PktLen = (u4Value >> 16) & 0xFFFF;
			i = 1;
		}
		if (u4PktLen > u4MaxRespBufferLen)
			return WLAN_STATUS_FAILURE;

		if (u4PktLen == 0) {
			/* timeout exceeding check */
			u4Current = (UINT_32) kalGetTimeTick();

			if ((u4Current > u4Time) && ((u4Current - u4Time) > RX_RESPONSE_TIMEOUT))
				return WLAN_STATUS_FAILURE;
			else if (u4Current < u4Time && ((u4Current + (0xFFFFFFFF - u4Time)) > RX_RESPONSE_TIMEOUT))
				return WLAN_STATUS_FAILURE;

			/* Response packet is not ready */
			kalUdelay(50);
		} else {

#if (CFG_ENABLE_READ_EXTRA_4_BYTES == 1)
#if CFG_SDIO_RX_AGG
			HAL_PORT_RD(prAdapter,
				    i == 0 ? MCR_WRDR0 : MCR_WRDR1,
				    ALIGN_4(u4PktLen + 4),
				    prRxCtrl->pucRxCoalescingBufPtr, CFG_RX_COALESCING_BUFFER_SIZE);
			kalMemCopy(pucRspBuffer, prRxCtrl->pucRxCoalescingBufPtr, u4PktLen);
#else
#error "Please turn on RX coalescing"
#endif
#else
			HAL_PORT_RD(prAdapter,
				    i == 0 ? MCR_WRDR0 : MCR_WRDR1, u4PktLen, pucRspBuffer, u4MaxRespBufferLen);
#endif
			*pu4Length = u4PktLen;
			break;
		}
	} while (TRUE);

	return u4Status;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief enable global interrupt
*
* @param prAdapter pointer to the Adapter handler
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID halEnableInterrupt(IN P_ADAPTER_T prAdapter)
{
	BOOLEAN fgIsIntEnableCache;

	ASSERT(prAdapter);
	fgIsIntEnableCache = prAdapter->fgIsIntEnable;

	prAdapter->fgIsIntEnable = TRUE;	/* NOTE(Kevin): It must be placed before MCR GINT write. */

	/* If need enable INT and also set LPOwn at the same time. */
	if (prAdapter->fgIsIntEnableWithLPOwnSet) {
		prAdapter->fgIsIntEnableWithLPOwnSet = FALSE;	/* NOTE(Kevin): It's better to place it
								 * before MCR GINT write.
								 */
		/* If INT was enabled, only set LPOwn */
		if (fgIsIntEnableCache) {
			HAL_MCR_WR(prAdapter, MCR_WHLPCR, WHLPCR_FW_OWN_REQ_SET);
			prAdapter->fgIsFwOwn = TRUE;
		}
		/* If INT was not enabled, enable it and also set LPOwn now */
		else {
			HAL_MCR_WR(prAdapter, MCR_WHLPCR, WHLPCR_FW_OWN_REQ_SET | WHLPCR_INT_EN_SET);
			prAdapter->fgIsFwOwn = TRUE;
		}
	}
	/* If INT was not enabled, enable it now */
	else if (!fgIsIntEnableCache)
		HAL_BYTE_WR(prAdapter, MCR_WHLPCR, WHLPCR_INT_EN_SET);
}				/* end of nicEnableInterrupt() */


/*----------------------------------------------------------------------------*/
/*!
* @brief disable global interrupt
*
* @param prAdapter pointer to the Adapter handler
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID halDisableInterrupt(IN P_ADAPTER_T prAdapter)
{

	ASSERT(prAdapter);

	HAL_BYTE_WR(prAdapter, MCR_WHLPCR, WHLPCR_INT_EN_CLR);

	prAdapter->fgIsIntEnable = FALSE;

}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to process the POWER OFF procedure.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
BOOLEAN halSetDriverOwn(IN P_ADAPTER_T prAdapter)
{
	BOOLEAN fgStatus = TRUE;
	UINT_32 i, u4CurrTick = 0;
	BOOLEAN fgTimeout;
	BOOLEAN fgResult;

	ASSERT(prAdapter);

	GLUE_INC_REF_CNT(prAdapter->u4PwrCtrlBlockCnt);

	if (prAdapter->fgIsFwOwn == FALSE)
		return fgStatus;

	DBGLOG(INIT, INFO, "DRIVER OWN\n");

	u4CurrTick = kalGetTimeTick();
	i = 0;
	while (1) {
		HAL_LP_OWN_RD(prAdapter, &fgResult);

		fgTimeout = ((kalGetTimeTick() - u4CurrTick) > LP_OWN_BACK_TOTAL_DELAY_MS) ? TRUE : FALSE;

		if (fgResult) {
			prAdapter->fgIsFwOwn = FALSE;
			prAdapter->u4OwnFailedCount = 0;
			prAdapter->u4OwnFailedLogCount = 0;
			break;
		} else if ((i > LP_OWN_BACK_FAILED_RETRY_CNT) &&
			   (kalIsCardRemoved(prAdapter->prGlueInfo) || fgIsBusAccessFailed || fgTimeout
			    || wlanIsChipNoAck(prAdapter))) {

			if ((prAdapter->u4OwnFailedCount == 0) ||
			    CHECK_FOR_TIMEOUT(u4CurrTick, prAdapter->rLastOwnFailedLogTime,
					      MSEC_TO_SYSTIME(LP_OWN_BACK_FAILED_LOG_SKIP_MS))) {

				DBGLOG(INIT, ERROR,
				       "LP cannot be own back, Timeout[%u](%ums), BusAccessError[%u]",
				       fgTimeout, kalGetTimeTick() - u4CurrTick, fgIsBusAccessFailed);
				DBGLOG(INIT, ERROR,
				       "Resetting[%u], CardRemoved[%u] NoAck[%u] Cnt[%u]\n",
				       kalIsResetting(),
				       kalIsCardRemoved(prAdapter->prGlueInfo), wlanIsChipNoAck(prAdapter),
				       prAdapter->u4OwnFailedCount);

				DBGLOG(INIT, INFO,
				       "Skip LP own back failed log for next %ums\n", LP_OWN_BACK_FAILED_LOG_SKIP_MS);

				prAdapter->u4OwnFailedLogCount++;
				if (prAdapter->u4OwnFailedLogCount > LP_OWN_BACK_FAILED_RESET_CNT) {
					/* Trigger RESET */
#if CFG_CHIP_RESET_SUPPORT
					glResetTrigger(prAdapter);
#endif
				}
				GET_CURRENT_SYSTIME(&prAdapter->rLastOwnFailedLogTime);
			}

			prAdapter->u4OwnFailedCount++;
			fgStatus = FALSE;
			break;
		}

		if ((i & (LP_OWN_BACK_CLR_OWN_ITERATION - 1)) == 0) {
			/* Software get LP ownership - per 256 iterations */
			HAL_LP_OWN_CLR(prAdapter, &fgResult);
		}

		/* Delay for LP engine to complete its operation. */
		kalMsleep(LP_OWN_BACK_LOOP_DELAY_MS);
		i++;
	}

	return fgStatus;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to process the POWER ON procedure.
*
* \param[in] pvAdapter Pointer to the Adapter structure.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID halSetFWOwn(IN P_ADAPTER_T prAdapter, IN BOOLEAN fgEnableGlobalInt)
{

	BOOLEAN fgResult;

	ASSERT(prAdapter);

	ASSERT(prAdapter->u4PwrCtrlBlockCnt != 0);
	/* Decrease Block to Enter Low Power Semaphore count */
	GLUE_DEC_REF_CNT(prAdapter->u4PwrCtrlBlockCnt);
	if (!(prAdapter->fgWiFiInSleepyState && (prAdapter->u4PwrCtrlBlockCnt == 0)))
		return;

	if (prAdapter->fgIsFwOwn == TRUE)
		return;

	if (nicProcessIST(prAdapter) != WLAN_STATUS_NOT_INDICATING) {
		DBGLOG(INIT, STATE, "FW OWN Failed due to pending INT\n");
		/* pending interrupts */
		return;
	}

	if (fgEnableGlobalInt) {
		prAdapter->fgIsIntEnableWithLPOwnSet = TRUE;
	} else {
		HAL_LP_OWN_SET(prAdapter, &fgResult);

		if (fgResult) {
			/* if set firmware own not successful (possibly pending interrupts), */
			/* indicate an own clear event */
			HAL_LP_OWN_CLR(prAdapter, &fgResult);

			return;
		}

		prAdapter->fgIsFwOwn = TRUE;

		DBGLOG(INIT, INFO, "FW OWN\n");
	}
}

VOID halWakeUpWiFi(IN P_ADAPTER_T prAdapter)
{

	BOOLEAN fgResult;

	ASSERT(prAdapter);

	HAL_LP_OWN_RD(prAdapter, &fgResult);

	if (fgResult)
		prAdapter->fgIsFwOwn = FALSE;
	else
		HAL_LP_OWN_CLR(prAdapter, &fgResult);
}

VOID halDevInit(IN P_ADAPTER_T prAdapter)
{
	UINT_32 u4Value = 0;

	ASSERT(prAdapter);

#if CFG_SDIO_INTR_ENHANCE
	/* 4 <1> Check STATUS Buffer is DW alignment. */
	ASSERT(IS_ALIGN_4((ULONG) & prAdapter->prSDIOCtrl->u4WHISR));

	/* 4 <2> Setup STATUS count. */
	{
		HAL_MCR_RD(prAdapter, MCR_WHCR, &u4Value);

		/* 4 <2.1> Setup the number of maximum RX length to be report */
		u4Value &= ~(WHCR_MAX_HIF_RX_LEN_NUM);
		u4Value |= ((SDIO_MAXIMUM_RX_LEN_NUM << WHCR_OFFSET_MAX_HIF_RX_LEN_NUM));

		/* 4 <2.2> Setup RX enhancement mode */
#if CFG_SDIO_RX_ENHANCE
		u4Value |= WHCR_RX_ENHANCE_MODE_EN;
#else
		u4Value &= ~WHCR_RX_ENHANCE_MODE_EN;
#endif /* CFG_SDIO_RX_AGG */

		HAL_MCR_WR(prAdapter, MCR_WHCR, u4Value);
	}
#endif /* CFG_SDIO_INTR_ENHANCE */

	HAL_MCR_WR(prAdapter, MCR_WHIER, WHIER_DEFAULT);
}

VOID halTxCancelSendingCmd(IN P_ADAPTER_T prAdapter, IN P_CMD_INFO_T prCmdInfo)
{
}

BOOLEAN halTxIsDataBufEnough(IN P_ADAPTER_T prAdapter, IN UINT_8 ucTc, IN UINT_32 u4Length)
{
	return TRUE;
}

BOOLEAN halTxReleaseResource(IN P_ADAPTER_T prAdapter, IN UINT_16 * au2TxRlsCnt)
{
	P_TX_TCQ_STATUS_T prTcqStatus;
	BOOLEAN bStatus = FALSE;
	UINT_32 i;
	/* UINT_32 u4BufferCountToBeFreed;
	UINT_16 au2FreeTcResource[TC_NUM] = { 0 }; */

	KAL_SPIN_LOCK_DECLARATION();

	ASSERT(prAdapter);
	prTcqStatus = &prAdapter->rTxCtrl.rTc;
#if 0
	/* Calculate free page count */
	if (nicTxCalculateResource(prAdapter, au2TxRlsCnt, au2FreeTcResource)) {

		/* Return free Tc page count */
		KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_RESOURCE);
		for (i = TC0_INDEX; i < TC_NUM; i++) {

			/* Real page counter */
			prTcqStatus->au4FreePageCount[i] += au2FreeTcResource[i];

			/* Buffer counter. For development only */
			/* Convert page count to buffer count */
			u4BufferCountToBeFreed = (prTcqStatus->au4FreePageCount[i] / NIC_TX_MAX_PAGE_PER_FRAME);
			prTcqStatus->au4FreeBufferCount[i] = u4BufferCountToBeFreed;

			if (au2FreeTcResource[i]) {
				DBGLOG(TX, EVENT,
				       "Release: TC%lu ReturnPageCnt[%u] FreePageCnt[%u] FreeBufferCnt[%u]\n",
				       i, au2FreeTcResource[i], prTcqStatus->au4FreePageCount[i],
				       prTcqStatus->au4FreeBufferCount[i]);
			}
		}
		KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_RESOURCE);
		bStatus = TRUE;
	}
#else
	/* Return free Tc page count */
	KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_RESOURCE);
	for (i = TC0_INDEX; i < TC5_INDEX; i++)
		nicTxReleaseResource(prAdapter, i, au2TxRlsCnt[nicTxGetTxQByTc(prAdapter, i)], FALSE);
	KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_TX_RESOURCE);
	bStatus = TRUE;
#endif

	if (!nicTxSanityCheckResource(prAdapter))
		DBGLOG(TX, ERROR, "Tx Done INT result, FFA[%u] AC[%u:%u:%u:%u:%u] CPU[%u]\n",
		       au2TxRlsCnt[HIF_TX_FFA_INDEX], au2TxRlsCnt[HIF_TX_AC0_INDEX],
		       au2TxRlsCnt[HIF_TX_AC1_INDEX], au2TxRlsCnt[HIF_TX_AC2_INDEX],
		       au2TxRlsCnt[HIF_TX_AC3_INDEX], au2TxRlsCnt[HIF_TX_AC4_INDEX], au2TxRlsCnt[HIF_TX_CPU_INDEX]);

	DBGLOG(TX, LOUD,
	       "TCQ Status Free Page:Buf[%03u:%02u, %03u:%02u, %03u:%02u, %03u:%02u, %03u:%02u, %03u:%02u]\n",
	       prTcqStatus->au4FreePageCount[TC0_INDEX],
	       prTcqStatus->au4FreeBufferCount[TC0_INDEX],
	       prTcqStatus->au4FreePageCount[TC1_INDEX],
	       prTcqStatus->au4FreeBufferCount[TC1_INDEX],
	       prTcqStatus->au4FreePageCount[TC2_INDEX],
	       prTcqStatus->au4FreeBufferCount[TC2_INDEX],
	       prTcqStatus->au4FreePageCount[TC3_INDEX],
	       prTcqStatus->au4FreeBufferCount[TC3_INDEX],
	       prTcqStatus->au4FreePageCount[TC4_INDEX],
	       prTcqStatus->au4FreeBufferCount[TC4_INDEX],
	       prTcqStatus->au4FreePageCount[TC5_INDEX], prTcqStatus->au4FreeBufferCount[TC5_INDEX]);

	return bStatus;
}

WLAN_STATUS halTxPollingResource(IN P_ADAPTER_T prAdapter, IN UINT_8 ucTC)
{
	P_TX_CTRL_T prTxCtrl;
	WLAN_STATUS u4Status = WLAN_STATUS_RESOURCES;
	UINT_32 au4WTSR[8];

	prTxCtrl = &prAdapter->rTxCtrl;

	HAL_READ_TX_RELEASED_COUNT(prAdapter, au4WTSR);

	if (kalIsCardRemoved(prAdapter->prGlueInfo) == TRUE || fgIsBusAccessFailed == TRUE) {
		u4Status = WLAN_STATUS_FAILURE;
	} else if (halTxReleaseResource(prAdapter, (PUINT_16) au4WTSR)) {
		if (prTxCtrl->rTc.au4FreeBufferCount[ucTC] > 0)
			u4Status = WLAN_STATUS_SUCCESS;
	}

	return u4Status;
}

VOID halProcessTxInterrupt(IN P_ADAPTER_T prAdapter)
{
	P_TX_CTRL_T prTxCtrl;
#if CFG_SDIO_INTR_ENHANCE
	P_SDIO_CTRL_T prSDIOCtrl;
#else
	UINT_32 au4TxCount[2];
#endif /* CFG_SDIO_INTR_ENHANCE */

	ASSERT(prAdapter);

	prTxCtrl = &prAdapter->rTxCtrl;
	ASSERT(prTxCtrl);

	/* Get the TX STATUS */
#if CFG_SDIO_INTR_ENHANCE

	prSDIOCtrl = prAdapter->prSDIOCtrl;
#if DBG
	/* dumpMemory8((PUINT_8)prSDIOCtrl, sizeof(SDIO_CTRL_T)); */
#endif

	nicTxInterruptSanityCheck(prAdapter, (PUINT_16) & prSDIOCtrl->rTxInfo);
	halTxReleaseResource(prAdapter, (PUINT_16) & prSDIOCtrl->rTxInfo);
	kalMemZero(&prSDIOCtrl->rTxInfo, sizeof(prSDIOCtrl->rTxInfo));

#else

	HAL_MCR_RD(prAdapter, MCR_WTSR0, &au4TxCount[0]);
	HAL_MCR_RD(prAdapter, MCR_WTSR1, &au4TxCount[1]);
	DBGLOG(EMU, TRACE, "MCR_WTSR0: 0x%x, MCR_WTSR1: 0x%x\n", au4TxCount[0], au4TxCount[1]);

	halTxReleaseResource(prAdapter, (PUINT_8) au4TxCount);

#endif /* CFG_SDIO_INTR_ENHANCE */
#if 0				/* T-put */
	nicTxAdjustTcq(prAdapter);
#endif
}				/* end of nicProcessTxInterrupt() */

#if !CFG_SDIO_INTR_ENHANCE
/*----------------------------------------------------------------------------*/
/*!
* @brief Read the rx data from data port and setup RFB
*
* @param prAdapter pointer to the Adapter handler
* @param prSWRfb the RFB to receive rx data
*
* @retval WLAN_STATUS_SUCCESS: SUCCESS
* @retval WLAN_STATUS_FAILURE: FAILURE
*
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS halRxReadBuffer(IN P_ADAPTER_T prAdapter, IN OUT P_SW_RFB_T prSwRfb)
{
	P_RX_CTRL_T prRxCtrl;
	PUINT_8 pucBuf;
	P_HW_MAC_RX_DESC_T prRxStatus;
	UINT_32 u4PktLen = 0, u4ReadBytes;
	WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;
	BOOL fgResult = TRUE;
	UINT_32 u4RegValue;
	UINT_32 rxNum;

	DEBUGFUNC("halRxReadBuffer");

	ASSERT(prAdapter);
	ASSERT(prSwRfb);

	prRxCtrl = &prAdapter->rRxCtrl;
	ASSERT(prRxCtrl);

	pucBuf = prSwRfb->pucRecvBuff;
	prRxStatus = prSwRfb->prRxStatus;

	ASSERT(prRxStatus);
	ASSERT(pucBuf);
	DBGLOG(RX, TRACE, "pucBuf= 0x%x, prRxStatus= 0x%x\n", pucBuf, prRxStatus);

	do {
		/* Read the RFB DW length and packet length */
		HAL_MCR_RD(prAdapter, MCR_WRPLR, &u4RegValue);
		if (!fgResult) {
			DBGLOG(RX, ERROR, "Read RX Packet Lentgh Error\n");
			return WLAN_STATUS_FAILURE;
		}
		/* 20091021 move the line to get the HIF RX header (for RX0/1) */
		if (u4RegValue == 0) {
			DBGLOG(RX, ERROR, "No RX packet\n");
			return WLAN_STATUS_FAILURE;
		}

		u4PktLen = u4RegValue & BITS(0, 15);
		if (u4PktLen != 0) {
			rxNum = 0;
		} else {
			rxNum = 1;
			u4PktLen = (u4RegValue & BITS(16, 31)) >> 16;
		}

		DBGLOG(RX, TRACE, "RX%d: u4PktLen = %d\n", rxNum, u4PktLen);

		/* 4 <4> Read Entire RFB and packet, include HW appended DW (Checksum Status) */
		u4ReadBytes = ALIGN_4(u4PktLen) + 4;
		HAL_READ_RX_PORT(prAdapter, rxNum, u4ReadBytes, pucBuf, CFG_RX_MAX_PKT_SIZE);

		/* 20091021 move the line to get the HIF RX header */
		/* u4PktLen = (UINT_32)prHifRxHdr->u2PacketLen; */
		if (u4PktLen != (UINT_32) HAL_RX_STATUS_GET_RX_BYTE_CNT(prRxStatus)) {
			DBGLOG(RX, ERROR, "Read u4PktLen = %d, prHifRxHdr->u2PacketLen: %d\n",
			       u4PktLen, HAL_RX_STATUS_GET_RX_BYTE_CNT(prRxStatus));
#if DBG
			dumpMemory8((PUINT_8) prRxStatus,
				    (HAL_RX_STATUS_GET_RX_BYTE_CNT(prRxStatus) >
				     4096) ? 4096 : prRxStatus->u2RxByteCount);
#endif
			ASSERT(0);
		}
		/* u4PktLen is byte unit, not inlude HW appended DW */

		prSwRfb->ucPacketType = (UINT_8) HAL_RX_STATUS_GET_PKT_TYPE(prRxStatus);
		DBGLOG(RX, TRACE, "ucPacketType = %d\n", prSwRfb->ucPacketType);

		prSwRfb->ucStaRecIdx =
		    secGetStaIdxByWlanIdx(prAdapter, (UINT_8) HAL_RX_STATUS_GET_WLAN_IDX(prRxStatus));

		/* fgResult will be updated in MACRO */
		if (!fgResult)
			return WLAN_STATUS_FAILURE;

		DBGLOG(RX, TRACE, "Dump RX buffer, length = 0x%x\n", u4ReadBytes);
		DBGLOG_MEM8(RX, TRACE, pucBuf, u4ReadBytes);
	} while (FALSE);

	return u4Status;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief Read frames from the data port, fill RFB
*        and put each frame into the rReceivedRFBList queue.
*
* @param prAdapter   Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID halRxSDIOReceiveRFBs(IN P_ADAPTER_T prAdapter)
{
	P_RX_CTRL_T prRxCtrl;
	P_SW_RFB_T prSwRfb = (P_SW_RFB_T) NULL;
	P_HW_MAC_RX_DESC_T prRxStatus;
	UINT_32 u4HwAppendDW;
	PUINT_32 pu4Temp;

	KAL_SPIN_LOCK_DECLARATION();

	DEBUGFUNC("halRxSDIOReceiveRFBs");

	ASSERT(prAdapter);

	prRxCtrl = &prAdapter->rRxCtrl;
	ASSERT(prRxCtrl);

	do {
		KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_FREE_QUE);
		QUEUE_REMOVE_HEAD(&prRxCtrl->rFreeSwRfbList, prSwRfb, P_SW_RFB_T);
		KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_FREE_QUE);

		if (!prSwRfb) {
			DBGLOG(RX, TRACE, "No More RFB\n");
			break;
		}
		/* need to consider */
		if (halRxReadBuffer(prAdapter, prSwRfb) == WLAN_STATUS_FAILURE) {
			DBGLOG(RX, TRACE, "halRxFillRFB failed\n");
			nicRxReturnRFB(prAdapter, prSwRfb);
			break;
		}

		KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
		QUEUE_INSERT_TAIL(&prRxCtrl->rReceivedRfbList, &prSwRfb->rQueEntry);
		RX_INC_CNT(prRxCtrl, RX_MPDU_TOTAL_COUNT);
		KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);

		prRxStatus = prSwRfb->prRxStatus;
		ASSERT(prRxStatus);

		pu4Temp = (PUINT_32) prRxStatus;
		u4HwAppendDW = *(pu4Temp + (ALIGN_4(prRxStatus->u2RxByteCount) >> 2));
		DBGLOG(RX, TRACE, "u4HwAppendDW = 0x%x\n", u4HwAppendDW);
		DBGLOG(RX, TRACE, "u2PacketLen = 0x%x\n", HAL_RX_STATUS_GET_RX_BYTE_CNT(prRxStatus));
	} while (FALSE);

}				/* end of nicReceiveRFBs() */

#else
/*----------------------------------------------------------------------------*/
/*!
* @brief Read frames from the data port, fill RFB
*        and put each frame into the rReceivedRFBList queue.
*
* @param prAdapter      Pointer to the Adapter structure.
* @param u4DataPort     Specify which port to read
* @param u2RxLength     Specify to the the rx packet length in Byte.
* @param prSwRfb        the RFB to receive rx data.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/

WLAN_STATUS
halRxEnhanceReadBuffer(IN P_ADAPTER_T prAdapter,
		       IN UINT_32 u4DataPort, IN UINT_16 u2RxLength, IN OUT P_SW_RFB_T prSwRfb)
{
	P_RX_CTRL_T prRxCtrl;
	PUINT_8 pucBuf;
	P_HW_MAC_RX_DESC_T prRxStatus;
	UINT_32 u4PktLen = 0;
	WLAN_STATUS u4Status = WLAN_STATUS_FAILURE;
	BOOL fgResult = TRUE;

	DEBUGFUNC("halRxEnhanceReadBuffer");

	ASSERT(prAdapter);
	ASSERT(prSwRfb);

	prRxCtrl = &prAdapter->rRxCtrl;
	ASSERT(prRxCtrl);

	pucBuf = prSwRfb->pucRecvBuff;
	ASSERT(pucBuf);

	prRxStatus = prSwRfb->prRxStatus;
	ASSERT(prRxStatus);

	/* DBGLOG(RX, TRACE, ("u2RxLength = %d\n", u2RxLength)); */

	do {
		/* 4 <1> Read RFB frame from MCR_WRDR0, include HW appended DW */
		HAL_READ_RX_PORT(prAdapter,
				 u4DataPort, ALIGN_4(u2RxLength + HIF_RX_HW_APPENDED_LEN), pucBuf, CFG_RX_MAX_PKT_SIZE);

		if (!fgResult) {
			DBGLOG(RX, ERROR, "Read RX Packet Lentgh Error\n");
			break;
		}

		u4PktLen = (UINT_32) (HAL_RX_STATUS_GET_RX_BYTE_CNT(prRxStatus));
		/* DBGLOG(RX, TRACE, ("u4PktLen = %d\n", u4PktLen)); */

		prSwRfb->ucPacketType = (UINT_8) HAL_RX_STATUS_GET_PKT_TYPE(prRxStatus);
		/* DBGLOG(RX, TRACE, ("ucPacketType = %d\n", prSwRfb->ucPacketType)); */

		prSwRfb->ucStaRecIdx =
		    secGetStaIdxByWlanIdx(prAdapter, (UINT_8) HAL_RX_STATUS_GET_WLAN_IDX(prRxStatus));

		/* 4 <2> if the RFB dw size or packet size is zero */
		if (u4PktLen == 0) {
			DBGLOG(RX, ERROR, "Packet Length = %lu\n", u4PktLen);
			ASSERT(0);
			break;
		}
		/* 4 <3> if the packet is too large or too small */
		/* ToDo[6630]: adjust CFG_RX_MAX_PKT_SIZE */
		if (u4PktLen > CFG_RX_MAX_PKT_SIZE) {
			DBGLOG(RX, TRACE, "Read RX Packet Lentgh Error (%lu)\n", u4PktLen);
			ASSERT(0);
			break;
		}

		u4Status = WLAN_STATUS_SUCCESS;
	} while (FALSE);

	DBGLOG_MEM8(RX, TRACE, pucBuf, ALIGN_4(u2RxLength + HIF_RX_HW_APPENDED_LEN));
	return u4Status;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief Read frames from the data port for SDIO
*        I/F, fill RFB and put each frame into the rReceivedRFBList queue.
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID halRxSDIOEnhanceReceiveRFBs(IN P_ADAPTER_T prAdapter)
{
	P_SDIO_CTRL_T prSDIOCtrl;
	P_RX_CTRL_T prRxCtrl;
	P_SW_RFB_T prSwRfb = (P_SW_RFB_T) NULL;
	UINT_32 i, rxNum;
	UINT_16 u2RxPktNum, u2RxLength = 0, u2Tmp = 0;

	KAL_SPIN_LOCK_DECLARATION();

	DEBUGFUNC("halRxSDIOEnhanceReceiveRFBs");

	ASSERT(prAdapter);

	prSDIOCtrl = prAdapter->prSDIOCtrl;
	ASSERT(prSDIOCtrl);

	prRxCtrl = &prAdapter->rRxCtrl;
	ASSERT(prRxCtrl);

	for (rxNum = 0; rxNum < 2; rxNum++) {
		u2RxPktNum =
		    (rxNum == 0 ? prSDIOCtrl->rRxInfo.u.u2NumValidRx0Len : prSDIOCtrl->rRxInfo.u.u2NumValidRx1Len);

		if (u2RxPktNum == 0)
			continue;

		for (i = 0; i < u2RxPktNum; i++) {
			if (rxNum == 0) {
				/* HAL_READ_RX_LENGTH */
				HAL_READ_RX_LENGTH(prAdapter, &u2RxLength, &u2Tmp);
			} else if (rxNum == 1) {
				/* HAL_READ_RX_LENGTH */
				HAL_READ_RX_LENGTH(prAdapter, &u2Tmp, &u2RxLength);
			}

			if (!u2RxLength)
				break;

			KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_FREE_QUE);
			QUEUE_REMOVE_HEAD(&prRxCtrl->rFreeSwRfbList, prSwRfb, P_SW_RFB_T);
			KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_FREE_QUE);

			if (!prSwRfb) {
				DBGLOG(RX, TRACE, "No More RFB\n");
				break;
			}
			ASSERT(prSwRfb);

			if (halRxEnhanceReadBuffer(prAdapter, rxNum, u2RxLength, prSwRfb) == WLAN_STATUS_FAILURE) {
				DBGLOG(RX, TRACE, "nicRxEnhanceRxReadBuffer failed\n");
				nicRxReturnRFB(prAdapter, prSwRfb);
				break;
			}
			/* prSDIOCtrl->au4RxLength[i] = 0; */

			KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
			QUEUE_INSERT_TAIL(&prRxCtrl->rReceivedRfbList, &prSwRfb->rQueEntry);
			RX_INC_CNT(prRxCtrl, RX_MPDU_TOTAL_COUNT);
			KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
		}
	}

	prSDIOCtrl->rRxInfo.u.u2NumValidRx0Len = 0;
	prSDIOCtrl->rRxInfo.u.u2NumValidRx1Len = 0;

}				/* end of nicRxSDIOReceiveRFBs() */

#endif /* CFG_SDIO_INTR_ENHANCE */

#if CFG_SDIO_RX_AGG
/*----------------------------------------------------------------------------*/
/*!
* @brief Read frames from the data port for SDIO with Rx aggregation enabled
*        I/F, fill RFB and put each frame into the rReceivedRFBList queue.
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID halRxSDIOAggReceiveRFBs(IN P_ADAPTER_T prAdapter)
{
	P_ENHANCE_MODE_DATA_STRUCT_T prEnhDataStr;
	P_RX_CTRL_T prRxCtrl;
	P_SDIO_CTRL_T prSDIOCtrl;
	P_SW_RFB_T prSwRfb = (P_SW_RFB_T) NULL;
	UINT_32 u4RxLength;
	UINT_32 i, rxNum;
	UINT_32 u4RxAggCount = 0, u4RxAggLength = 0;
	UINT_32 u4RxAvailAggLen, u4CurrAvailFreeRfbCnt;
	PUINT_8 pucSrcAddr;
	P_HW_MAC_RX_DESC_T prRxStatus;
	BOOL fgResult = TRUE;
	BOOLEAN fgIsRxEnhanceMode;
	UINT_16 u2RxPktNum;
#if CFG_SDIO_RX_ENHANCE
	UINT_32 u4MaxLoopCount = CFG_MAX_RX_ENHANCE_LOOP_COUNT;
#endif

	KAL_SPIN_LOCK_DECLARATION();

	DEBUGFUNC("halRxSDIOAggReceiveRFBs");

	ASSERT(prAdapter);
	prEnhDataStr = prAdapter->prSDIOCtrl;
	prRxCtrl = &prAdapter->rRxCtrl;
	prSDIOCtrl = prAdapter->prSDIOCtrl;

#if CFG_SDIO_RX_ENHANCE
	fgIsRxEnhanceMode = TRUE;
#else
	fgIsRxEnhanceMode = FALSE;
#endif

	do {
#if CFG_SDIO_RX_ENHANCE
		/* to limit maximum loop for RX */
		u4MaxLoopCount--;
		if (u4MaxLoopCount == 0)
			break;
#endif

		if (prEnhDataStr->rRxInfo.u.u2NumValidRx0Len == 0 && prEnhDataStr->rRxInfo.u.u2NumValidRx1Len == 0)
			break;

		for (rxNum = 0; rxNum < 2; rxNum++) {
			u2RxPktNum =
			    (rxNum ==
			     0 ? prEnhDataStr->rRxInfo.u.u2NumValidRx0Len : prEnhDataStr->rRxInfo.u.u2NumValidRx1Len);

			/* if this assertion happened, it is most likely a F/W bug */
			ASSERT(u2RxPktNum <= 16);

			if (u2RxPktNum > 16)
				continue;

			if (u2RxPktNum == 0)
				continue;

#if CFG_HIF_STATISTICS
			prRxCtrl->u4TotalRxAccessNum++;
			prRxCtrl->u4TotalRxPacketNum += u2RxPktNum;
#endif

			u4CurrAvailFreeRfbCnt = prRxCtrl->rFreeSwRfbList.u4NumElem;

			/* if SwRfb is not enough, abort reading this time */
			if (u4CurrAvailFreeRfbCnt < u2RxPktNum) {
#if CFG_HIF_RX_STARVATION_WARNING
				DbgPrint("FreeRfb is not enough: %d available, need %d\n",
					 u4CurrAvailFreeRfbCnt, u2RxPktNum);
				DbgPrint("Queued Count: %d / Dequeud Count: %d\n",
					 prRxCtrl->u4QueuedCnt, prRxCtrl->u4DequeuedCnt);
#endif
				continue;
			}
#if CFG_SDIO_RX_ENHANCE
			u4RxAvailAggLen =
			    CFG_RX_COALESCING_BUFFER_SIZE - (sizeof(ENHANCE_MODE_DATA_STRUCT_T) +
							     4 /* extra HW padding */);
#else
			u4RxAvailAggLen = CFG_RX_COALESCING_BUFFER_SIZE;
#endif
			u4RxAggCount = 0;

			for (i = 0; i < u2RxPktNum; i++) {
				u4RxLength = (rxNum == 0 ?
					      (UINT_32) prEnhDataStr->rRxInfo.u.au2Rx0Len[i] :
					      (UINT_32) prEnhDataStr->rRxInfo.u.au2Rx1Len[i]);

				if (!u4RxLength) {
					ASSERT(0);
					break;
				}

				if (ALIGN_4(u4RxLength + HIF_RX_HW_APPENDED_LEN) < u4RxAvailAggLen) {
					if (u4RxAggCount < u4CurrAvailFreeRfbCnt) {
						u4RxAvailAggLen -= ALIGN_4(u4RxLength + HIF_RX_HW_APPENDED_LEN);
						u4RxAggCount++;
					} else {
						/* no FreeSwRfb for rx packet */
						DBGLOG(RX, ERROR,
						       "[%s] RxAggCount(%d) is greater than AvailableFreeCount(%d)\n",
						       __func__, u4RxAggCount, u4CurrAvailFreeRfbCnt);

						ASSERT(0);
						break;
					}
				} else {
					/* CFG_RX_COALESCING_BUFFER_SIZE is not large enough */
					DBGLOG(RX, ERROR,
					       "[%s] Request_len(%d) is greater than Available_len(%d)\n",
					       __func__,
					       (ALIGN_4(u4RxLength + HIF_RX_HW_APPENDED_LEN)), u4RxAvailAggLen);
					ASSERT(0);
					break;
				}
			}

			u4RxAggLength = (CFG_RX_COALESCING_BUFFER_SIZE - u4RxAvailAggLen);
			/* DBGLOG(RX, INFO, ("u4RxAggCount = %d, u4RxAggLength = %d\n", */
			/* u4RxAggCount, u4RxAggLength)); */

			HAL_READ_RX_PORT(prAdapter,
					 rxNum,
					 u4RxAggLength, prRxCtrl->pucRxCoalescingBufPtr, CFG_RX_COALESCING_BUFFER_SIZE);
			if (!fgResult) {
				DBGLOG(RX, ERROR, "Read RX Agg Packet Error\n");
				continue;
			}

			pucSrcAddr = prRxCtrl->pucRxCoalescingBufPtr;
			for (i = 0; i < u4RxAggCount; i++) {
				UINT_16 u2PktLength;

				u2PktLength = (rxNum == 0 ?
					       prEnhDataStr->rRxInfo.u.au2Rx0Len[i] :
					       prEnhDataStr->rRxInfo.u.au2Rx1Len[i]);

				if (ALIGN_4(u2PktLength + HIF_RX_HW_APPENDED_LEN) > CFG_RX_MAX_PKT_SIZE) {
					DBGLOG(RX, ERROR,
					       "[%s] Request_len(%d) is greater than CFG_RX_MAX_PKT_SIZE(%d)...",
					       __func__, (ALIGN_4(u2PktLength + HIF_RX_HW_APPENDED_LEN)),
					       CFG_RX_MAX_PKT_SIZE);
					DBGLOG(RX, ERROR, "Drop the unexpected packet...\n");
					DBGLOG_MEM32(RX, ERROR, pucSrcAddr,
						     ALIGN_4(u2PktLength + HIF_RX_HW_APPENDED_LEN));

					pucSrcAddr += ALIGN_4(u2PktLength + HIF_RX_HW_APPENDED_LEN);
					RX_INC_CNT(prRxCtrl, RX_DROP_TOTAL_COUNT);
					continue;
				}

				KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_FREE_QUE);
				QUEUE_REMOVE_HEAD(&prRxCtrl->rFreeSwRfbList, prSwRfb, P_SW_RFB_T);
				KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_FREE_QUE);

				ASSERT(prSwRfb);
				kalMemCopy(prSwRfb->pucRecvBuff, pucSrcAddr,
					   ALIGN_4(u2PktLength + HIF_RX_HW_APPENDED_LEN));

				/* prHifRxHdr = prSwRfb->prHifRxHdr; */
				/* ASSERT(prHifRxHdr); */

				prRxStatus = prSwRfb->prRxStatus;
				ASSERT(prRxStatus);

				prSwRfb->ucPacketType = (UINT_8) HAL_RX_STATUS_GET_PKT_TYPE(prRxStatus);
				/* DBGLOG(RX, TRACE, ("ucPacketType = %d\n", prSwRfb->ucPacketType)); */
#if DBG
				DBGLOG(RX, TRACE,
				       "Rx status flag = %x wlan index = %d SecMode = %d\n",
				       prRxStatus->u2StatusFlag, prRxStatus->ucWlanIdx,
				       HAL_RX_STATUS_GET_SEC_MODE(prRxStatus));
#endif

				KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
				QUEUE_INSERT_TAIL(&prRxCtrl->rReceivedRfbList, &prSwRfb->rQueEntry);
				RX_INC_CNT(prRxCtrl, RX_MPDU_TOTAL_COUNT);
				KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);

				pucSrcAddr += ALIGN_4(u2PktLength + HIF_RX_HW_APPENDED_LEN);
				/* prEnhDataStr->au4RxLength[i] = 0; */
			}

#if CFG_SDIO_RX_ENHANCE
			kalMemCopy(prAdapter->prSDIOCtrl, (pucSrcAddr + 4), sizeof(ENHANCE_MODE_DATA_STRUCT_T));

			/* do the same thing what nicSDIOReadIntStatus() does */
			if ((prSDIOCtrl->u4WHISR & WHISR_TX_DONE_INT) == 0 &&
			    (prSDIOCtrl->rTxInfo.au4WTSR[0] | prSDIOCtrl->rTxInfo.au4WTSR[1])) {
				prSDIOCtrl->u4WHISR |= WHISR_TX_DONE_INT;
			}

			if ((prSDIOCtrl->u4WHISR & BIT(31)) == 0 &&
			    HAL_GET_MAILBOX_READ_CLEAR(prAdapter) == TRUE &&
			    (prSDIOCtrl->u4RcvMailbox0 != 0 || prSDIOCtrl->u4RcvMailbox1 != 0)) {
				prSDIOCtrl->u4WHISR |= BIT(31);
			}

			/* dispatch to interrupt handler with RX bits masked */
			nicProcessIST_impl(prAdapter,
					   prSDIOCtrl->u4WHISR & (~(WHISR_RX0_DONE_INT | WHISR_RX1_DONE_INT)));
#endif
		}

#if !CFG_SDIO_RX_ENHANCE
		prEnhDataStr->rRxInfo.u.u2NumValidRx0Len = 0;
		prEnhDataStr->rRxInfo.u.u2NumValidRx1Len = 0;
#endif
	} while ((prEnhDataStr->rRxInfo.u.u2NumValidRx0Len || prEnhDataStr->rRxInfo.u.u2NumValidRx1Len)
		 && fgIsRxEnhanceMode);

}
#endif /* CFG_SDIO_RX_AGG */


VOID halProcessRxInterrupt(IN P_ADAPTER_T prAdapter)
{
#if CFG_SDIO_INTR_ENHANCE
#if CFG_SDIO_RX_AGG
	halRxSDIOAggReceiveRFBs(prAdapter);
#else
	halRxSDIOEnhanceReceiveRFBs(prAdapter);
#endif
#else
	halRxSDIOReceiveRFBs(prAdapter);
#endif /* CFG_SDIO_INTR_ENHANCE */
}

VOID halHifSwInfoInit(IN P_ADAPTER_T prAdapter)
{

}

VOID halRxProcessMsduReport(IN P_ADAPTER_T prAdapter, IN OUT P_SW_RFB_T prSwRfb)
{

}

UINT_32 halTxGetPageCount(IN UINT_32 u4FrameLength, IN BOOLEAN fgIncludeDesc)
{
	return 1;
}

VOID halDumpHifStatus(IN P_GLUE_INFO_T prGlueInfo)
{

}

#if (CFG_SDIO_ACCESS_N9_REGISTER_BY_MAILBOX == 1)
/*----------------------------------------------------------------------------*/
/*!
* \brief
*       This routine is used to get the value of N9 register
*       by SDIO SW interrupt and mailbox.
*
* \param[in]
*       pvAdapter: Pointer to the Adapter structure.
*       addr: the interested address to be read
*       prresult: to stored the value of the addr
*
* \return
*       the error of the reading operation
*/
/*----------------------------------------------------------------------------*/

BOOL halReadN9RegisterByMailBox(IN P_ADAPTER_T prAdapter, IN UINT_32 addr, IN UINT_32 * prresult)
{
	UINT_32 ori_whlpcr, temp, counter = 0;
	BOOL err = TRUE, stop = FALSE;

	/* use polling mode */
	HAL_MCR_RD(prAdapter, MCR_WHLPCR, &ori_whlpcr); /* backup the original setting of W_INT_EN */
	ori_whlpcr &= WHLPCR_INT_EN_SET;
	HAL_MCR_WR(prAdapter, MCR_WHLPCR, WHLPCR_INT_EN_CLR); /* disabel interrupt */

	/* progrqm h2d mailbox0 as interested register address */
	HAL_MCR_WR(prAdapter, MCR_H2DSM0R, addr);

	/* set h2d interrupt to notify firmware (bit16) */
	HAL_MCR_WR(prAdapter, MCR_WSICR, SDIO_MAILBOX_FUNC_READ_REG_IDX);

	/* polling interrupt status for the returned result */
	while (!stop) {
		HAL_MCR_RD(prAdapter, MCR_WHISR, &temp); /* read clear mode */
		if (temp & SDIO_MAILBOX_FUNC_READ_REG_IDX) {
			/* get the result */

			/* read d2h mailbox0 for interested register address */
			HAL_MCR_RD(prAdapter, MCR_D2HRM0R, &temp);
			if (temp == addr) {
				/* read d2h mailbox1 for the value of the register */
				HAL_MCR_RD(prAdapter, MCR_D2HRM1R, prresult);
				err = FALSE;
			} else {
	DBGLOG(HAL, ERROR, "halReadN9RegisterByMailBox >> interested address is not correct.\n");
			}
			stop = TRUE;
		} else {
counter++;

if (counter > 300000) {
	DBGLOG(HAL, ERROR, "halReadN9RegisterByMailBox >> get response failure.\n");
				ASSERT(0);
				break;
			}
		}
	}

	HAL_MCR_WR(prAdapter, MCR_WHLPCR, ori_whlpcr); /* restore the W_INT_EN */

	return err;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief
*       This routine is used to write the value of N9 register by SDIO SW interrupt and mailbox.
*
* \param[in]
*       pvAdapter: Pointer to the Adapter structure.
*       addr: the interested address to be write
*       value: the value to write into the addr
*
* \return
*       the error of the write operation
*/
/*----------------------------------------------------------------------------*/

BOOL halWriteN9RegisterByMailBox(IN P_ADAPTER_T prAdapter, IN UINT_32 addr, IN UINT_32 value)
{
	UINT_32 ori_whlpcr, temp, counter = 0;
	BOOL err = TRUE, stop = FALSE;

	/* use polling mode */
	HAL_MCR_RD(prAdapter, MCR_WHLPCR, &ori_whlpcr); /* backup the original setting of W_INT_EN */
	ori_whlpcr &= WHLPCR_INT_EN_SET;
	HAL_MCR_WR(prAdapter, MCR_WHLPCR, WHLPCR_INT_EN_CLR); /* disabel interrupt */

	/* progrqm h2d mailbox0 as interested register address */
	HAL_MCR_WR(prAdapter, MCR_H2DSM0R, addr);

	/* progrqm h2d mailbox1 as the value to write */
	HAL_MCR_WR(prAdapter, MCR_H2DSM1R, value);

	/* set h2d interrupt to notify firmware (bit17) */
	HAL_MCR_WR(prAdapter, MCR_WSICR, SDIO_MAILBOX_FUNC_WRITE_REG_IDX);

	/* polling interrupt status for the returned result */
	while (!stop) {
		HAL_MCR_RD(prAdapter, MCR_WHISR, &temp); /* read clear mode */

		if (temp & SDIO_MAILBOX_FUNC_WRITE_REG_IDX) {
			/* get the result */

			/* read d2h mailbox0 for interested register address */
			HAL_MCR_RD(prAdapter, MCR_D2HRM0R, &temp);
			if (temp == addr)
				err = FALSE;
			else {
	DBGLOG(HAL, ERROR, "halWriteN9RegisterByMailBox >> interested address is not correct.\n");
			}
			stop = TRUE;
		} else {
			counter++;

if (counter > 300000) {
	DBGLOG(HAL, ERROR, "halWriteN9RegisterByMailBox >> get response failure.\n");
				ASSERT(0);
				break;
			}
		}
	}

	HAL_MCR_WR(prAdapter, MCR_WHLPCR, ori_whlpcr); /* restore the W_INT_EN */

	return err;
}
#endif


