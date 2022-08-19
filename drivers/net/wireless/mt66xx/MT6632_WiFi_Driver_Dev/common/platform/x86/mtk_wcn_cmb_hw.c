/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#include "osal_typedef.h"
#include "wmt_exp.h"
#include "mtk_wcn_cmb_hw.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

#define DFT_RTC_STABLE_TIME 100
#define DFT_LDO_STABLE_TIME 100
#define DFT_RST_STABLE_TIME 30
#define DFT_OFF_STABLE_TIME 10
#define DFT_ON_STABLE_TIME 30

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

PWR_SEQ_TIME gPwrSeqTime;

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

static VOID
mtk_wcn_cmb_hw_dmp_seq(VOID);

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

INT32
mtk_wcn_cmb_hw_pwr_off(VOID)
{
    WMT_DBG_FUNC("WCN-CMB: %s : assume EVK is turned off\n", __func__);

    /* On x86, make sure you power off the EVK manually */

    return 0;
}

INT32
mtk_wcn_cmb_hw_pwr_on(VOID)
{
    WMT_DBG_FUNC("WCN-PLAT: %s : assume EVK is turned on\n", __func__);

    /* On x86, make sure you power on the EVK manually */

    return 0;
}

INT32
mtk_wcn_cmb_hw_rst(VOID)
{
    WMT_DBG_FUNC("WCN-PLAT: %s : assume EVK is reset already\n", __func__);

    /* On x86, make sure you reset the EVK manually */

    return 0;
}

INT32 mtk_wcn_cmb_hw_state_show(VOID)
{
    WMT_DBG_FUNC("WCN-PLAT: %s\n", __func__);
    return 0;
}

INT32
mtk_wcn_cmb_hw_init(
    P_PWR_SEQ_TIME pPwrSeqTime
)
{
    WMT_DBG_FUNC("WCN-PLAT: %s\n", __func__);
    if (NULL != pPwrSeqTime            &&
        pPwrSeqTime->ldoStableTime > 0 &&
        pPwrSeqTime->rtcStableTime > 0 &&
        pPwrSeqTime->offStableTime > DFT_OFF_STABLE_TIME &&
        pPwrSeqTime->onStableTime  > DFT_ON_STABLE_TIME  &&
        pPwrSeqTime->rstStableTime > DFT_RST_STABLE_TIME
       ) {
        /*memcpy may be more performance*/
        WMT_DBG_FUNC("setting hw init sequence parameters\n");
        osal_memcpy(&gPwrSeqTime, pPwrSeqTime, osal_sizeof(gPwrSeqTime));
    } else {
        WMT_WARN_FUNC("invalid pPwrSeqTime parameter, "
                      "use default hw init sequence parameters\n");
        gPwrSeqTime.ldoStableTime = DFT_LDO_STABLE_TIME;
        gPwrSeqTime.offStableTime = DFT_OFF_STABLE_TIME;
        gPwrSeqTime.onStableTime = DFT_ON_STABLE_TIME;
        gPwrSeqTime.rstStableTime = DFT_RST_STABLE_TIME;
        gPwrSeqTime.rtcStableTime = DFT_RTC_STABLE_TIME;
    }
    mtk_wcn_cmb_hw_dmp_seq();
    return 0;
}

INT32
mtk_wcn_cmb_hw_deinit(VOID)
{
    WMT_DBG_FUNC("WCN-PLAT: %s\n", __func__);
    return 0;
}

static VOID
mtk_wcn_cmb_hw_dmp_seq(VOID)
{
    PUINT32 pTimeSlot = (PUINT32) &gPwrSeqTime;
    WMT_INFO_FUNC("combo chip power on sequence time, "
                  "RTC (%d), LDO (%d), RST(%d), OFF(%d), ON(%d)\n",
                  pTimeSlot[0], /**pTimeSlot++,*/
                  pTimeSlot[1],
                  pTimeSlot[2],
                  pTimeSlot[3],
                  pTimeSlot[4]
                 );
    return;
}
