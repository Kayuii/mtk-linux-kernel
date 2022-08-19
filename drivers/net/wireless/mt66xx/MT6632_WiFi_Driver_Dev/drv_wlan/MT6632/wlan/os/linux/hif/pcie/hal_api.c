/******************************************************************************
*[File]             hif_api.c
*[Version]          v1.0
*[Revision Date]    2015-09-08
*[Author]
*[Description]
*    The program provides PCIE HIF APIs
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

#include "hif_pci.h"

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

static PCIE_CHIP_CR_MAPPING arBus2ChipCrMapping[] = {
	/* chip addr, bus addr, range */
	{0x82060000, 0x00008000, 0x00000450}, /* WF_PLE */
	{0x82068000, 0x0000c000, 0x00000450}, /* WF_PSE */
	{0x8206c000, 0x0000e000, 0x00000300}, /* PP */
	{0x820d0000, 0x00020000, 0x00000200}, /* WF_AON */
	{0x820f0000, 0x00020200, 0x00000400}, /* WF_CFG */
	{0x820f0800, 0x00020600, 0x00000200}, /* WF_CFGOFF */
	{0x820f1000, 0x00020800, 0x00000200}, /* WF_TRB */
	{0x820f2000, 0x00020a00, 0x00000200}, /* WF_AGG */
	{0x820f3000, 0x00020c00, 0x00000400}, /* WF_ARB */
	{0x820f4000, 0x00021000, 0x00000200}, /* WF_TMAC */
	{0x820f5000, 0x00021200, 0x00000400}, /* WF_RMAC */
	{0x820f6000, 0x00021600, 0x00000200}, /* WF_SEC */
	{0x820f7000, 0x00021800, 0x00000200}, /* WF_DMA */

	{0x820f8000, 0x00022000, 0x00001000}, /* WF_PF */
	{0x820f9000, 0x00023000, 0x00000400}, /* WF_WTBLON */
	{0x820f9800, 0x00023400, 0x00000200}, /* WF_WTBLOFF */

	{0x820fa000, 0x00024000, 0x00000200}, /* WF_ETBF */
	{0x820fb000, 0x00024200, 0x00000400}, /* WF_LPON */
	{0x820fc000, 0x00024600, 0x00000200}, /* WF_INT */
	{0x820fd000, 0x00024800, 0x00000400}, /* WF_MIB */

	{0x820fe000, 0x00025000, 0x00002000}, /* WF_MU */

	{0x820e0000, 0x00030000, 0x00010000}, /* WF_WTBL */

	{0x80020000, 0x00000000, 0x00002000}, /* TOP_CFG */
	{0x80000000, 0x00002000, 0x00002000}, /* MCU_CFG */
	{0x50000000, 0x00004000, 0x00004000}, /* PDMA_CFG */
	{0xA0000000, 0x00008000, 0x00008000}, /* PSE_CFG */
	{0x82070000, 0x00010000, 0x00010000}, /* WF_PHY */

	{0x0, 0x0, 0x0}
};

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

	HAL_MCR_RD(prAdapter, TOP_HW_CONTROL, &u4CIR);

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

	HAL_MCR_RD(prAdapter, TOP_HW_VERSION, &u4CIR);

	prAdapter->ucRevID = (UINT_8)(u4CIR & 0xF);
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

	DEBUGFUNC("nicRxWaitResponse");

	ASSERT(prAdapter);
	ASSERT(pucRspBuffer);
	ASSERT(ucPortIdx < 2);

	ucPortIdx = HIF_IMG_DL_STATUS_PORT_IDX;

	prRxCtrl = &prAdapter->rRxCtrl;

	u4Time = (UINT_32) kalGetTimeTick();

	do {
		/* Read pdma0 RX done Interrupt */
		HAL_MCR_RD(prAdapter, WPDMA_INT_STA, &u4Value);


		u4PktLen = ((u4Value & WPDMA_RX_DONE_INT1)) ? u4MaxRespBufferLen : 0;
		/* clear RX_DONE_INT_1 */
		if (u4PktLen != 0)
			HAL_MCR_WR(prAdapter, WPDMA_INT_STA, WPDMA_RX_DONE_INT1);

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

			i++;
		} else {
			HAL_PORT_RD(prAdapter, ucPortIdx, u4PktLen, pucRspBuffer,
				CFG_RX_COALESCING_BUFFER_SIZE);
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
	P_GLUE_INFO_T prGlueInfo = NULL;
	P_GL_HIF_INFO_T prHifInfo = NULL;
	WPMDA_INT_MASK IntMask;

	prGlueInfo = prAdapter->prGlueInfo;
	prHifInfo = &prGlueInfo->rHifInfo;

	prAdapter->fgIsIntEnable = TRUE;

	HAL_MCR_RD(prAdapter, WPDMA_INT_MSK, &IntMask.word);

	IntMask.field.rx_done_0 = 1;
	IntMask.field.rx_done_1 = 1;
	IntMask.field.tx_done_0 = 1;
	IntMask.field.tx_done_1 = 1;
	IntMask.field.tx_done_2 = 1;
	IntMask.field.tx_done_3 = 1;
	IntMask.field.tx_coherent = 0;
	IntMask.field.rx_coherent = 0;
	IntMask.field.tx_dly_int = 0;
	IntMask.field.rx_dly_int = 0;

	HAL_MCR_WR(prAdapter, WPDMA_INT_MSK, IntMask.word);

	DBGLOG(HAL, TRACE, "%s [0x%08x]\n", __func__, IntMask.word);
}	/* end of nicEnableInterrupt() */



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
	P_GLUE_INFO_T prGlueInfo = NULL;
	WPMDA_INT_MASK IntMask;

	ASSERT(prAdapter);

	prGlueInfo = prAdapter->prGlueInfo;

	IntMask.word = 0;

	HAL_MCR_WR(prAdapter, WPDMA_INT_MSK, IntMask.word);
	HAL_MCR_RD(prAdapter, WPDMA_INT_MSK, &IntMask.word);

	prAdapter->fgIsIntEnable = FALSE;

	DBGLOG(HAL, TRACE, "%s\n", __func__);

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
	UINT_32 i, u4CurrTick;
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
		DBGLOG(INIT, STATE, "Skip FW OWN due to pending INT\n");
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

VOID halTxCancelSendingCmd(IN P_ADAPTER_T prAdapter, IN P_CMD_INFO_T prCmdInfo)
{
}

BOOLEAN halTxIsDataBufEnough(IN P_ADAPTER_T prAdapter, IN UINT_8 ucTc, IN UINT_32 u4Length)
{
	P_GL_HIF_INFO_T prHifInfo = NULL;
	P_RTMP_TX_RING prTxRing;

	prHifInfo = &prAdapter->prGlueInfo->rHifInfo;
	prTxRing = &prHifInfo->TxRing[TX_RING_DATA0_IDX_0];

	if ((halGetMsduTokenFreeCnt(prAdapter) > 0) && ((TX_RING_SIZE - prTxRing->u4UsedCnt) > 0))
		return TRUE;
	else {
		DBGLOG(HAL, TRACE, "Low Tx Data Resource Tok[%u] Ring[%u]\n", halGetMsduTokenFreeCnt(prAdapter),
			(TX_RING_SIZE - prTxRing->u4UsedCnt));
		return FALSE;
	}
}

VOID halProcessTxInterrupt(IN P_ADAPTER_T prAdapter)
{
	P_GL_HIF_INFO_T prHifInfo = &prAdapter->prGlueInfo->rHifInfo;
	WPDMA_INT_STA_STRUCT rIntrStatus;

	rIntrStatus = (WPDMA_INT_STA_STRUCT)prHifInfo->u4IntStatus;

	if (rIntrStatus.field.tx_done_3 == 1)
		halWpdmaProcessCmdDmaDone(prAdapter->prGlueInfo, TX_RING_FWDL_IDX_3);

	if (rIntrStatus.field.tx_done_2 == 1)
		halWpdmaProcessCmdDmaDone(prAdapter->prGlueInfo, TX_RING_CMD_IDX_2);

	if (rIntrStatus.field.tx_done_0 == 1) {
		halWpdmaProcessDataDmaDone(prAdapter->prGlueInfo, TX_RING_DATA0_IDX_0);

		kalSetTxEvent2Hif(prAdapter->prGlueInfo);
	}
}

VOID halInitMsduTokenInfo(IN P_ADAPTER_T prAdapter)
{
	P_MSDU_TOKEN_INFO_T prTokenInfo = &prAdapter->prGlueInfo->rHifInfo.rTokenInfo;
	P_MSDU_TOKEN_ENTRY_T prToken;
	UINT_32 u4Idx;

	for (u4Idx = 0; u4Idx < HIF_TX_MSDU_TOKEN_NUM; u4Idx++) {
		prToken = &prTokenInfo->arToken[u4Idx];
		prToken->fgInUsed = FALSE;
		prToken->prMsduInfo = NULL;

#if HIF_TX_PREALLOC_DATA_BUFFER
		prToken->u4DmaLength = NIC_TX_MAX_SIZE_PER_FRAME + NIC_TX_HEAD_ROOM;
		prToken->prPacket = kalMemAlloc(prToken->u4DmaLength, PHY_MEM_TYPE);
		if (prToken->prPacket) {
			prToken->rDmaAddr = pci_map_single(prAdapter->prGlueInfo->rHifInfo.pdev,
				prToken->prPacket, prToken->u4DmaLength, PCI_DMA_TODEVICE);

			DBGLOG(HAL, TRACE, "Msdu Entry[0x%p] Tok[%u] Buf[0x%p] len[%u]\n", prToken,
				u4Idx, prToken->prPacket, prToken->u4DmaLength);
		} else {
			prToken->fgInUsed = TRUE;
			DBGLOG(HAL, WARN, "Msdu Token Memory alloc failed[%u]\n", u4Idx);
			continue;
		}
#else
		prToken->prPacket = NULL;
		prToken->u4DmaLength = 0;
		prToken->rDmaAddr = 0;
#endif
		prToken->u4Token = u4Idx;

		prTokenInfo->aprTokenStack[u4Idx] = prToken;
	}

	spin_lock_init(&prTokenInfo->rTokenLock);
	prTokenInfo->i4UsedCnt = 0;

	DBGLOG(HAL, INFO, "Msdu Token Init: Tot[%u] Used[%u]\n", HIF_TX_MSDU_TOKEN_NUM, prTokenInfo->i4UsedCnt);
}

VOID halUninitMsduTokenInfo(IN P_ADAPTER_T prAdapter)
{
	P_MSDU_TOKEN_INFO_T prTokenInfo = &prAdapter->prGlueInfo->rHifInfo.rTokenInfo;
	P_MSDU_TOKEN_ENTRY_T prToken;
	UINT_32 u4Idx;

	for (u4Idx = 0; u4Idx < HIF_TX_MSDU_TOKEN_NUM; u4Idx++) {
		prToken = &prTokenInfo->arToken[u4Idx];

		if (prToken->fgInUsed) {
			kalPciUnmapToDev(prAdapter->prGlueInfo, prToken->rDmaAddr, prToken->u4DmaLength);

			DBGLOG(HAL, TRACE, "Clear pending Tok[%u] Msdu[0x%p] Free[%u]\n", prToken->u4Token,
				prToken->prMsduInfo, halGetMsduTokenFreeCnt(prAdapter));

#if !HIF_TX_PREALLOC_DATA_BUFFER
			nicTxFreePacket(prAdapter, prToken->prMsduInfo, FALSE);
			nicTxReturnMsduInfo(prAdapter, prToken->prMsduInfo);
#endif
		}

#if HIF_TX_PREALLOC_DATA_BUFFER
		kalMemFree(prToken->prPacket, PHY_MEM_TYPE, prToken->u4DmaLength);
		prToken->prPacket = NULL;
#endif
	}

	prTokenInfo->i4UsedCnt = 0;

	DBGLOG(HAL, INFO, "Msdu Token Uninit: Tot[%u] Used[%u]\n", HIF_TX_MSDU_TOKEN_NUM, prTokenInfo->i4UsedCnt);
}

UINT_32 halGetMsduTokenFreeCnt(IN P_ADAPTER_T prAdapter)
{
	P_MSDU_TOKEN_INFO_T prTokenInfo = &prAdapter->prGlueInfo->rHifInfo.rTokenInfo;

	return (HIF_TX_MSDU_TOKEN_NUM - prTokenInfo->i4UsedCnt);
}

P_MSDU_TOKEN_ENTRY_T halGetMsduTokenEntry(IN P_ADAPTER_T prAdapter, UINT_32 u4TokenNum)
{
	P_MSDU_TOKEN_INFO_T prTokenInfo = &prAdapter->prGlueInfo->rHifInfo.rTokenInfo;

	return &prTokenInfo->arToken[u4TokenNum];
}

P_MSDU_TOKEN_ENTRY_T halAcquireMsduToken(IN P_ADAPTER_T prAdapter)
{
	P_MSDU_TOKEN_INFO_T prTokenInfo = &prAdapter->prGlueInfo->rHifInfo.rTokenInfo;
	P_MSDU_TOKEN_ENTRY_T prToken;
	ULONG flags = 0;

	if (!halGetMsduTokenFreeCnt(prAdapter)) {
		DBGLOG(HAL, INFO, "No more free MSDU token, Used[%u]\n", prTokenInfo->i4UsedCnt);
		return NULL;
	}

	spin_lock_irqsave(&prTokenInfo->rTokenLock, flags);

	prToken = prTokenInfo->aprTokenStack[prTokenInfo->i4UsedCnt];
	prToken->fgInUsed = TRUE;
	prTokenInfo->i4UsedCnt++;

	spin_unlock_irqrestore(&prTokenInfo->rTokenLock, flags);

	DBGLOG(HAL, TRACE, "Acquire Entry[0x%p] Tok[%u] Buf[0x%u] Len[%u]\n", prToken,
		prToken->u4Token, prToken->prPacket, prToken->u4DmaLength);

	return prToken;
}

VOID halReturnMsduToken(IN P_ADAPTER_T prAdapter, UINT_32 u4TokenNum)
{
	P_MSDU_TOKEN_INFO_T prTokenInfo = &prAdapter->prGlueInfo->rHifInfo.rTokenInfo;
	P_MSDU_TOKEN_ENTRY_T prToken;
	ULONG flags = 0;

	if (!prTokenInfo->i4UsedCnt) {
		DBGLOG(HAL, INFO, "MSDU token is full, Used[%u]\n", prTokenInfo->i4UsedCnt);
		return;
	}

	prToken = &prTokenInfo->arToken[u4TokenNum];

	spin_lock_irqsave(&prTokenInfo->rTokenLock, flags);

	prToken->fgInUsed = FALSE;
	prTokenInfo->i4UsedCnt--;
	prTokenInfo->aprTokenStack[prTokenInfo->i4UsedCnt] = prToken;

	spin_unlock_irqrestore(&prTokenInfo->rTokenLock, flags);
}

VOID halHifSwInfoInit(IN P_ADAPTER_T prAdapter)
{
	halInitMsduTokenInfo(prAdapter);
}

VOID halRxProcessMsduReport(IN P_ADAPTER_T prAdapter, IN OUT P_SW_RFB_T prSwRfb)
{
	P_HW_MAC_MSDU_REPORT_T prMsduReport;
#if !HIF_TX_PREALLOC_DATA_BUFFER
	P_MSDU_INFO_T prMsduInfo;
#endif
	QUE_T rFreeQueue;
	P_QUE_T prFreeQueue;
	UINT_16 u2TokenCnt;
	UINT_32 u4Idx, u4Token;
	P_MSDU_TOKEN_ENTRY_T prTokenEntry;

	prFreeQueue = &rFreeQueue;
	QUEUE_INITIALIZE(prFreeQueue);

	prMsduReport = (P_HW_MAC_MSDU_REPORT_T)prSwRfb->pucRecvBuff;
	u2TokenCnt = prMsduReport->u2MsduCount;

	for (u4Idx = 0; u4Idx < u2TokenCnt; u4Idx++) {
		u4Token = prMsduReport->au2MsduToken[u4Idx];
		prTokenEntry = halGetMsduTokenEntry(prAdapter, u4Token);

#if HIF_TX_PREALLOC_DATA_BUFFER
		DBGLOG(HAL, TRACE, "MsduRpt: Cnt[%u] Tok[%u] Free[%u]\n", u2TokenCnt,
			u4Token, halGetMsduTokenFreeCnt(prAdapter));
#else
		prMsduInfo = prTokenEntry->prMsduInfo;
		prMsduInfo->prToken = NULL;
		if (!prMsduInfo->pfTxDoneHandler)
			QUEUE_INSERT_TAIL(prFreeQueue, (P_QUE_ENTRY_T) prMsduInfo);

		kalPciUnmapToDev(prAdapter->prGlueInfo, prTokenEntry->rDmaAddr, prTokenEntry->u4DmaLength);

		DBGLOG(HAL, TRACE, "MsduRpt: Cnt[%u] Tok[%u] Msdu[0x%p] TxDone[%u] Free[%u]\n", u2TokenCnt,
			u4Token, prMsduInfo, (prMsduInfo->pfTxDoneHandler ? TRUE : FALSE),
			halGetMsduTokenFreeCnt(prAdapter));
#endif

		halReturnMsduToken(prAdapter, u4Token);
	}

#if !HIF_TX_PREALLOC_DATA_BUFFER
	nicTxMsduDoneCb(prAdapter->prGlueInfo, prFreeQueue);
#endif

	/* Indicate Service Thread */
	if (wlanGetTxPendingFrameCount(prAdapter) > 0)
		kalSetEvent(prAdapter->prGlueInfo);

	kalSetTxEvent2Hif(prAdapter->prGlueInfo);
}

VOID halTxUpdateCutThroughDesc(P_MSDU_INFO_T prMsduInfo, P_MSDU_TOKEN_ENTRY_T prToken)
{
	PUINT_8 pucBufferTxD;
	struct sk_buff *skb;
	P_HW_MAC_TX_DESC_APPEND_T prHwTxDescAppend;

	skb = (struct sk_buff *)prMsduInfo->prPacket;
	/* pucBufferTxD = skb->data; */
	pucBufferTxD = prToken->prPacket;

	prHwTxDescAppend = (P_HW_MAC_TX_DESC_APPEND_T) (pucBufferTxD + NIC_TX_DESC_LONG_FORMAT_LENGTH);

	prHwTxDescAppend->u2MsduToken = (UINT_16)prToken->u4Token;

	prHwTxDescAppend->ucBufNum = 1;
	prHwTxDescAppend->au4BufPtr[0] = (prToken->rDmaAddr + NIC_TX_HEAD_ROOM);
	prHwTxDescAppend->au2BufLen[0] = prMsduInfo->u2FrameLength;
}

UINT_32 halTxGetPageCount(IN UINT_32 u4FrameLength, IN BOOLEAN fgIncludeDesc)
{
	return 1;
}

WLAN_STATUS halTxPollingResource(IN P_ADAPTER_T prAdapter, IN UINT_8 ucTC)
{
	return WLAN_STATUS_SUCCESS;
}

VOID halRxPCIeReceiveRFBs(IN P_ADAPTER_T prAdapter, UINT_32 u4Port)
{
	P_RX_CTRL_T prRxCtrl;
	P_SW_RFB_T prSwRfb = (P_SW_RFB_T) NULL;
	PUINT_8 pucBuf = NULL;
	P_HW_MAC_RX_DESC_T prRxStatus;
	BOOLEAN fgStatus;
	UINT_32 u4RxCnt;

	KAL_SPIN_LOCK_DECLARATION();

	DEBUGFUNC("nicRxPCIeReceiveRFBs");

	ASSERT(prAdapter);

	prRxCtrl = &prAdapter->rRxCtrl;
	ASSERT(prRxCtrl);

	u4RxCnt = halWpdmaGetRxDmaDoneCnt(prAdapter->prGlueInfo, u4Port);

	while (u4RxCnt--) {
		KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_FREE_QUE);
		QUEUE_REMOVE_HEAD(&prRxCtrl->rFreeSwRfbList, prSwRfb, P_SW_RFB_T);
		KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_FREE_QUE);

		if (!prSwRfb) {
			DBGLOG(RX, WARN, "No More RFB for P[%u] Num[%d]\n", u4Port, prRxCtrl->rFreeSwRfbList.u4NumElem);
			break;
		}

		if (u4Port == RX_RING_DATA_IDX_0) {
			fgStatus = kalDevReadData(prAdapter->prGlueInfo, u4Port, prSwRfb);
		} else {
			pucBuf = prSwRfb->pucRecvBuff;
			ASSERT(pucBuf);

			fgStatus = kalDevPortRead(prAdapter->prGlueInfo, u4Port, CFG_RX_MAX_PKT_SIZE,
				pucBuf, CFG_RX_MAX_PKT_SIZE);
		}
		if (!fgStatus) {
			KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_FREE_QUE);
			QUEUE_INSERT_TAIL(&prRxCtrl->rFreeSwRfbList, &prSwRfb->rQueEntry);
			KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_FREE_QUE);

			break;
		}

		prRxStatus = prSwRfb->prRxStatus;
		ASSERT(prRxStatus);

		prSwRfb->ucPacketType = (UINT_8) HAL_RX_STATUS_GET_PKT_TYPE(prRxStatus);
		DBGLOG(RX, TRACE, "ucPacketType = %d\n", prSwRfb->ucPacketType);

		if (prSwRfb->ucPacketType == RX_PKT_TYPE_MSDU_REPORT) {
			nicRxProcessMsduReport(prAdapter, prSwRfb);

			continue;
		}

		prSwRfb->ucStaRecIdx =
			secGetStaIdxByWlanIdx(prAdapter, (UINT_8)HAL_RX_STATUS_GET_WLAN_IDX(prRxStatus));

		KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
		QUEUE_INSERT_TAIL(&prRxCtrl->rReceivedRfbList, &prSwRfb->rQueEntry);
		RX_INC_CNT(prRxCtrl, RX_MPDU_TOTAL_COUNT);
		KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
	}

}

/*----------------------------------------------------------------------------*/
/*!
* @brief Read frames from the data port for PCIE
*        I/F, fill RFB and put each frame into the rReceivedRFBList queue.
*
* @param prAdapter      Pointer to the Adapter structure.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID halProcessRxInterrupt(IN P_ADAPTER_T prAdapter)
{
	P_GL_HIF_INFO_T prHifInfo = &prAdapter->prGlueInfo->rHifInfo;
	WPDMA_INT_STA_STRUCT rIntrStatus;

	rIntrStatus = (WPDMA_INT_STA_STRUCT)prHifInfo->u4IntStatus;

	if (rIntrStatus.field.rx_done_1)
		halRxPCIeReceiveRFBs(prAdapter, RX_RING_EVT_IDX_1);

	if (rIntrStatus.field.rx_done_0)
		halRxPCIeReceiveRFBs(prAdapter, RX_RING_DATA_IDX_0);
}

static INT_32 halWpdmaAllocRingDesc(P_GLUE_INFO_T prGlueInfo, RTMP_DMABUF *pDescRing, INT_32 size)
{
	P_GL_HIF_INFO_T prHifInfo = &prGlueInfo->rHifInfo;
	struct pci_dev *pdev = prHifInfo->pdev;

	pDescRing->AllocSize = size;

	pDescRing->AllocVa = (PVOID) pci_alloc_consistent(pdev, pDescRing->AllocSize, &pDescRing->AllocPa);

	if (pDescRing->AllocVa == NULL) {
		DBGLOG(HAL, ERROR, "Failed to allocate a big buffer\n");
		return -1;
	}

	/* Zero init this memory block */
	kalMemZero(pDescRing->AllocVa, size);

	return 0;
}

static INT_32 halWpdmaFreeRingDesc(P_GLUE_INFO_T prGlueInfo, RTMP_DMABUF *pDescRing)
{
	P_GL_HIF_INFO_T prHifInfo = &prGlueInfo->rHifInfo;
	struct pci_dev *pdev = prHifInfo->pdev;

	if (pDescRing->AllocVa)
		pci_free_consistent(pdev, pDescRing->AllocSize, pDescRing->AllocVa, pDescRing->AllocPa);

	kalMemZero(pDescRing, sizeof(RTMP_DMABUF));

	return TRUE;
}

static PVOID halWpdmaAllocRxPacketBuff(IN PVOID pPciDev, IN ULONG Length,
	IN BOOLEAN Cached, OUT PPVOID VirtualAddress, OUT dma_addr_t *phy_addr)
{
	struct sk_buff *pkt;

	/* pkt = __dev_alloc_skb(Length, GFP_DMA | GFP_ATOMIC); */
	pkt = dev_alloc_skb(Length);

	if (pkt == NULL)
		DBGLOG(HAL, ERROR, "can't allocate rx %ld size packet\n", Length);

	if (pkt) {
		*VirtualAddress = (PVOID) pkt->data;
		*phy_addr = pci_map_single(pPciDev, *VirtualAddress, Length, PCI_DMA_FROMDEVICE);
	} else {
		*VirtualAddress = (PVOID) NULL;
		*phy_addr = (dma_addr_t) 0;
	}

	return (PVOID) pkt;
}

VOID halWpdmaAllocRxRing(P_GLUE_INFO_T prGlueInfo, UINT_32 u4Num, UINT_32 u4Size,
	UINT_32 u4DescSize, UINT_32 u4BufSize)
{
	dma_addr_t RingBasePa;
	PVOID RingBaseVa;
	INT_32 index;
	RXD_STRUCT *pRxD;
	RTMP_DMABUF *pDmaBuf;
	RTMP_DMACB *dma_cb;
	PVOID pPacket;
	P_GL_HIF_INFO_T prHifInfo = &prGlueInfo->rHifInfo;

	/* Alloc RxRingDesc memory except Tx ring allocated eariler */
	halWpdmaAllocRingDesc(prGlueInfo, &prHifInfo->RxDescRing[u4Num], u4Size * u4DescSize);
	if (prHifInfo->RxDescRing[u4Num].AllocVa == NULL) {
		DBGLOG(HAL, ERROR, "\n\n\nRxDescRing[%p] allocation failed!!\n\n\n");
		return;
	}

	DBGLOG(HAL, INFO, "RxDescRing[%p]: total %d bytes allocated\n",
		prHifInfo->RxDescRing[u4Num].AllocVa, (INT_32) prHifInfo->RxDescRing[u4Num].AllocSize);

	/* Initialize Rx Ring and associated buffer memory */
	RingBasePa = prHifInfo->RxDescRing[u4Num].AllocPa;
	RingBaseVa = prHifInfo->RxDescRing[u4Num].AllocVa;

	prHifInfo->RxRing[u4Num].u4BufSize = u4BufSize;
	prHifInfo->RxRing[u4Num].u4RingSize = u4Size;

	for (index = 0; index < u4Size; index++) {
		dma_cb = &prHifInfo->RxRing[u4Num].Cell[index];
		/* Init RX Ring Size, Va, Pa variables */
		dma_cb->AllocSize = u4DescSize;
		dma_cb->AllocVa = RingBaseVa;
		dma_cb->AllocPa = RingBasePa;

		/* Offset to next ring descriptor address */
		RingBasePa += u4DescSize;
		RingBaseVa = (PUCHAR) RingBaseVa + u4DescSize;

		/* Setup Rx associated Buffer size & allocate share memory */
		pDmaBuf = &dma_cb->DmaBuf;
		pDmaBuf->AllocSize = u4BufSize;
		pPacket = halWpdmaAllocRxPacketBuff(prHifInfo->pdev, pDmaBuf->AllocSize,
			FALSE, &pDmaBuf->AllocVa, &pDmaBuf->AllocPa);

		/* keep allocated rx packet */
		dma_cb->pPacket = pPacket;
		if (pDmaBuf->AllocVa == NULL) {
			DBGLOG(HAL, ERROR, "\n\n\nFailed to allocate RxRing buffer index[%u]\n\n\n", index);
			return;
		}

		/* Zero init this memory block */
		kalMemZero(pDmaBuf->AllocVa, pDmaBuf->AllocSize);

		/* Write RxD buffer address & allocated buffer length */
		pRxD = (RXD_STRUCT *) dma_cb->AllocVa;
		pRxD->SDP0 = pDmaBuf->AllocPa;
		pRxD->SDL0 = u4BufSize;
		pRxD->DDONE = 0;
	}

	DBGLOG(HAL, TRACE, "Rx[%d] Ring: total %d entry allocated\n", u4Num, index);
}

VOID halWpdmaAllocRing(P_GLUE_INFO_T prGlueInfo)
{
	dma_addr_t RingBasePa;
	PVOID RingBaseVa;
	INT_32 index, num;
	TXD_STRUCT *pTxD;
	RTMP_TX_RING *pTxRing;
	RTMP_DMABUF *pDmaBuf;
	RTMP_DMACB *dma_cb;
	P_GL_HIF_INFO_T prHifInfo = &prGlueInfo->rHifInfo;

	do {
		/*
		   Allocate all ring descriptors, include TxD, RxD, MgmtD.
		   Although each size is different, to prevent cacheline and alignment
		   issue, I intentional set them all to 64 bytes
		 */
		for (num = 0; num < NUM_OF_TX_RING; num++) {
			dma_addr_t BufBasePa;
			PVOID BufBaseVa;

			/*
			   Allocate Tx ring descriptor's memory
			 */
			halWpdmaAllocRingDesc(prGlueInfo, &prHifInfo->TxDescRing[num], TX_RING_SIZE * TXD_SIZE);
			if (prHifInfo->TxDescRing[num].AllocVa == NULL)
				break;

			pDmaBuf = &prHifInfo->TxDescRing[num];
			DBGLOG(HAL, TRACE, "TxDescRing[%p]: total %d bytes allocated\n",
			       pDmaBuf->AllocVa, (INT_32) pDmaBuf->AllocSize);

			/* Save PA & VA for further operation */
			RingBasePa = pDmaBuf->AllocPa;
			RingBaseVa = pDmaBuf->AllocVa;

			/*
			   Allocate all 1st TXBuf's memory for this TxRing
			 */
			halWpdmaAllocRingDesc(prGlueInfo, &prHifInfo->TxBufSpace[num],
						 TX_RING_SIZE * TX_DMA_1ST_BUFFER_SIZE);
			if (prHifInfo->TxBufSpace[num].AllocVa == NULL) {
				DBGLOG(HAL, ERROR, "Failed to allocate a big buffer\n");
				break;
			}

			/* Zero init this memory block */
			kalMemZero(prHifInfo->TxBufSpace[num].AllocVa, prHifInfo->TxBufSpace[num].AllocSize);

			/* Save PA & VA for further operation */
			BufBasePa = prHifInfo->TxBufSpace[num].AllocPa;
			BufBaseVa = prHifInfo->TxBufSpace[num].AllocVa;

			/*
			   Initialize Tx Ring Descriptor and associated buffer memory
			 */
			pTxRing = &prHifInfo->TxRing[num];
			for (index = 0; index < TX_RING_SIZE; index++) {
				dma_cb = &pTxRing->Cell[index];
				dma_cb->pPacket = NULL;
				dma_cb->pBuffer = NULL;
				/* Init Tx Ring Size, Va, Pa variables */
				dma_cb->AllocSize = TXD_SIZE;
				dma_cb->AllocVa = RingBaseVa;
				dma_cb->AllocPa = RingBasePa;

				/* Setup Tx Buffer size & address. only 802.11 header will store in this space */
				pDmaBuf = &dma_cb->DmaBuf;
				pDmaBuf->AllocSize = TX_DMA_1ST_BUFFER_SIZE;
				pDmaBuf->AllocVa = BufBaseVa;
				pDmaBuf->AllocPa = BufBasePa;

				/* link the pre-allocated TxBuf to TXD */
				pTxD = (TXD_STRUCT *) dma_cb->AllocVa;
				pTxD->SDPtr0 = BufBasePa;
				/* advance to next ring descriptor address */
				pTxD->DMADONE = 1;

				RingBasePa += TXD_SIZE;
				RingBaseVa = (PUCHAR) RingBaseVa + TXD_SIZE;

				/* advance to next TxBuf address */
				BufBasePa += TX_DMA_1ST_BUFFER_SIZE;
				BufBaseVa = (PUCHAR) BufBaseVa + TX_DMA_1ST_BUFFER_SIZE;
			}

			DBGLOG(HAL, TRACE, "TxRing[%d]: total %d entry allocated\n", num, index);
		}

#if 1
		/* Data Rx path */
		halWpdmaAllocRxRing(prGlueInfo, RX_RING_DATA_IDX_0, RX_RING0_SIZE, RXD_SIZE, CFG_RX_MAX_PKT_SIZE);
		/* Event Rx path */
		halWpdmaAllocRxRing(prGlueInfo, RX_RING_EVT_IDX_1, RX_RING1_SIZE, RXD_SIZE, RX_BUFFER_AGGRESIZE);
#else
		for (num = 0; num < NUM_OF_RX_RING; num++) {
			/* Alloc RxRingDesc memory except Tx ring allocated eariler */
			halWpdmaAllocRingDesc(prGlueInfo, &prHifInfo->RxDescRing[num], RX_RING_SIZE * RXD_SIZE);
			if (prHifInfo->RxDescRing[num].AllocVa == NULL)
				break;

			DBGLOG(HAL, INFO, "RxDescRing[%p]: total %d bytes allocated\n",
			       prHifInfo->RxDescRing[num].AllocVa, (INT_32) prHifInfo->RxDescRing[num].AllocSize);

			/* Initialize Rx Ring and associated buffer memory */
			RingBasePa = prHifInfo->RxDescRing[num].AllocPa;
			RingBaseVa = prHifInfo->RxDescRing[num].AllocVa;
			for (index = 0; index < RX_RING_SIZE; index++) {
				dma_cb = &prHifInfo->RxRing[num].Cell[index];
				/* Init RX Ring Size, Va, Pa variables */
				dma_cb->AllocSize = RXD_SIZE;
				dma_cb->AllocVa = RingBaseVa;
				dma_cb->AllocPa = RingBasePa;

				/* Offset to next ring descriptor address */
				RingBasePa += RXD_SIZE;
				RingBaseVa = (PUCHAR) RingBaseVa + RXD_SIZE;

				/* Setup Rx associated Buffer size & allocate share memory */
				pDmaBuf = &dma_cb->DmaBuf;
				pDmaBuf->AllocSize = RX_BUFFER_AGGRESIZE;
				pPacket = halWpdmaAllocRxPacketBuff(prHifInfo->pdev,
								       pDmaBuf->AllocSize,
								       FALSE, &pDmaBuf->AllocVa, &pDmaBuf->AllocPa);

				/* keep allocated rx packet */
				dma_cb->pPacket = pPacket;
				if (pDmaBuf->AllocVa == NULL) {
					DBGLOG(HAL, ERROR, "Failed to allocate RxRing's 1st buffer\n");
					break;
				}

				/* Zero init this memory block */
				kalMemZero(pDmaBuf->AllocVa, pDmaBuf->AllocSize);

				/* Write RxD buffer address & allocated buffer length */
				pRxD = (RXD_STRUCT *) dma_cb->AllocVa;
				pRxD->SDP0 = pDmaBuf->AllocPa;
				pRxD->SDL0 = RX_BUFFER_AGGRESIZE;
				pRxD->DDONE = 0;
			}

			DBGLOG(HAL, TRACE, "Rx[%d] Ring: total %d entry allocated\n", num, index);
		}
#endif
	} while (FALSE);

	/* Initialize all transmit related software queues */

	/* Init TX rings index pointer */
	for (index = 0; index < NUM_OF_TX_RING; index++) {
		prHifInfo->TxRing[index].TxSwUsedIdx = 0;
		prHifInfo->TxRing[index].TxCpuIdx = 0;
	}

#if 0
	/* Init RX Ring index pointer */
	for (index = 0; index < NUM_OF_RX_RING; index++) {
		prHifInfo->RxRing[index].RxSwReadIdx = 0;
		prHifInfo->RxRing[index].RxCpuIdx = RX_RING_SIZE - 1;
	}
#endif
}

VOID halWpdmaFreeRing(P_GLUE_INFO_T prGlueInfo)
{
	int index, num, j;
	RTMP_TX_RING *pTxRing;
	TXD_STRUCT *pTxD;
	PVOID pPacket, pBuffer;
	RTMP_DMACB *dma_cb;
	P_GL_HIF_INFO_T prHifInfo = &prGlueInfo->rHifInfo;
	struct pci_dev *pdev = prHifInfo->pdev;

	/* Free Tx Ring Packet */
	for (index = 0; index < NUM_OF_TX_RING; index++) {
		pTxRing = &prHifInfo->TxRing[index];

		for (j = 0; j < TX_RING_SIZE; j++) {
			pTxD = (TXD_STRUCT *) (pTxRing->Cell[j].AllocVa);

			pPacket = pTxRing->Cell[j].pPacket;
			pBuffer = pTxRing->Cell[j].pBuffer;

			if (pPacket || pBuffer)
				pci_unmap_single(pdev, pTxD->SDPtr0, pTxD->SDLen0, PCI_DMA_TODEVICE);

			pTxRing->Cell[j].pPacket = NULL;

			if (pBuffer)
				kalMemFree(pBuffer, PHY_MEM_TYPE, 0);

			pTxRing->Cell[j].pBuffer = NULL;
		}
	}

	for (j = 0; j < NUM_OF_RX_RING; j++) {
		for (index = prHifInfo->RxRing[j].u4RingSize - 1; index >= 0; index--) {
			dma_cb = &prHifInfo->RxRing[j].Cell[index];
			if ((dma_cb->DmaBuf.AllocVa) && (dma_cb->pPacket)) {
				pci_unmap_single(pdev, dma_cb->DmaBuf.AllocPa,
					dma_cb->DmaBuf.AllocSize, PCI_DMA_FROMDEVICE);

				kalPacketFree(prGlueInfo, dma_cb->pPacket);
			}
		}
		kalMemZero(prHifInfo->RxRing[j].Cell, prHifInfo->RxRing[j].u4RingSize * sizeof(RTMP_DMACB));

		halWpdmaFreeRingDesc(prGlueInfo, &prHifInfo->RxDescRing[j]);
	}

	for (num = 0; num < NUM_OF_TX_RING; num++) {
		halWpdmaFreeRingDesc(prGlueInfo, &prHifInfo->TxBufSpace[num]);

		halWpdmaFreeRingDesc(prGlueInfo, &prHifInfo->TxDescRing[num]);
	}
}

static VOID halWpdmaSetup(P_GLUE_INFO_T prGlueInfo, BOOLEAN enable)
{
	WPDMA_GLO_CFG_STRUCT GloCfg;
	WPMDA_INT_MASK IntMask;

	kalDevRegRead(prGlueInfo, WPDMA_GLO_CFG, &GloCfg.word);

	kalDevRegRead(prGlueInfo, WPDMA_INT_MSK, &IntMask.word);

	if (enable == TRUE) {
		GloCfg.field.EnableTxDMA = 1;
		GloCfg.field.EnableRxDMA = 1;
		GloCfg.field.EnTXWriteBackDDONE = 1;
		GloCfg.field.WPDMABurstSIZE = 2;
		GloCfg.field.omit_tx_info = 1;
		GloCfg.field.fifo_little_endian = 1;

		IntMask.field.rx_done_0 = 1;
		IntMask.field.rx_done_1 = 1;
		IntMask.field.tx_done_0 = 1;
		IntMask.field.tx_done_1 = 1;
		IntMask.field.tx_done_2 = 1;
	} else {
		GloCfg.field.EnableRxDMA = 0;
		GloCfg.field.EnableTxDMA = 0;

		IntMask.field.rx_done_0 = 0;
		IntMask.field.rx_done_1 = 0;
		IntMask.field.tx_done_0 = 0;
		IntMask.field.tx_done_1 = 0;
		IntMask.field.tx_done_2 = 0;
	}

	kalDevRegWrite(prGlueInfo, WPDMA_INT_MSK, IntMask.word);

	kalDevRegWrite(prGlueInfo, WPDMA_GLO_CFG, GloCfg.word);
}

static BOOLEAN halWpdmaWaitIdle(P_GLUE_INFO_T prGlueInfo, INT_32 round, INT_32 wait_us)
{
	INT_32 i = 0;
	WPDMA_GLO_CFG_STRUCT GloCfg;

	do {
		kalDevRegRead(prGlueInfo, WPDMA_GLO_CFG, &GloCfg.word);
		if ((GloCfg.field.TxDMABusy == 0) && (GloCfg.field.RxDMABusy == 0)) {
			DBGLOG(HAL, TRACE, "==>  DMAIdle, GloCfg=0x%x\n", GloCfg.word);
			return TRUE;
		}
		kalUdelay(wait_us);
	} while ((i++) < round);

	DBGLOG(HAL, INFO, "==>  DMABusy, GloCfg=0x%x\n", GloCfg.word);

	return FALSE;
}

VOID halWpdmaInitRing(P_GLUE_INFO_T prGlueInfo)
{
	UINT_32 phy_addr, offset;
	INT_32 i;
	P_GL_HIF_INFO_T prHifInfo = &prGlueInfo->rHifInfo;
	RTMP_TX_RING *tx_ring;
	RTMP_RX_RING *rx_ring;

	/* Set DMA global configuration except TX_DMA_EN and RX_DMA_EN bits */
	halWpdmaSetup(prGlueInfo, FALSE);

	halWpdmaWaitIdle(prGlueInfo, 100, 1000);

	/* Reset DMA Index */
	kalDevRegWrite(prGlueInfo, WPDMA_RST_PTR, 0xFFFFFFFF);

	for (i = 0; i < NUM_OF_TX_RING; i++) {
		tx_ring = &prHifInfo->TxRing[i];
		offset = i * MT_RINGREG_DIFF;
		phy_addr = prHifInfo->TxRing[i].Cell[0].AllocPa;
		tx_ring->TxSwUsedIdx = 0;
		tx_ring->u4UsedCnt = 0;
		tx_ring->TxCpuIdx = 0;
		tx_ring->hw_desc_base = MT_TX_RING_BASE + offset;
		tx_ring->hw_cidx_addr = MT_TX_RING_CIDX + offset;
		tx_ring->hw_didx_addr = MT_TX_RING_DIDX + offset;
		tx_ring->hw_cnt_addr = MT_TX_RING_CNT + offset;
		kalDevRegWrite(prGlueInfo, tx_ring->hw_desc_base, phy_addr);
		kalDevRegWrite(prGlueInfo, tx_ring->hw_cidx_addr, tx_ring->TxCpuIdx);
		kalDevRegWrite(prGlueInfo, tx_ring->hw_cnt_addr, TX_RING_SIZE);

		spin_lock_init((spinlock_t *) (&prHifInfo->TxRingLock[i]));

		DBGLOG(HAL, INFO, "-->TX_RING_%d[0x%x]: Base=0x%x, Cnt=%d!\n",
			i, prHifInfo->TxRing[i].hw_desc_base, phy_addr, TX_RING_SIZE);
	}

	/* Init RX Ring0 Base/Size/Index pointer CSR */
	for (i = 0; i < NUM_OF_RX_RING; i++) {
		rx_ring = &prHifInfo->RxRing[i];
		offset = i * MT_RINGREG_DIFF;
		phy_addr = rx_ring->Cell[0].AllocPa;
		rx_ring->RxSwReadIdx = 0;
		rx_ring->RxCpuIdx = rx_ring->u4RingSize - 1;
		rx_ring->hw_desc_base = MT_RX_RING_BASE + offset;
		rx_ring->hw_cidx_addr = MT_RX_RING_CIDX + offset;
		rx_ring->hw_didx_addr = MT_RX_RING_DIDX + offset;
		rx_ring->hw_cnt_addr = MT_RX_RING_CNT + offset;
		kalDevRegWrite(prGlueInfo, rx_ring->hw_desc_base, phy_addr);
		kalDevRegWrite(prGlueInfo, rx_ring->hw_cidx_addr, rx_ring->RxCpuIdx);
		kalDevRegWrite(prGlueInfo, rx_ring->hw_cnt_addr, rx_ring->u4RingSize);

		spin_lock_init((spinlock_t *) (&prHifInfo->RxRingLock[i]));

		DBGLOG(HAL, INFO, "-->RX_RING%d[0x%x]: Base=0x%x, Cnt=%d\n",
			i, rx_ring->hw_desc_base, phy_addr, rx_ring->u4RingSize);
	}

	halWpdmaSetup(prGlueInfo, TRUE);
}

VOID halWpdmaProcessCmdDmaDone(IN P_GLUE_INFO_T prGlueInfo, IN UINT_16 u2Port)
{
	P_GL_HIF_INFO_T prHifInfo = NULL;
	UINT_32 u4SwIdx, u4DmaIdx;
	P_RTMP_TX_RING prTxRing;
	TXD_STRUCT *pTxD;
	PVOID pBuffer = NULL;
	struct pci_dev *pdev;
	ULONG flags = 0;

	ASSERT(prGlueInfo);

	prHifInfo = &prGlueInfo->rHifInfo;
	pdev = prHifInfo->pdev;
	prTxRing = &prHifInfo->TxRing[u2Port];

	spin_lock_irqsave(&prHifInfo->TxRingLock[u2Port], flags);

	kalDevRegRead(prGlueInfo, prTxRing->hw_didx_addr, &u4DmaIdx);

	u4SwIdx = prTxRing->TxSwUsedIdx;
	pBuffer = prTxRing->Cell[u4SwIdx].pBuffer;
	pTxD = (TXD_STRUCT *) prTxRing->Cell[u4SwIdx].AllocVa;

	if (pTxD->DMADONE == 0)
		return;

	do {
		pBuffer = prTxRing->Cell[u4SwIdx].pBuffer;
		pTxD = (TXD_STRUCT *) prTxRing->Cell[u4SwIdx].AllocVa;

		if (!pBuffer || (pTxD->DMADONE == 0))
			break;

		DBGLOG(HAL, TRACE, "DMA done: port[%u] dma[%u] idx[%u] done[%u] pkt[0x%p] used[%u]\n", u2Port,
			u4DmaIdx, u4SwIdx, pTxD->DMADONE, prTxRing->Cell[u4SwIdx].pPacket, prTxRing->u4UsedCnt);

		if (pTxD->SDPtr0)
			pci_unmap_single(pdev, pTxD->SDPtr0, pTxD->SDLen0, PCI_DMA_TODEVICE);

		if (pTxD->SDPtr1)
			pci_unmap_single(pdev, pTxD->SDPtr1, pTxD->SDLen1, PCI_DMA_TODEVICE);

		pTxD->DMADONE = 0;
		kalMemFree(prTxRing->Cell[u4SwIdx].pBuffer, PHY_MEM_TYPE, 0);
		prTxRing->Cell[u4SwIdx].pBuffer = NULL;
		prTxRing->Cell[u4SwIdx].pPacket = NULL;
		prTxRing->u4UsedCnt--;

		if (u2Port == TX_RING_CMD_IDX_2)
			nicTxReleaseResource(prGlueInfo->prAdapter, TC4_INDEX,
				nicTxGetPageCount(pTxD->SDLen0, TRUE), TRUE);

		INC_RING_INDEX(u4SwIdx, TX_RING_SIZE);
	} while (u4SwIdx != u4DmaIdx);

	prTxRing->TxSwUsedIdx = u4SwIdx;

	spin_unlock_irqrestore(&prHifInfo->TxRingLock[u2Port], flags);
}

VOID halWpdmaProcessDataDmaDone(IN P_GLUE_INFO_T prGlueInfo, IN UINT_16 u2Port)
{
	P_GL_HIF_INFO_T prHifInfo = NULL;
	UINT_32 u4SwIdx, u4DmaIdx;
	P_RTMP_TX_RING prTxRing;
	ULONG flags = 0;

	ASSERT(prGlueInfo);

	prHifInfo = &prGlueInfo->rHifInfo;
	prTxRing = &prHifInfo->TxRing[u2Port];

	kalDevRegRead(prGlueInfo, prTxRing->hw_didx_addr, &u4DmaIdx);
	u4SwIdx = prTxRing->TxSwUsedIdx;

	spin_lock_irqsave(&prHifInfo->TxRingLock[u2Port], flags);

	if (u4DmaIdx > u4SwIdx)
		prTxRing->u4UsedCnt -= u4DmaIdx - u4SwIdx;
	else if (u4DmaIdx < u4SwIdx)
		prTxRing->u4UsedCnt -= (TX_RING_SIZE + u4DmaIdx) - u4SwIdx;
	else {
		/* DMA index == SW used index */
		if (prTxRing->u4UsedCnt == TX_RING_SIZE)
			prTxRing->u4UsedCnt = 0;
	}

	spin_unlock_irqrestore(&prHifInfo->TxRingLock[u2Port], flags);

	DBGLOG(HAL, TRACE, "DMA done: port[%u] dma[%u] idx[%u] used[%u]\n", u2Port,
		u4DmaIdx, u4SwIdx, prTxRing->u4UsedCnt);

	prTxRing->TxSwUsedIdx = u4DmaIdx;
}

UINT_32 halWpdmaGetRxDmaDoneCnt(IN P_GLUE_INFO_T prGlueInfo, IN UINT_8 ucRingNum)
{
	P_RTMP_RX_RING prRxRing;
	P_GL_HIF_INFO_T prHifInfo;
	UINT_32 u4MaxCnt, u4CpuIdx, u4DmaIdx, u4RxPktCnt;

	prHifInfo = &prGlueInfo->rHifInfo;
	prRxRing = &prHifInfo->RxRing[ucRingNum];

	kalDevRegRead(prGlueInfo, prRxRing->hw_cnt_addr, &u4MaxCnt);
	kalDevRegRead(prGlueInfo, prRxRing->hw_cidx_addr, &u4CpuIdx);
	kalDevRegRead(prGlueInfo, prRxRing->hw_didx_addr, &u4DmaIdx);

	u4RxPktCnt = u4MaxCnt;

	if (u4CpuIdx > u4DmaIdx)
		u4RxPktCnt = u4MaxCnt + u4DmaIdx - u4CpuIdx - 1;
	else if (u4CpuIdx < u4DmaIdx)
		u4RxPktCnt = u4DmaIdx - u4CpuIdx - 1;

	return u4RxPktCnt;
}

VOID halDumpHifStatus(IN P_GLUE_INFO_T prGlueInfo)
{
	P_GL_HIF_INFO_T prHifInfo = &prGlueInfo->rHifInfo;
	UINT_32 u4Idx, u4DmaIdx, u4CpuIdx, u4MaxCnt;
	P_RTMP_TX_RING prTxRing;
	P_RTMP_RX_RING prRxRing;

	DBGLOG(SW4, INFO, "\n------<Dump PCIe Status>------\n");

	for (u4Idx = 0; u4Idx < NUM_OF_TX_RING; u4Idx++) {
		prTxRing = &prHifInfo->TxRing[u4Idx];
		kalDevRegRead(prGlueInfo, prTxRing->hw_cnt_addr, &u4MaxCnt);
		kalDevRegRead(prGlueInfo, prTxRing->hw_cidx_addr, &u4CpuIdx);
		kalDevRegRead(prGlueInfo, prTxRing->hw_didx_addr, &u4DmaIdx);

		DBGLOG(SW4, INFO, "TX[%u] SZ[%04u] CPU[%04u/%04u] DMA[%04u/%04u] SW_UD[%04u] Used[%u]\n", u4Idx,
			u4MaxCnt, prTxRing->TxCpuIdx, u4CpuIdx, prTxRing->TxDmaIdx, u4DmaIdx, prTxRing->TxSwUsedIdx,
			prTxRing->u4UsedCnt);
	}

	for (u4Idx = 0; u4Idx < NUM_OF_RX_RING; u4Idx++) {
		prRxRing = &prHifInfo->RxRing[u4Idx];

		kalDevRegRead(prGlueInfo, prRxRing->hw_cnt_addr, &u4MaxCnt);
		kalDevRegRead(prGlueInfo, prRxRing->hw_cidx_addr, &u4CpuIdx);
		kalDevRegRead(prGlueInfo, prRxRing->hw_didx_addr, &u4DmaIdx);

		DBGLOG(SW4, INFO, "RX[%u] SZ[%04u] CPU[%04u/%04u] DMA[%04u/%04u] SW_RD[%04u]\n", u4Idx,
			u4MaxCnt, prRxRing->RxCpuIdx, u4CpuIdx, prRxRing->RxDmaIdx, u4DmaIdx, prRxRing->RxSwReadIdx);
	}

	DBGLOG(SW4, INFO, "MSDU Tok: Free[%u] Used[%u]\n", halGetMsduTokenFreeCnt(prGlueInfo->prAdapter),
		prGlueInfo->rHifInfo.rTokenInfo.i4UsedCnt);
	DBGLOG(SW4, INFO, "Pending QLen Normal[%u] Sec[%u]\n",
		prGlueInfo->i4TxPendingFrameNum, prGlueInfo->i4TxPendingSecurityFrameNum);

	DBGLOG(SW4, INFO, "---------------------------------\n\n");
}


BOOLEAN halIsStaticMapBusAddr(IN UINT_32 u4Addr)
{
	if (u4Addr < MAX_PCIE_BUS_STATIC_MAP_ADDR)
		return TRUE;
	else
		return FALSE;
}

BOOLEAN halChipToStaticMapBusAddr(IN UINT_32 u4ChipAddr, OUT PUINT_32 pu4BusAddr)
{
	UINT_32 u4StartAddr, u4EndAddr, u4BusAddr;
	UINT_32 u4Idx = 0;

	if (halIsStaticMapBusAddr(u4ChipAddr)) {
		*pu4BusAddr = u4ChipAddr;
		return TRUE;
	}

	while (TRUE) {
		u4StartAddr = arBus2ChipCrMapping[u4Idx].u4ChipAddr;
		u4EndAddr = arBus2ChipCrMapping[u4Idx].u4ChipAddr + arBus2ChipCrMapping[u4Idx].u4Range;

		/* End of mapping table */
		if (u4EndAddr == 0x0) {
			return FALSE;
		}

		if ((u4ChipAddr >= u4StartAddr) && (u4ChipAddr <= u4EndAddr)) {
			u4BusAddr = (u4ChipAddr - u4StartAddr) + arBus2ChipCrMapping[u4Idx].u4BusAddr;
			break;
		}

		u4Idx++;
	}

	*pu4BusAddr = u4BusAddr;
	return TRUE;
}

BOOLEAN halGetDynamicMapReg(IN P_GL_HIF_INFO_T prHifInfo, IN UINT_32 u4ChipAddr, OUT PUINT_32 pu4Value)
{
	UINT_32 u4ReMapReg, u4BusAddr;
	ULONG flags;

	if (!halChipToStaticMapBusAddr(MCU_CFG_PCIE_REMAP2, &u4ReMapReg))
		return FALSE;

	spin_lock_irqsave(&prHifInfo->rDynMapRegLock, flags);

	RTMP_IO_WRITE32(prHifInfo, u4ReMapReg, u4ChipAddr & PCIE_REMAP2_MASK);
	u4BusAddr = PCIE_REMAP2_BUS_ADDR + (u4ChipAddr & ~PCIE_REMAP2_MASK);
	RTMP_IO_READ32(prHifInfo, u4BusAddr, pu4Value);

	spin_unlock_irqrestore(&prHifInfo->rDynMapRegLock, flags);

	return TRUE;
}

BOOLEAN halSetDynamicMapReg(IN P_GL_HIF_INFO_T prHifInfo, IN UINT_32 u4ChipAddr, IN UINT_32 u4Value)
{
	UINT_32 u4ReMapReg, u4BusAddr;
	ULONG flags;

	if (!halChipToStaticMapBusAddr(MCU_CFG_PCIE_REMAP2, &u4ReMapReg))
		return FALSE;

	spin_lock_irqsave(&prHifInfo->rDynMapRegLock, flags);

	RTMP_IO_WRITE32(prHifInfo, u4ReMapReg, u4ChipAddr & PCIE_REMAP2_MASK);
	u4BusAddr = PCIE_REMAP2_BUS_ADDR + (u4ChipAddr & ~PCIE_REMAP2_MASK);
	RTMP_IO_WRITE32(prHifInfo, u4BusAddr, u4Value);

	spin_unlock_irqrestore(&prHifInfo->rDynMapRegLock, flags);

	return TRUE;
}

