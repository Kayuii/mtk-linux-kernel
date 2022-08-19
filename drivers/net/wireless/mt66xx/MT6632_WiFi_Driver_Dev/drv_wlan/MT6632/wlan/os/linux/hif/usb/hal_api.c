/******************************************************************************
*[File]             hif_api.c
*[Version]          v1.0
*[Revision Date]    2015-09-08
*[Author]
*[Description]
*    The program provides USB HIF APIs
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

#include <linux/usb.h>
#include <linux/mutex.h>

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
static const UINT_16 arTcToUSBEP[TC_NUM] = {
	USB_DATA_BULK_OUT_EP5,
	USB_DATA_BULK_OUT_EP4,
	USB_DATA_BULK_OUT_EP6,
	USB_DATA_BULK_OUT_EP7,
	USB_DATA_BULK_OUT_EP8,
	USB_DATA_BULK_OUT_EP4,

	/* Second HW queue */
#if NIC_TX_ENABLE_SECOND_HW_QUEUE
	USB_DATA_BULK_OUT_EP9,
	USB_DATA_BULK_OUT_EP9,
	USB_DATA_BULK_OUT_EP9,
	USB_DATA_BULK_OUT_EP9,
#endif
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

	prAdapter->eChipIdx = MTK_WIFI_CHIP_MAX;
	prChipInfo = g_prMTKWifiChipInfo;

	HAL_MCR_RD(prAdapter, TOP_HCR, &u4CIR);
	DBGLOG(INIT, TRACE, "Chip ID: 0x%lx\n", u4CIR);

	for (i = 0; i < MTK_WIFI_CHIP_MAX; i++) {
		if (u4CIR == prChipInfo->chip_id) {
			prAdapter->eChipIdx = i;
			break;
		}
		prChipInfo++;
	}

	if (i >= MTK_WIFI_CHIP_MAX)
		return FALSE;

	HAL_MCR_RD(prAdapter, TOP_HVR, &u4CIR);
	DBGLOG(INIT, TRACE, "Revision ID: 0x%lx\n", u4CIR);

	prAdapter->ucRevID = (UINT_8) (u4CIR & 0xF);
	prAdapter->fgIsReadRevID = TRUE;
	return TRUE;
}

WLAN_STATUS
halRxWaitResponse(IN P_ADAPTER_T prAdapter, IN UINT_8 ucPortIdx, OUT PUINT_8 pucRspBuffer,
		  IN UINT_32 u4MaxRespBufferLen, OUT PUINT_32 pu4Length)
{
	WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;
	P_RX_CTRL_T prRxCtrl;

	DEBUGFUNC("halRxWaitResponse");

	ASSERT(prAdapter);
	ASSERT(pucRspBuffer);

	prRxCtrl = &prAdapter->rRxCtrl;

	HAL_PORT_RD(prAdapter, USB_EVENT_EP_IN, ALIGN_4(u4MaxRespBufferLen) + LEN_USB_RX_PADDING_CSO,
		prRxCtrl->pucRxCoalescingBufPtr, CFG_RX_COALESCING_BUFFER_SIZE);
	kalMemCopy(pucRspBuffer, prRxCtrl->pucRxCoalescingBufPtr, u4MaxRespBufferLen);
	*pu4Length = u4MaxRespBufferLen;

	return u4Status;
}

WLAN_STATUS halTxUSBSendCmd(IN P_GLUE_INFO_T prGlueInfo, IN UINT_8 ucTc, IN P_CMD_INFO_T prCmdInfo)
{
	P_GL_HIF_INFO_T prHifInfo = &prGlueInfo->rHifInfo;
	WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;
	P_USB_REQ_T prUsbReq;
	P_BUF_CTRL_T prBufCtrl;
	UINT_16 u2OverallBufferLength = 0;
	unsigned long flags;

	prUsbReq = glUsbDequeueReq(prHifInfo, &prHifInfo->rTxCmdFreeQ);
	if (prUsbReq == NULL)
		return WLAN_STATUS_RESOURCES;

	prBufCtrl = prUsbReq->prBufCtrl;

	if ((TFCB_FRAME_PAD_TO_DW(prCmdInfo->u4TxdLen + prCmdInfo->u4TxpLen) + LEN_USB_UDMA_TX_TERMINATOR) >
	    prBufCtrl->u4BufSize) {
		DBGLOG(HAL, ERROR, "Command TX buffer underflow!\n");
		return WLAN_STATUS_RESOURCES;
	}
	if (prCmdInfo->u4TxdLen) {
		memcpy((prBufCtrl->pucBuf + u2OverallBufferLength), prCmdInfo->pucTxd, prCmdInfo->u4TxdLen);
		u2OverallBufferLength += prCmdInfo->u4TxdLen;
	}

	if (prCmdInfo->u4TxpLen) {
		memcpy((prBufCtrl->pucBuf + u2OverallBufferLength), prCmdInfo->pucTxp, prCmdInfo->u4TxpLen);
		u2OverallBufferLength += prCmdInfo->u4TxpLen;
	}

	memset(prBufCtrl->pucBuf + u2OverallBufferLength, 0,
	       ((TFCB_FRAME_PAD_TO_DW(u2OverallBufferLength) - u2OverallBufferLength) + LEN_USB_UDMA_TX_TERMINATOR));
	prBufCtrl->u4WrIdx = TFCB_FRAME_PAD_TO_DW(u2OverallBufferLength) + LEN_USB_UDMA_TX_TERMINATOR;

	prUsbReq->prPriv = (PVOID) prCmdInfo;
	usb_fill_bulk_urb(prUsbReq->prUrb,
			  prHifInfo->udev,
			  usb_sndbulkpipe(prHifInfo->udev, arTcToUSBEP[ucTc]),
			  (void *)prUsbReq->prBufCtrl->pucBuf,
			  prBufCtrl->u4WrIdx, halTxUSBSendCmdComplete, (void *)prUsbReq);

	prUsbReq->prUrb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	spin_lock_irqsave(&prHifInfo->rQLock, flags);
	u4Status = usb_submit_urb(prUsbReq->prUrb, GFP_ATOMIC);
	if (u4Status) {
		DBGLOG(HAL, ERROR, "usb_submit_urb() reports error (%d)\n", u4Status);
		/* glUsbEnqueueReq(prHifInfo, &prHifInfo->rTxCmdFreeQ, prUsbReq); */
		list_add_tail(&prUsbReq->list, &prHifInfo->rTxCmdFreeQ);
		spin_unlock_irqrestore(&prHifInfo->rQLock, flags);
		return WLAN_STATUS_FAILURE;
	}

	/* glUsbEnqueueReq(prHifInfo, &prHifInfo->rTxCmdSendingQ, prUsbReq); */
	list_add_tail(&prUsbReq->list, &prHifInfo->rTxCmdSendingQ);
	spin_unlock_irqrestore(&prHifInfo->rQLock, flags);
	return u4Status;
}

VOID halTxUSBSendCmdComplete(struct urb *urb)
{
	P_USB_REQ_T prUsbReq = urb->context;
	P_CMD_INFO_T prCmdInfo = (P_CMD_INFO_T) prUsbReq->prPriv;
	P_GL_HIF_INFO_T prHifInfo = prUsbReq->prHifInfo;
	P_GLUE_INFO_T prGlueInfo = prHifInfo->prGlueInfo;
	P_ADAPTER_T prAdapter = prGlueInfo->prAdapter;
	unsigned long flags;

	spin_lock_irqsave(&prHifInfo->rQLock, flags);
	list_del_init(&prUsbReq->list);
	list_add_tail(&prUsbReq->list, &prHifInfo->rTxCmdFreeQ);
	spin_unlock_irqrestore(&prHifInfo->rQLock, flags);

	if (urb->status != 0) {
		DBGLOG(TX, ERROR, "[%s] send CMD fail (status = %d)\n", __func__, urb->status);
		/* TODO: handle error */
	}

	DBGLOG(INIT, INFO, "TX CMD DONE: ID[0x%02X] SEQ[%u]\n", prCmdInfo->ucCID, prCmdInfo->ucCmdSeqNum);

	/* glUsbEnqueueReq(prHifInfo, &prHifInfo->rTxCmdFreeQ, prUsbReq); */

	if (prCmdInfo->pfHifTxCmdDoneCb)
		prCmdInfo->pfHifTxCmdDoneCb(prAdapter, prCmdInfo);

	nicTxReleaseResource(prAdapter, TC4_INDEX, nicTxGetCmdPageCount(prCmdInfo), TRUE);

}

VOID halTxCancelSendingCmd(IN P_ADAPTER_T prAdapter, IN P_CMD_INFO_T prCmdInfo)
{
	P_USB_REQ_T prUsbReq, prNext;
	unsigned long flags;
	struct urb *urb = NULL;
	P_GL_HIF_INFO_T prHifInfo = &prAdapter->prGlueInfo->rHifInfo;

	spin_lock_irqsave(&prHifInfo->rQLock, flags);
	list_for_each_entry_safe(prUsbReq, prNext, &prHifInfo->rTxCmdSendingQ, list) {
		if (prUsbReq->prPriv == (PVOID) prCmdInfo) {
			list_del_init(&prUsbReq->list);
			urb = prUsbReq->prUrb;
			break;
		}
	}
	spin_unlock_irqrestore(&prHifInfo->rQLock, flags);

	if (urb) {
		prCmdInfo->pfHifTxCmdDoneCb = NULL;
		usb_kill_urb(urb);
	}
}

#if CFG_USB_TX_AGG
WLAN_STATUS halTxUSBSendAggData(IN P_GL_HIF_INFO_T prHifInfo, IN UINT_8 ucTc, IN P_USB_REQ_T prUsbReq)
{
	P_BUF_CTRL_T prBufCtrl = prUsbReq->prBufCtrl;
	WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;

	memset(prBufCtrl->pucBuf + prBufCtrl->u4WrIdx, 0, LEN_USB_UDMA_TX_TERMINATOR);
	prBufCtrl->u4WrIdx += LEN_USB_UDMA_TX_TERMINATOR;

	list_del_init(&prUsbReq->list);

	usb_fill_bulk_urb(prUsbReq->prUrb,
			  prHifInfo->udev,
			  usb_sndbulkpipe(prHifInfo->udev, arTcToUSBEP[ucTc]),
			  (void *)prBufCtrl->pucBuf, prBufCtrl->u4WrIdx, halTxUSBSendDataComplete, (void *)prUsbReq);
	prUsbReq->prUrb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	u4Status = usb_submit_urb(prUsbReq->prUrb, GFP_ATOMIC);
	if (u4Status) {
		DBGLOG(HAL, ERROR, "usb_submit_urb() reports error (%d) [%s]\n", u4Status, __func__);
		list_add_tail(&prUsbReq->list, &prHifInfo->rTxDataFreeQ[ucTc]);
		return WLAN_STATUS_FAILURE;
	}

	list_add_tail(&prUsbReq->list, &prHifInfo->rTxDataSendingQ[ucTc]);

	return u4Status;
}
#endif

WLAN_STATUS halTxUSBSendData(IN P_GLUE_INFO_T prGlueInfo, IN P_MSDU_INFO_T prMsduInfo)
{
	P_GL_HIF_INFO_T prHifInfo = &prGlueInfo->rHifInfo;
	WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;
	P_USB_REQ_T prUsbReq;
	P_BUF_CTRL_T prBufCtrl;
	UINT_32 u4PaddingLength;
	struct sk_buff *skb;
	UINT_8 ucTc;
	UINT_8 *pucBuf;
	UINT_32 u4Length;
	unsigned long flags;

	skb = (struct sk_buff *)prMsduInfo->prPacket;
	pucBuf = skb->data;
	u4Length = skb->len;
	ucTc = prMsduInfo->ucTC;
#if CFG_USB_TX_AGG
	spin_lock_irqsave(&prHifInfo->rQLock, flags);

	if (list_empty(&prHifInfo->rTxDataFreeQ[ucTc])) {
		spin_unlock_irqrestore(&prHifInfo->rQLock, flags);
		DBGLOG(HAL, ERROR, "overflow BUG!!\n");
		return WLAN_STATUS_RESOURCES;
	}
	prUsbReq = list_entry(prHifInfo->rTxDataFreeQ[ucTc].next, struct _USB_REQ_T, list);
	prBufCtrl = prUsbReq->prBufCtrl;

	if (prBufCtrl->u4WrIdx + ALIGN_4(u4Length) + LEN_USB_UDMA_TX_TERMINATOR > prBufCtrl->u4BufSize) {
		halTxUSBSendAggData(prHifInfo, ucTc, prUsbReq);

		if (list_empty(&prHifInfo->rTxDataFreeQ[ucTc])) {
			spin_unlock_irqrestore(&prHifInfo->rQLock, flags);
			DBGLOG(HAL, ERROR, "overflow BUG!!\n");
			return WLAN_STATUS_FAILURE;
		}

		prUsbReq = list_entry(prHifInfo->rTxDataFreeQ[ucTc].next, struct _USB_REQ_T, list);
		prBufCtrl = prUsbReq->prBufCtrl;
	}

	memcpy(prBufCtrl->pucBuf + prBufCtrl->u4WrIdx, pucBuf, u4Length);
	prBufCtrl->u4WrIdx += u4Length;

	u4PaddingLength = (ALIGN_4(u4Length) - u4Length);
	if (u4PaddingLength) {
		memset(prBufCtrl->pucBuf + prBufCtrl->u4WrIdx, 0, u4PaddingLength);
		prBufCtrl->u4WrIdx += u4PaddingLength;
	}

	if (!prMsduInfo->pfTxDoneHandler)
		QUEUE_INSERT_TAIL(&prUsbReq->rSendingDataMsduInfoList, (P_QUE_ENTRY_T) prMsduInfo);

	if (list_empty(&prHifInfo->rTxDataSendingQ[ucTc]))
		halTxUSBSendAggData(prHifInfo, ucTc, prUsbReq);

	spin_unlock_irqrestore(&prHifInfo->rQLock, flags);
#else
	prUsbReq = glUsbDequeueReq(prHifInfo, &prHifInfo->rTxDataFreeQ);
	if (prUsbReq == NULL)
		return WLAN_STATUS_RESOURCES;

	prBufCtrl = prUsbReq->prBufCtrl;
	prBufCtrl->u4WrIdx = 0;

	memcpy(prBufCtrl->pucBuf, pucBuf, u4Length);
	prBufCtrl->u4WrIdx += u4Length;

	u4PaddingLength = (ALIGN_4(u4Length) - u4Length);
	if (u4PaddingLength) {
		memset(prBufCtrl->pucBuf + prBufCtrl->u4WrIdx, 0, u4PaddingLength);
		prBufCtrl->u4WrIdx += u4PaddingLength;
	}

	memset(prBufCtrl->pucBuf + prBufCtrl->u4WrIdx, 0, LEN_USB_UDMA_TX_TERMINATOR);
	prBufCtrl->u4WrIdx += LEN_USB_UDMA_TX_TERMINATOR;

	if (!prMsduInfo->pfTxDoneHandler)
		QUEUE_INSERT_TAIL(&prUsbReq->rSendingDataMsduInfoList, (P_QUE_ENTRY_T) prMsduInfo);

	*((PUINT_8)&prUsbReq->prPriv) = ucTc;
	usb_fill_bulk_urb(prUsbReq->prUrb,
			  prHifInfo->udev,
			  usb_sndbulkpipe(prHifInfo->udev, arTcToUSBEP[ucTc]),
			  (void *)prUsbReq->prBufCtrl->pucBuf,
			  prBufCtrl->u4WrIdx, halTxUSBSendDataComplete, (void *)prUsbReq);
	prUsbReq->prUrb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	u4Status = usb_submit_urb(prUsbReq->prUrb, GFP_ATOMIC);
	if (u4Status)
		DBGLOG(HAL, ERROR, "usb_submit_urb() reports error (%d) [%s]\n", u4Status, __func__);
#endif

	return u4Status;
}

VOID halTxUSBSendDataComplete(struct urb *urb)
{
	QUE_T rFreeQueue;
	P_QUE_T prFreeQueue;
	P_USB_REQ_T prUsbReq = urb->context;
	UINT_8 ucTc;
	P_GL_HIF_INFO_T prHifInfo = prUsbReq->prHifInfo;
	P_GLUE_INFO_T prGlueInfo = prHifInfo->prGlueInfo;
	P_ADAPTER_T prAdapter = prGlueInfo->prAdapter;
	P_BUF_CTRL_T prBufCtrl = prUsbReq->prBufCtrl;
	UINT_32 u4SentDataSize;
	unsigned long flags;

	ucTc = *((PUINT_8)&prUsbReq->prPriv);

	if (urb->status != 0) {
		DBGLOG(TX, ERROR, "[%s] send DATA fail (status = %d)\n", __func__, urb->status);
		/* TODO: handle error */
	}

	prFreeQueue = &rFreeQueue;
	QUEUE_INITIALIZE(prFreeQueue);
	QUEUE_MOVE_ALL((prFreeQueue), (&(prUsbReq->rSendingDataMsduInfoList)));
	if (g_pfTxDataDoneCb)
		g_pfTxDataDoneCb(prGlueInfo, prFreeQueue);
#if 0
	GLUE_DEC_REF_CNT(prGlueInfo->i4TxPendingFrameNum);
	GLUE_DEC_REF_CNT(prGlueInfo->ai4TxPendingFrameNumPerQueue[ucBssIndex][u2QueueIdx]);
#endif

	u4SentDataSize = urb->actual_length - LEN_USB_UDMA_TX_TERMINATOR;

	nicTxReleaseResource(prAdapter, ucTc, nicTxGetPageCount(u4SentDataSize, TRUE), TRUE);

#if CFG_USB_TX_AGG
	prBufCtrl->u4WrIdx = 0;

	spin_lock_irqsave(&prHifInfo->rQLock, flags);

	list_del_init(&prUsbReq->list);
	list_add_tail(&prUsbReq->list, &prHifInfo->rTxDataFreeQ[ucTc]);

	if (list_empty(&prHifInfo->rTxDataSendingQ[ucTc])) {
		prUsbReq = list_entry(prHifInfo->rTxDataFreeQ[ucTc].next, struct _USB_REQ_T, list);
		prBufCtrl = prUsbReq->prBufCtrl;

		if (prBufCtrl->u4WrIdx != 0)
			halTxUSBSendAggData(prHifInfo, ucTc, prUsbReq);	/* TODO */
	}

	spin_unlock_irqrestore(&prHifInfo->rQLock, flags);
#else
	glUsbEnqueueReq(prHifInfo, &prHifInfo->rTxDataFreeQ, prUsbReq);
#endif

	if (kalGetTxPendingCmdCount(prAdapter->prGlueInfo) > 0 || wlanGetTxPendingFrameCount(prAdapter) > 0)
		kalSetEvent(prAdapter->prGlueInfo);
	kalSetTxEvent2Hif(prAdapter->prGlueInfo);
}

VOID halRxUSBEnqueueRFB(IN P_ADAPTER_T prAdapter, IN UINT_8 *pucBuf, IN UINT_32 u4Length)
{
	P_RX_CTRL_T prRxCtrl = &prAdapter->rRxCtrl;
	P_SW_RFB_T prSwRfb = (P_SW_RFB_T) NULL;
	P_HW_MAC_RX_DESC_T prRxStatus;
	UINT_32 u4RemainCount;
	UINT_16 u2RxByteCount;
	UINT_8 *pucRxFrame;
	OS_SYSTIME rLogTime;

	KAL_SPIN_LOCK_DECLARATION();

	pucRxFrame = pucBuf;
	u4RemainCount = u4Length;
	while (u4RemainCount > 4) {
		u2RxByteCount = HAL_RX_STATUS_GET_RX_BYTE_CNT((P_HW_MAC_RX_DESC_T) pucRxFrame);
		u2RxByteCount = ALIGN_4(u2RxByteCount) + LEN_USB_RX_PADDING_CSO;

		if (u2RxByteCount <= CFG_RX_MAX_PKT_SIZE) {
			prSwRfb = NULL;
			GET_CURRENT_SYSTIME(&rLogTime);

			while (!prSwRfb) {
				KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_FREE_QUE);
				QUEUE_REMOVE_HEAD(&prRxCtrl->rFreeSwRfbList, prSwRfb, P_SW_RFB_T);
				KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_FREE_QUE);

				if (!prSwRfb && CHECK_FOR_TIMEOUT(kalGetTimeTick(), rLogTime, SW_RFB_TIMEOUT_MS)) {
					GET_CURRENT_SYSTIME(&rLogTime);
					DBGLOG(RX, WARN, "Out of SwRfb for %ums!\n", SW_RFB_TIMEOUT_MS);
				}
			}

			kalMemCopy(prSwRfb->pucRecvBuff, pucRxFrame, u2RxByteCount);

			prRxStatus = prSwRfb->prRxStatus;
			ASSERT(prRxStatus);

			prSwRfb->ucPacketType = (UINT_8) HAL_RX_STATUS_GET_PKT_TYPE(prRxStatus);
			/* DBGLOG(RX, TRACE, ("ucPacketType = %d\n", prSwRfb->ucPacketType)); */
#if DBG
			DBGLOG(RX, INFO, "Rx status flag = %x wlan index = %d SecMode = %d\n",
			       prRxStatus->u2StatusFlag, prRxStatus->ucWlanIdx, HAL_RX_STATUS_GET_SEC_MODE(prRxStatus));
#endif

			KAL_ACQUIRE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
			QUEUE_INSERT_TAIL(&prRxCtrl->rReceivedRfbList, &prSwRfb->rQueEntry);
			RX_INC_CNT(prRxCtrl, RX_MPDU_TOTAL_COUNT);
			KAL_RELEASE_SPIN_LOCK(prAdapter, SPIN_LOCK_RX_QUE);
		} else {
			DBGLOG(RX, WARN, "Rx byte count:%u exceeds SW_RFB max length:%u\n!",
				u2RxByteCount, CFG_RX_MAX_PKT_SIZE);
			DBGLOG_MEM32(RX, WARN, pucRxFrame, sizeof(HW_MAC_RX_DESC_T));
		}

		u4RemainCount -= u2RxByteCount;
		pucRxFrame += u2RxByteCount;
	}

	set_bit(GLUE_FLAG_RX_BIT, &(prAdapter->prGlueInfo->ulFlag));
	wake_up_interruptible(&(prAdapter->prGlueInfo->waitq));
}

WLAN_STATUS halRxUSBReceiveEvent(IN P_ADAPTER_T prAdapter)
{
	P_GLUE_INFO_T prGlueInfo = prAdapter->prGlueInfo;
	P_GL_HIF_INFO_T prHifInfo = &prGlueInfo->rHifInfo;
	P_USB_REQ_T prUsbReq;
	WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;

	if (prGlueInfo->ulFlag & GLUE_FLAG_HALT)
		return WLAN_STATUS_FAILURE;

	while (1) {
		prUsbReq = glUsbDequeueReq(prHifInfo, &prHifInfo->rRxEventFreeQ);
		if (prUsbReq == NULL)
			return WLAN_STATUS_RESOURCES;

		prUsbReq->prPriv = NULL;
		usb_fill_bulk_urb(prUsbReq->prUrb,
				  prHifInfo->udev,
				  usb_rcvbulkpipe(prHifInfo->udev, USB_EVENT_EP_IN),
				  (void *)prUsbReq->prBufCtrl->pucBuf,
				  prUsbReq->prBufCtrl->u4BufSize, halRxUSBReceiveEventComplete, (void *)prUsbReq);
		u4Status = usb_submit_urb(prUsbReq->prUrb, GFP_ATOMIC);
		if (u4Status)
			DBGLOG(HAL, ERROR, "usb_submit_urb() reports error (%d) [%s]\n", u4Status, __func__);
	}

	return u4Status;
}

VOID halRxUSBReceiveEventComplete(struct urb *urb)
{
	P_USB_REQ_T prUsbReq = urb->context;
	P_GL_HIF_INFO_T prHifInfo = prUsbReq->prHifInfo;
	P_GLUE_INFO_T prGlueInfo = prHifInfo->prGlueInfo;
	P_ADAPTER_T prAdapter = prGlueInfo->prAdapter;

	if (prGlueInfo->ulFlag & GLUE_FLAG_HALT)
		return;

	if (urb->status == -ESHUTDOWN) {
		glUsbEnqueueReq(prHifInfo, &prHifInfo->rRxEventFreeQ, prUsbReq);
		DBGLOG(RX, ERROR, "USB device shutdown skip Rx [%s]\n", __func__);
		return;
	}

	if (urb->status == 0)
		halRxUSBEnqueueRFB(prAdapter, prUsbReq->prBufCtrl->pucBuf, urb->actual_length);
	else
		DBGLOG(RX, ERROR, "[%s] receive EVENT fail (status = %d)\n", __func__, urb->status);

	glUsbEnqueueReq(prHifInfo, &prHifInfo->rRxEventFreeQ, prUsbReq);

	halRxUSBReceiveEvent(prAdapter);
}

WLAN_STATUS halRxUSBReceiveData(IN P_ADAPTER_T prAdapter)
{
	P_GLUE_INFO_T prGlueInfo = prAdapter->prGlueInfo;
	P_GL_HIF_INFO_T prHifInfo = &prGlueInfo->rHifInfo;
	P_USB_REQ_T prUsbReq;
	WLAN_STATUS u4Status = WLAN_STATUS_SUCCESS;

	if (prGlueInfo->ulFlag & GLUE_FLAG_HALT)
		return WLAN_STATUS_FAILURE;

	while (1) {
		prUsbReq = glUsbDequeueReq(prHifInfo, &prHifInfo->rRxDataFreeQ);
		if (prUsbReq == NULL)
			return WLAN_STATUS_RESOURCES;

		prUsbReq->prPriv = NULL;
		usb_fill_bulk_urb(prUsbReq->prUrb,
				  prHifInfo->udev,
				  usb_rcvbulkpipe(prHifInfo->udev, USB_DATA_EP_IN),
				  (void *)prUsbReq->prBufCtrl->pucBuf,
				  prUsbReq->prBufCtrl->u4BufSize, halRxUSBReceiveDataComplete, (void *)prUsbReq);
		u4Status = usb_submit_urb(prUsbReq->prUrb, GFP_ATOMIC);
		if (u4Status)
			DBGLOG(HAL, ERROR, "usb_submit_urb() reports error (%d) [%s]\n", u4Status, __func__);
	}

	return u4Status;
}

VOID halRxUSBReceiveDataComplete(struct urb *urb)
{
	P_USB_REQ_T prUsbReq = urb->context;
	P_GL_HIF_INFO_T prHifInfo = prUsbReq->prHifInfo;
	P_GLUE_INFO_T prGlueInfo = prHifInfo->prGlueInfo;
	P_ADAPTER_T prAdapter = prGlueInfo->prAdapter;

	if (prGlueInfo->ulFlag & GLUE_FLAG_HALT)
		return;

	if (urb->status == -ESHUTDOWN) {
		glUsbEnqueueReq(prHifInfo, &prHifInfo->rRxDataFreeQ, prUsbReq);
		DBGLOG(RX, ERROR, "USB device shutdown skip Rx [%s]\n", __func__);
		return;
	}

	if (urb->status == 0)
		halRxUSBEnqueueRFB(prAdapter, prUsbReq->prBufCtrl->pucBuf, urb->actual_length);
	else
		DBGLOG(RX, ERROR, "[%s] receive EVENT fail (status = %d)\n", __func__, urb->status);

	glUsbEnqueueReq(prHifInfo, &prHifInfo->rRxDataFreeQ, prUsbReq);

	halRxUSBReceiveData(prAdapter);
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
	halRxUSBReceiveEvent(prAdapter);
	halRxUSBReceiveData(prAdapter);
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
	return TRUE;
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
}

VOID halWakeUpWiFi(IN P_ADAPTER_T prAdapter)
{
	BOOLEAN fgResult;
	UINT_8 ucCount = 0;

	DBGLOG(INIT, INFO, "Power on Wi-Fi....\n");

	HAL_WIFI_FUNC_READY_CHECK(prAdapter, WIFI_FUNC_INIT_DONE, &fgResult);

	while (!fgResult) {
		HAL_WIFI_FUNC_POWER_ON(prAdapter);
		kalMdelay(50);
		HAL_WIFI_FUNC_READY_CHECK(prAdapter, WIFI_FUNC_INIT_DONE, &fgResult);

		ucCount++;

		if (ucCount >= 5) {
			DBGLOG(INIT, WARN, "Power on failed!!!\n");
			break;
		}
	}

	prAdapter->fgIsFwOwn = FALSE;
}

VOID halEnableFWDownload(IN P_ADAPTER_T prAdapter, IN BOOL fgEnable)
{
#if (CFG_UMAC_GENERATION >= 0x20)

	UINT_32 u4Value = 0;

	ASSERT(prAdapter);

	{
		HAL_MCR_RD(prAdapter, UDMA_TX_QSEL, &u4Value);

		if (fgEnable)
			u4Value |= FW_DL_EN;
		else
			u4Value &= ~FW_DL_EN;

		HAL_MCR_WR(prAdapter, UDMA_TX_QSEL, u4Value);
	}
#endif
}

VOID halDevInit(IN P_ADAPTER_T prAdapter)
{
	UINT_32 u4Value = 0;

	ASSERT(prAdapter);

	{
		HAL_MCR_RD(prAdapter, UDMA_WLCFG_0, &u4Value);

		/* enable UDMA TX & RX */
		u4Value = UDMA_WLCFG_0_TX_EN(1) | UDMA_WLCFG_0_RX_EN(1) |
#if 0
		    UDMA_WLCFG_0_RX_AGG_EN(1) |
#else
		    UDMA_WLCFG_0_RX_AGG_EN(0) |
#endif
		    UDMA_WLCFG_0_RX_AGG_LMT(USB_RX_AGGREGTAION_LIMIT) |
		    UDMA_WLCFG_0_RX_AGG_TO(USB_RX_AGGREGTAION_TIMEOUT);

		HAL_MCR_WR(prAdapter, UDMA_WLCFG_0, u4Value);
	}
}

BOOLEAN halTxIsDataBufEnough(IN P_ADAPTER_T prAdapter, IN UINT_8 ucTc, IN UINT_32 u4Length)
{
	P_GLUE_INFO_T prGlueInfo = prAdapter->prGlueInfo;
	P_GL_HIF_INFO_T prHifInfo = &prGlueInfo->rHifInfo;
	P_USB_REQ_T prUsbReq;
	P_BUF_CTRL_T prBufCtrl;

	unsigned long flags;

	spin_lock_irqsave(&prHifInfo->rQLock, flags);

	if (list_empty(&prHifInfo->rTxDataFreeQ[ucTc])) {
		spin_unlock_irqrestore(&prHifInfo->rQLock, flags);

		return FALSE;
	}

	prUsbReq = list_entry(prHifInfo->rTxDataFreeQ[ucTc].next, struct _USB_REQ_T, list);
	prBufCtrl = prUsbReq->prBufCtrl;

	if (prBufCtrl->u4WrIdx + ALIGN_4(u4Length) + LEN_USB_UDMA_TX_TERMINATOR > prBufCtrl->u4BufSize) {
		if (list_empty(&prHifInfo->rTxDataFreeQ[ucTc])) {
			spin_unlock_irqrestore(&prHifInfo->rQLock, flags);

			return FALSE;
		}
	}

	spin_unlock_irqrestore(&prHifInfo->rQLock, flags);
	return TRUE;
}

VOID halProcessTxInterrupt(IN P_ADAPTER_T prAdapter)
{

}

VOID halHifSwInfoInit(IN P_ADAPTER_T prAdapter)
{

}

VOID halRxProcessMsduReport(IN P_ADAPTER_T prAdapter, IN OUT P_SW_RFB_T prSwRfb)
{

}

UINT_32 halTxGetPageCount(IN UINT_32 u4FrameLength, IN BOOLEAN fgIncludeDesc)
{
#if CFG_USB_TX_AGG
	UINT_32 u4RequiredBufferSize;
	UINT_32 u4PageCount;

	/* Frame Buffer
	 *  |<--Tx Descriptor-->|<--Tx descriptor padding-->|
	 *  <--802.3/802.11 Header-->|<--Header padding-->|<--Payload-->|
	 */

	if (fgIncludeDesc) {
		u4RequiredBufferSize = u4FrameLength;
	} else {
		u4RequiredBufferSize = NIC_TX_DESC_LONG_FORMAT_LENGTH + NIC_TX_DESC_PADDING_LENGTH
			+ HW_MAC_TX_DESC_APPEND_T_LENGTH + u4FrameLength;

		u4RequiredBufferSize = ALIGN_4(u4RequiredBufferSize);
	}

	if (NIC_TX_PAGE_SIZE_IS_POWER_OF_2)
		u4PageCount = (u4RequiredBufferSize + (NIC_TX_PAGE_SIZE - 1)) >> NIC_TX_PAGE_SIZE_IN_POWER_OF_2;
	else
		u4PageCount = (u4RequiredBufferSize + (NIC_TX_PAGE_SIZE - 1)) / NIC_TX_PAGE_SIZE;

	return u4PageCount;
#else
	return 1;
#endif
}

WLAN_STATUS halTxPollingResource(IN P_ADAPTER_T prAdapter, IN UINT_8 ucTC)
{
	return WLAN_STATUS_SUCCESS;
}

VOID halProcessRxInterrupt(IN P_ADAPTER_T prAdapter)
{

}

VOID halDumpHifStatus(IN P_GLUE_INFO_T prGlueInfo)
{

}

