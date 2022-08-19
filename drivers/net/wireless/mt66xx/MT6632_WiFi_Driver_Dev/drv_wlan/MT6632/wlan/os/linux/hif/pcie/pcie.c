/******************************************************************************
*[File]             pcie.c
*[Version]          v1.0
*[Revision Date]    2010-03-01
*[Author]
*[Description]
*    The program provides PCIE HIF driver
*[Copyright]
*    Copyright (C) 2010 MediaTek Incorporation. All Rights Reserved.
******************************************************************************/

/*
**
** 10 14 2015 zd.hu
** [BORA00005104] [MT6632 Wi-Fi] Fix coding style.
**	1) Purpose:
**	Fix typos.
**	2) Changed function name:
**	Files under include/, os/linux/hif and os/linux/include
**	3) Code change description brief:
**	Fix typos.
**	4) Unit Test Result:
**	build pass, scan OK and connect to a AP OK on kernel 3.11.
**
** 09 30 2015 th3.huang
** [BORA00005104] [MT6632 Wi-Fi] Fix coding style.
** 1 fixed coding style issue by auto tool.
**
** 09 24 2015 litien.chang
** [BORA00005127] MT6632
** [WiFi] usb/sdio/pcie 3 interface integration
**
** 09 22 2015 zd.hu
** [BORA00005104] [MT6632 Wi-Fi] Fix coding style.
**	Use "STRUCT" to avoid reporting typo by checkpatch.pl.
**
**	Test: build pass, scan OK and connect to a AP OK on kernel 3.11.
**
** 08 06 2015 terry.wu
** 1. use defined(_HIF_USB) instead of _HIF_USB
** 2. enable QA tool
** 3. rename register header file to MT6632
**
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#include "gl_os.h"

#include "hif_pci.h"

#include "precomp.h"

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

#define MTK_PCI_VENDOR_ID	0x14C3
#define NIC6632_PCIe_DEVICE_ID	0x6632

static const struct pci_device_id mtk_pci_ids[] = {
	{PCI_DEVICE(MTK_PCI_VENDOR_ID, NIC6632_PCIe_DEVICE_ID)},
	{ /* end: all zeroes */ },
};

MODULE_DEVICE_TABLE(pci, mtk_pci_ids);

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
static probe_card pfWlanProbe;
static remove_card pfWlanRemove;

static struct pci_driver mtk_pci_driver = {
	.name = "wlan",
	.id_table = mtk_pci_ids,
	.probe = NULL,
	.remove = NULL,
};

static BOOLEAN g_fgDriverProbed = FALSE;
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
* \brief This function is a PCIE interrupt callback function
*
* \param[in] func  pointer to PCIE handle
*
* \return void
*/
/*----------------------------------------------------------------------------*/
static PUCHAR CSRBaseAddress;

static irqreturn_t mtk_pci_interrupt(int irq, void *dev_instance)
{
	P_GLUE_INFO_T prGlueInfo = NULL;
	UINT_32 u4RegValue;

	prGlueInfo = (P_GLUE_INFO_T) dev_instance;
	if (!prGlueInfo) {
		DBGLOG(HAL, INFO, "No glue info in mtk_pci_interrupt()\n");
		return IRQ_NONE;
	}

	HAL_MCR_RD(prGlueInfo->prAdapter, WPDMA_INT_STA, &u4RegValue);
	if (!u4RegValue)
		return IRQ_HANDLED;

	halDisableInterrupt(prGlueInfo->prAdapter);

	if (prGlueInfo->ulFlag & GLUE_FLAG_HALT) {
		DBGLOG(HAL, INFO, "GLUE_FLAG_HALT skip INT\n");
		return IRQ_NONE;
	}

	DBGLOG(HAL, TRACE, "%s INT[0x%08x]\n", __func__, u4RegValue);

	set_bit(GLUE_FLAG_INT_BIT, &prGlueInfo->ulFlag);

	/* when we got pci interrupt, we wake up the tx servie thread */
#if CFG_SUPPORT_MULTITHREAD
	wake_up_interruptible(&prGlueInfo->waitq_hif);
#else
	wake_up_interruptible(&prGlueInfo->waitq);
#endif
	return IRQ_HANDLED;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This function is a PCIE probe function
*
* \param[in] func   pointer to PCIE handle
* \param[in] id     pointer to PCIE device id table
*
* \return void
*/
/*----------------------------------------------------------------------------*/
static int mtk_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret = 0;

	ASSERT(pdev);
	ASSERT(id);

	ret = pci_enable_device(pdev);

	if (ret) {
		DBGLOG(INIT, INFO, "pci_enable_device failed!\n");
		goto out;
	}

	DBGLOG(INIT, INFO, "pci_enable_device done!\n");

	if (pfWlanProbe((PVOID) pdev) != WLAN_STATUS_SUCCESS) {
		DBGLOG(INIT, INFO, "pfWlanProbe fail!call pfWlanRemove()\n");
		pfWlanRemove();
		ret = -1;
	} else {
		g_fgDriverProbed = TRUE;
	}
out:
	DBGLOG(INIT, INFO, "mtk_pci_probe() done(%d)\n", ret);

	return ret;
}

static void mtk_pci_remove(struct pci_dev *pdev)
{
	ASSERT(pdev);

	if (g_fgDriverProbed)
		pfWlanRemove();
	DBGLOG(INIT, INFO, "pfWlanRemove done\n");

	/* Unmap CSR base address */
	iounmap(CSRBaseAddress);

	/* release memory region */
	release_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));

	pci_disable_device(pdev);
	DBGLOG(INIT, INFO, "mtk_pci_remove() done\n");
}

static int mtk_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	return 0;
}

int mtk_pci_resume(struct pci_dev *pdev)
{
	return 0;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This function will register pci bus to the os
*
* \param[in] pfProbe    Function pointer to detect card
* \param[in] pfRemove   Function pointer to remove card
*
* \return The result of registering pci bus
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS glRegisterBus(probe_card pfProbe, remove_card pfRemove)
{
	int ret = 0;

	ASSERT(pfProbe);
	ASSERT(pfRemove);

	/* printk(KERN_INFO "mtk_pci: MediaTek PCIE WLAN driver\n"); */
	/* printk(KERN_INFO "mtk_pci: Copyright MediaTek Inc.\n"); */

	pfWlanProbe = pfProbe;
	pfWlanRemove = pfRemove;

	mtk_pci_driver.probe = mtk_pci_probe;
	mtk_pci_driver.remove = mtk_pci_remove;

	mtk_pci_driver.suspend = mtk_pci_suspend;
	mtk_pci_driver.resume = mtk_pci_resume;

	ret = (pci_register_driver(&mtk_pci_driver) == 0) ? WLAN_STATUS_SUCCESS : WLAN_STATUS_FAILURE;

	return ret;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This function will unregister pci bus to the os
*
* \param[in] pfRemove Function pointer to remove card
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID glUnregisterBus(remove_card pfRemove)
{
	if (g_fgDriverProbed) {
		pfRemove();
		g_fgDriverProbed = FALSE;
	}
	pci_unregister_driver(&mtk_pci_driver);
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This function stores hif related info, which is initialized before.
*
* \param[in] prGlueInfo Pointer to glue info structure
* \param[in] u4Cookie   Pointer to UINT_32 memory base variable for _HIF_HPI
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID glSetHifInfo(P_GLUE_INFO_T prGlueInfo, ULONG ulCookie)
{
	P_GL_HIF_INFO_T prHif = NULL;

	prHif = &prGlueInfo->rHifInfo;

	prHif->pdev = (struct pci_dev *)ulCookie;

	prHif->CSRBaseAddress = CSRBaseAddress;

	pci_set_drvdata(prHif->pdev, prGlueInfo);

	SET_NETDEV_DEV(prGlueInfo->prDevHandler, &prHif->pdev->dev);

	halWpdmaAllocRing(prGlueInfo);

	halWpdmaInitRing(prGlueInfo);

	spin_lock_init(&prHif->rDynMapRegLock);
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This function clears hif related info.
*
* \param[in] prGlueInfo Pointer to glue info structure
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID glClearHifInfo(P_GLUE_INFO_T prGlueInfo)
{
	halUninitMsduTokenInfo(prGlueInfo->prAdapter);
	halWpdmaFreeRing(prGlueInfo);
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Initialize bus operation and hif related information, request resources.
*
* \param[out] pvData    A pointer to HIF-specific data type buffer.
*                       For eHPI, pvData is a pointer to UINT_32 type and stores a
*                       mapped base address.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
BOOL glBusInit(PVOID pvData)
{
	int ret = 0;
	struct pci_dev *pdev = NULL;

	ASSERT(pvData);

	pdev = (struct pci_dev *)pvData;

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret != 0) {
		DBGLOG(INIT, INFO, "set DMA mask failed!errno=%d\n", ret);
		return FALSE;
	}

	ret = pci_request_regions(pdev, pci_name(pdev));
	if (ret != 0) {
		DBGLOG(INIT, INFO, "Request PCI resource failed, errno=%d!\n", ret);
		goto err_out;
	}

	/* map physical address to virtual address for accessing register */
	CSRBaseAddress = (PUCHAR) ioremap(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
	DBGLOG(INIT, INFO, "ioremap for device %s, region 0x%lX @ 0x%lX\n",
		pci_name(pdev), (ULONG) pci_resource_len(pdev, 0), (ULONG) pci_resource_start(pdev, 0));
	if (!CSRBaseAddress) {
		DBGLOG(INIT, INFO, "ioremap failed for device %s, region 0x%lX @ 0x%lX\n",
			pci_name(pdev), (ULONG) pci_resource_len(pdev, 0), (ULONG) pci_resource_start(pdev, 0));
		goto err_out_free_res;
	}

	/* Set DMA master */
	pci_set_master(pdev);

	return TRUE;
#if 0
err_out_iounmap:
	iounmap((void *)(CSRBaseAddress));
	release_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
#endif
err_out_free_res:
	pci_release_regions(pdev);

err_out:
	pci_disable_device(pdev);

	return FALSE;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Stop bus operation and release resources.
*
* \param[in] pvData A pointer to struct net_device.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID glBusRelease(PVOID pvData)
{
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Setup bus interrupt operation and interrupt handler for os.
*
* \param[in] pvData     A pointer to struct net_device.
* \param[in] pfnIsr     A pointer to interrupt handler function.
* \param[in] pvCookie   Private data for pfnIsr function.
*
* \retval WLAN_STATUS_SUCCESS   if success
*         NEGATIVE_VALUE   if fail
*/
/*----------------------------------------------------------------------------*/
INT_32 glBusSetIrq(PVOID pvData, PVOID pfnIsr, PVOID pvCookie)
{
	int ret = 0;

	struct net_device *prNetDevice = NULL;
	P_GLUE_INFO_T prGlueInfo = NULL;
	P_GL_HIF_INFO_T prHifInfo = NULL;
	struct pci_dev *pdev = NULL;

	ASSERT(pvData);
	if (!pvData)
		return -1;

	prNetDevice = (struct net_device *)pvData;
	prGlueInfo = (P_GLUE_INFO_T) pvCookie;
	ASSERT(prGlueInfo);
	if (!prGlueInfo)
		return -1;

	prHifInfo = &prGlueInfo->rHifInfo;
	pdev = prHifInfo->pdev;

		 //6PC
	//ret = request_irq(pdev->irq, mtk_pci_interrupt, IRQF_SHARED, prNetDevice->name, prGlueInfo);
    //7623
    ret = request_irq(pdev->irq, mtk_pci_interrupt, IRQF_SHARED | IRQF_TRIGGER_FALLING, prNetDevice->name, prGlueInfo);
	if (ret != 0)
		DBGLOG(INIT, INFO, "glBusSetIrq: request_irq  ERROR(%d)\n", ret);

	return ret;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Stop bus interrupt operation and disable interrupt handling for os.
*
* \param[in] pvData     A pointer to struct net_device.
* \param[in] pvCookie   Private data for pfnIsr function.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
VOID glBusFreeIrq(PVOID pvData, PVOID pvCookie)
{
	struct net_device *prNetDevice = NULL;
	P_GLUE_INFO_T prGlueInfo = NULL;
	P_GL_HIF_INFO_T prHifInfo = NULL;
	struct pci_dev *pdev = NULL;

	ASSERT(pvData);
	if (!pvData) {
		DBGLOG(INIT, INFO, "%s null pvData\n", __func__);
		return;
	}
	prNetDevice = (struct net_device *)pvData;
	prGlueInfo = (P_GLUE_INFO_T) pvCookie;
	ASSERT(prGlueInfo);
	if (!prGlueInfo) {
		DBGLOG(INIT, INFO, "%s no glue info\n", __func__);
		return;
	}

	prHifInfo = &prGlueInfo->rHifInfo;
	pdev = prHifInfo->pdev;

	synchronize_irq(pdev->irq);
	free_irq(pdev->irq, prGlueInfo);
}

BOOLEAN glIsReadClearReg(UINT_32 u4Address)
{
	return TRUE;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Read a 32-bit device register
*
* \param[in] prGlueInfo Pointer to the GLUE_INFO_T structure.
* \param[in] u4Register Register offset
* \param[in] pu4Value   Pointer to variable used to store read value
*
* \retval TRUE          operation success
* \retval FALSE         operation fail
*/
/*----------------------------------------------------------------------------*/
BOOL kalDevRegRead(IN P_GLUE_INFO_T prGlueInfo, IN UINT_32 u4Register, OUT PUINT_32 pu4Value)
{
	P_GL_HIF_INFO_T prHifInfo = NULL;
	UINT_32 u4BusAddr = u4Register;
	BOOLEAN fgResult = TRUE;

	ASSERT(prGlueInfo);
	ASSERT(pu4Value);

	prHifInfo = &prGlueInfo->rHifInfo;

	if (halChipToStaticMapBusAddr(u4Register, &u4BusAddr)) {
		/* Static mapping */
		RTMP_IO_READ32(prHifInfo, u4BusAddr, pu4Value);
	} else {
		/* Dynamic mapping */
		fgResult = halGetDynamicMapReg(prHifInfo, u4BusAddr, pu4Value);
	}

	if ((u4Register & 0xFFFFF000) != PCIE_HIF_BASE)
		DBGLOG(HAL, INFO, "Get CR[0x%08x/0x%08x] value[0x%08x]\n", u4Register, u4BusAddr, *pu4Value);

	return TRUE;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Write a 32-bit device register
*
* \param[in] prGlueInfo Pointer to the GLUE_INFO_T structure.
* \param[in] u4Register Register offset
* \param[in] u4Value    Value to be written
*
* \retval TRUE          operation success
* \retval FALSE         operation fail
*/
/*----------------------------------------------------------------------------*/
BOOL kalDevRegWrite(IN P_GLUE_INFO_T prGlueInfo, IN UINT_32 u4Register, IN UINT_32 u4Value)
{
	P_GL_HIF_INFO_T prHifInfo = NULL;
	UINT_32 u4BusAddr = u4Register;
	BOOLEAN fgResult = TRUE;

	ASSERT(prGlueInfo);

	prHifInfo = &prGlueInfo->rHifInfo;

	if (halChipToStaticMapBusAddr(u4Register, &u4BusAddr)) {
		/* Static mapping */
		RTMP_IO_WRITE32(prHifInfo, u4BusAddr, u4Value);
	} else {
		/* Dynamic mapping */
		fgResult = halSetDynamicMapReg(prHifInfo, u4BusAddr, u4Value);
	}

	if ((u4Register & 0xFFFFF000) != PCIE_HIF_BASE)
		DBGLOG(HAL, INFO, "Set CR[0x%08x/0x%08x] value[0x%08x]\n", u4Register, u4BusAddr, u4Value);

	return TRUE;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Read device I/O port
*
* \param[in] prGlueInfo         Pointer to the GLUE_INFO_T structure.
* \param[in] u2Port             I/O port offset
* \param[in] u2Len              Length to be read
* \param[out] pucBuf            Pointer to read buffer
* \param[in] u2ValidOutBufSize  Length of the buffer valid to be accessed
*
* \retval TRUE          operation success
* \retval FALSE         operation fail
*/
/*----------------------------------------------------------------------------*/
BOOL
kalDevPortRead(IN P_GLUE_INFO_T prGlueInfo, IN UINT_16 u2Port, IN UINT_32 u4Len,
	OUT PUINT_8 pucBuf, IN UINT_32 u4ValidOutBufSize)
{
	P_GL_HIF_INFO_T prHifInfo = NULL;
	PUINT_8 pucDst = NULL;
	RXD_STRUCT *pRxD;
	P_RTMP_RX_RING prRxRing;
	spinlock_t *pRxRingLock;
	PVOID pRxPacket = NULL;
	RTMP_DMACB *pRxCell;
	struct pci_dev *pdev = NULL;
	BOOL fgRet = TRUE;
	ULONG flags = 0;

	ASSERT(prGlueInfo);
	prHifInfo = &prGlueInfo->rHifInfo;

	ASSERT(pucBuf);
	pucDst = pucBuf;

	ASSERT(u4Len <= u4ValidOutBufSize);

	pdev = prHifInfo->pdev;

	prRxRing = &prHifInfo->RxRing[u2Port];
	pRxRingLock = &prHifInfo->RxRingLock[u2Port];

	spin_lock_irqsave(pRxRingLock, flags);

	pRxCell = &prRxRing->Cell[prRxRing->RxSwReadIdx];

	/* Point to Rx indexed rx ring descriptor */
	pRxD = (RXD_STRUCT *) pRxCell->AllocVa;

	if (pRxD->DDONE == 0) {
		/* Get how may packets had been received */
		kalDevRegRead(prGlueInfo, prRxRing->hw_didx_addr, &prRxRing->RxDmaIdx);

		DBGLOG(HAL, TRACE, "Rx DMA done P[%u] DMA[%u] SW_RD[%u]\n", u2Port,
			prRxRing->RxDmaIdx, prRxRing->RxSwReadIdx);
		fgRet = FALSE;
		goto done;
	}

	pRxPacket = pRxCell->pPacket;
	ASSERT(pRxPacket);

	kalMemCopy(pucDst, ((UCHAR *) ((struct sk_buff *)(pRxPacket))->data), pRxD->SDL0);

	pRxD->SDL0 = prRxRing->u4BufSize;
	pRxD->DDONE = 0;

	prRxRing->RxCpuIdx = prRxRing->RxSwReadIdx;
	kalDevRegWrite(prGlueInfo, prRxRing->hw_cidx_addr, prRxRing->RxCpuIdx);
	INC_RING_INDEX(prRxRing->RxSwReadIdx, prRxRing->u4RingSize);

done:
	spin_unlock_irqrestore(pRxRingLock, flags);

	return fgRet;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Write device I/O port
*
* \param[in] prGlueInfo         Pointer to the GLUE_INFO_T structure.
* \param[in] u2Port             I/O port offset
* \param[in] u2Len              Length to be write
* \param[in] pucBuf             Pointer to write buffer
* \param[in] u2ValidInBufSize   Length of the buffer valid to be accessed
*
* \retval TRUE          operation success
* \retval FALSE         operation fail
*/
/*----------------------------------------------------------------------------*/
BOOL
kalDevPortWrite(IN P_GLUE_INFO_T prGlueInfo,
		IN UINT_16 u2Port, IN UINT_32 u4Len, IN PUINT_8 pucBuf, IN UINT_32 u4ValidInBufSize)
{
	P_GL_HIF_INFO_T prHifInfo = NULL;
	PUINT_8 pucSrc = NULL;
	UINT_32 u4SrcLen = u4Len;
	ULONG flags = 0;
	UINT_32 SwIdx = 0;
	P_RTMP_TX_RING prTxRing;
	spinlock_t *prTxRingLock;
	TXD_STRUCT *pTxD;
	struct pci_dev *pdev = NULL;

	ASSERT(prGlueInfo);
	prHifInfo = &prGlueInfo->rHifInfo;

	ASSERT(pucBuf);

	ASSERT(u4Len <= u4ValidInBufSize);

	pucSrc = kalMemAlloc(u4SrcLen, PHY_MEM_TYPE);
	ASSERT(pucSrc);

	kalMemCopy(pucSrc, pucBuf, u4SrcLen);

	pdev = prHifInfo->pdev;
	prTxRing = &prHifInfo->TxRing[u2Port];
	prTxRingLock = &prHifInfo->TxRingLock[u2Port];

	spin_lock_irqsave((spinlock_t *)prTxRingLock, flags);

	SwIdx = prTxRing->TxCpuIdx;
	pTxD = (TXD_STRUCT *) prTxRing->Cell[SwIdx].AllocVa;

	prTxRing->Cell[SwIdx].pPacket = NULL;
	prTxRing->Cell[SwIdx].pBuffer = pucSrc;
	prTxRing->Cell[SwIdx].PacketPa = pci_map_single(pdev, pucSrc, u4SrcLen, PCI_DMA_TODEVICE);

	pTxD->LastSec0 = 1;
	pTxD->LastSec1 = 0;
	pTxD->SDLen0 = u4SrcLen;
	pTxD->SDLen1 = 0;
	pTxD->SDPtr0 = prTxRing->Cell[SwIdx].PacketPa;
	pTxD->SDPtr1 = (UINT_32)NULL;
	pTxD->Burst = 0;
	pTxD->DMADONE = 0;

	/* Increase TX_CTX_IDX, but write to register later. */
	INC_RING_INDEX(prTxRing->TxCpuIdx, TX_RING_SIZE);

	prTxRing->u4UsedCnt++;

	kalDevRegWrite(prGlueInfo, prTxRing->hw_cidx_addr, prTxRing->TxCpuIdx);

	spin_unlock_irqrestore((spinlock_t *)prTxRingLock, flags);

	return TRUE;
}

VOID kalDevReadIntStatus(IN P_ADAPTER_T prAdapter, OUT PUINT_32 pu4IntStatus)
{
	UINT_32 u4RegValue;
	P_GL_HIF_INFO_T prHifInfo = &prAdapter->prGlueInfo->rHifInfo;

	*pu4IntStatus = 0;

	HAL_MCR_RD(prAdapter, WPDMA_INT_STA, &u4RegValue);

	if (HAL_IS_RX_DONE_INTR(u4RegValue))
		*pu4IntStatus |= WHISR_RX0_DONE_INT;

	if (HAL_IS_TX_DONE_INTR(u4RegValue))
		*pu4IntStatus |= WHISR_TX_DONE_INT;

	prHifInfo->u4IntStatus = u4RegValue;

	/* clear interrupt */
	HAL_MCR_WR(prAdapter, WPDMA_INT_STA, u4RegValue);

}

BOOL kalDevWriteCmd(IN P_GLUE_INFO_T prGlueInfo, IN P_CMD_INFO_T prCmdInfo, IN UINT_8 ucTC)
{
	P_GL_HIF_INFO_T prHifInfo = NULL;
	ULONG flags = 0;
	UINT_32 SwIdx = 0;
	P_RTMP_TX_RING prTxRing;
	spinlock_t *prTxRingLock;
	TXD_STRUCT *pTxD;
	struct pci_dev *pdev = NULL;
	UINT_16 u2Port = TX_RING_CMD_IDX_2;
	UINT_32 u4TotalLen;
	PUINT_8 pucSrc = NULL;

	ASSERT(prGlueInfo);
	prHifInfo = &prGlueInfo->rHifInfo;

	pdev = prHifInfo->pdev;
	prTxRing = &prHifInfo->TxRing[u2Port];
	prTxRingLock = &prHifInfo->TxRingLock[u2Port];

	u4TotalLen = prCmdInfo->u4TxdLen + prCmdInfo->u4TxpLen;
	pucSrc = kalMemAlloc(u4TotalLen, PHY_MEM_TYPE);
	ASSERT(pucSrc);

	kalMemCopy(pucSrc, prCmdInfo->pucTxd, prCmdInfo->u4TxdLen);
	kalMemCopy(pucSrc + prCmdInfo->u4TxdLen, prCmdInfo->pucTxp, prCmdInfo->u4TxpLen);

	spin_lock_irqsave((spinlock_t *)prTxRingLock, flags);

	SwIdx = prTxRing->TxCpuIdx;
	pTxD = (TXD_STRUCT *) prTxRing->Cell[SwIdx].AllocVa;

	prTxRing->Cell[SwIdx].pPacket = (PVOID)prCmdInfo;
	prTxRing->Cell[SwIdx].pBuffer = pucSrc;
	prTxRing->Cell[SwIdx].PacketPa = pci_map_single(pdev, pucSrc, u4TotalLen, PCI_DMA_TODEVICE);

	pTxD->SDPtr0 = prTxRing->Cell[SwIdx].PacketPa;
	pTxD->SDLen0 = u4TotalLen;
	pTxD->SDPtr1 = (UINT_32)NULL;
	pTxD->SDLen1 = 0;
	pTxD->LastSec0 = 1;
	pTxD->LastSec1 = 0;
	pTxD->Burst = 0;
	pTxD->DMADONE = 0;

	/* Increase TX_CTX_IDX, but write to register later. */
	INC_RING_INDEX(prTxRing->TxCpuIdx, TX_RING_SIZE);

	prTxRing->u4UsedCnt++;
	kalDevRegWrite(prGlueInfo, prTxRing->hw_cidx_addr, prTxRing->TxCpuIdx);

	spin_unlock_irqrestore((spinlock_t *)prTxRingLock, flags);

	DBGLOG(HAL, TRACE, "%s: CmdInfo[0x%p], TxD[0x%p/%u] TxP[0x%p/%u] CPU idx[%u] Used[%u]\n", __func__,
		prCmdInfo, prCmdInfo->pucTxd, prCmdInfo->u4TxdLen, prCmdInfo->pucTxp, prCmdInfo->u4TxpLen,
		SwIdx, prTxRing->u4UsedCnt);

	return TRUE;
}

BOOL kalDevWriteData(IN P_GLUE_INFO_T prGlueInfo, IN P_MSDU_INFO_T prMsduInfo, IN UINT_8 ucIsLastMsduInfo)
{
	P_GL_HIF_INFO_T prHifInfo = NULL;
	ULONG flags = 0;
	UINT_32 SwIdx = 0;
	P_RTMP_TX_RING prTxRing;
	spinlock_t *prTxRingLock;
	TXD_STRUCT *pTxD;
	struct pci_dev *pdev = NULL;
	UINT_16 u2Port = TX_RING_DATA0_IDX_0;
	UINT_32 u4TotalLen;
	struct sk_buff *skb;
	PUINT_8 pucSrc;
	P_MSDU_TOKEN_ENTRY_T prToken;

	ASSERT(prGlueInfo);
	prHifInfo = &prGlueInfo->rHifInfo;

	pdev = prHifInfo->pdev;
	prTxRing = &prHifInfo->TxRing[u2Port];
	prTxRingLock = &prHifInfo->TxRingLock[u2Port];

	skb = (struct sk_buff *)prMsduInfo->prPacket;
	pucSrc = skb->data;
	u4TotalLen = skb->len;

	/* Acquire MSDU token */
	prToken = halAcquireMsduToken(prGlueInfo->prAdapter);

#if HIF_TX_PREALLOC_DATA_BUFFER
	kalMemCopy(prToken->prPacket, pucSrc, u4TotalLen);
#else
	prToken->prMsduInfo = prMsduInfo;
	prToken->prPacket = pucSrc;
	prMsduInfo->prToken = prToken;
	prToken->rDmaAddr = pci_map_single(pdev, prToken->prPacket, u4TotalLen, PCI_DMA_TODEVICE);
	prToken->u4DmaLength = u4TotalLen;
#endif

	/* Update Tx descriptor */
	halTxUpdateCutThroughDesc(prMsduInfo, prToken);

	SwIdx = prTxRing->TxCpuIdx;
	pTxD = (TXD_STRUCT *) prTxRing->Cell[SwIdx].AllocVa;

#if 0
	prTxRing->Cell[SwIdx].pPacket = (PVOID)prMsduInfo;
	prTxRing->Cell[SwIdx].pBuffer = prToken->prPacket;
	prTxRing->Cell[SwIdx].PacketPa = prToken->rDmaAddr;
#endif

	pTxD->SDPtr0 = prToken->rDmaAddr;
	pTxD->SDLen0 = HIF_TX_DESC_PAYLOAD_LENGTH;
	pTxD->SDPtr1 = (UINT_32)NULL;
	pTxD->SDLen1 = 0;
	pTxD->LastSec0 = 1;
	pTxD->LastSec1 = 0;
	pTxD->Burst = 0;
	pTxD->DMADONE = 0;

	/* Increase TX_CTX_IDX, but write to register later. */
	INC_RING_INDEX(prTxRing->TxCpuIdx, TX_RING_SIZE);

	/* Update HW Tx DMA ring */
	spin_lock_irqsave((spinlock_t *)prTxRingLock, flags);

	prTxRing->u4UsedCnt++;
	if (ucIsLastMsduInfo)
		kalDevRegWrite(prGlueInfo, prTxRing->hw_cidx_addr, prTxRing->TxCpuIdx);

	spin_unlock_irqrestore((spinlock_t *)prTxRingLock, flags);

	DBGLOG(HAL, TRACE, "Tx Data: Msdu[0x%p], Tok[%u] TokFree[%u] CPU idx[%u] Used[%u] TxDone[%u]\n",
		prMsduInfo, prToken->u4Token, halGetMsduTokenFreeCnt(prGlueInfo->prAdapter),
		SwIdx, prTxRing->u4UsedCnt, (prMsduInfo->pfTxDoneHandler ? TRUE : FALSE));

	nicTxReleaseResource(prGlueInfo->prAdapter, prMsduInfo->ucTC,
		nicTxGetPageCount(prMsduInfo->u2FrameLength, TRUE), TRUE);

#if HIF_TX_PREALLOC_DATA_BUFFER
	if (!prMsduInfo->pfTxDoneHandler) {
		nicTxFreePacket(prGlueInfo->prAdapter, prMsduInfo, FALSE);
		nicTxReturnMsduInfo(prGlueInfo->prAdapter, prMsduInfo);
	}
#endif

	if (ucIsLastMsduInfo && wlanGetTxPendingFrameCount(prGlueInfo->prAdapter))
		kalSetEvent(prGlueInfo);

	return TRUE;
}

BOOL kalDevReadData(IN P_GLUE_INFO_T prGlueInfo, IN UINT_16 u2Port, IN OUT P_SW_RFB_T prSwRfb)
{
	P_GL_HIF_INFO_T prHifInfo = NULL;
	RXD_STRUCT *pRxD;
	P_RTMP_RX_RING prRxRing;
	spinlock_t *pRxRingLock;
	PVOID pRxPacket = NULL;
	RTMP_DMACB *pRxCell;
	struct pci_dev *pdev = NULL;
	BOOL fgRet = TRUE;
	ULONG flags = 0;
	PRTMP_DMABUF prDmaBuf;

	ASSERT(prGlueInfo);
	prHifInfo = &prGlueInfo->rHifInfo;

	pdev = prHifInfo->pdev;

	prRxRing = &prHifInfo->RxRing[u2Port];
	pRxRingLock = &prHifInfo->RxRingLock[u2Port];

	spin_lock_irqsave(pRxRingLock, flags);

	pRxCell = &prRxRing->Cell[prRxRing->RxSwReadIdx];

	/* Point to Rx indexed rx ring descriptor */
	pRxD = (RXD_STRUCT *) pRxCell->AllocVa;

	if (pRxD->DDONE == 0) {
		/* Get how may packets had been received */
		kalDevRegRead(prGlueInfo, prRxRing->hw_didx_addr, &prRxRing->RxDmaIdx);

		DBGLOG(HAL, TRACE, "Rx DMA done P[%u] DMA[%u] SW_RD[%u]\n", u2Port,
			prRxRing->RxDmaIdx, prRxRing->RxSwReadIdx);
		fgRet = FALSE;
		goto done;
	}

	pRxPacket = pRxCell->pPacket;
	ASSERT(pRxPacket);

	prDmaBuf = &pRxCell->DmaBuf;

	pRxCell->pPacket = prSwRfb->pvPacket;

	pci_unmap_single(pdev, prDmaBuf->AllocPa, prDmaBuf->AllocSize, PCI_DMA_FROMDEVICE);
	prSwRfb->pvPacket = pRxPacket;
	prSwRfb->pucRecvBuff = ((struct sk_buff *)pRxPacket)->data;
	prSwRfb->prRxStatus = (P_HW_MAC_RX_DESC_T)prSwRfb->pucRecvBuff;

	prDmaBuf->AllocVa = ((struct sk_buff *)pRxCell->pPacket)->data;
	prDmaBuf->AllocPa = pci_map_single(pdev, prDmaBuf->AllocVa, prDmaBuf->AllocSize, PCI_DMA_FROMDEVICE);

	pRxD->SDP0 = prDmaBuf->AllocPa;
	pRxD->SDL0 = prRxRing->u4BufSize;
	pRxD->DDONE = 0;

	prRxRing->RxCpuIdx = prRxRing->RxSwReadIdx;
	kalDevRegWrite(prGlueInfo, prRxRing->hw_cidx_addr, prRxRing->RxCpuIdx);
	INC_RING_INDEX(prRxRing->RxSwReadIdx, prRxRing->u4RingSize);

done:
	spin_unlock_irqrestore(pRxRingLock, flags);

	return fgRet;
}


VOID kalPciUnmapToDev(IN P_GLUE_INFO_T prGlueInfo, IN dma_addr_t rDmaAddr, IN UINT_32 u4Length)
{
	P_GL_HIF_INFO_T prHifInfo = NULL;
	struct pci_dev *pdev = NULL;

	prHifInfo = &prGlueInfo->rHifInfo;
	pdev = prHifInfo->pdev;
	pci_unmap_single(pdev, rDmaAddr, u4Length, PCI_DMA_TODEVICE);
}

VOID glSetPowerState(IN P_GLUE_INFO_T prGlueInfo, IN UINT_32 ePowerMode)
{
}

void glGetDev(PVOID ctx, struct device **dev)
{
	*dev = &((struct pci_dev *)ctx)->dev;
}

void glGetHifDev(P_GL_HIF_INFO_T prHif, struct device **dev)
{
	*dev = &(prHif->pdev->dev);
}
