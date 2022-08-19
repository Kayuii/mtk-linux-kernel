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
#include "wmt_plat.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

UINT32 gWmtDbgLvl = WMT_LOG_INFO;

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

int g_sdio_irq = -1;

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

INT32
wmt_plat_init(
    P_PWR_SEQ_TIME pPwrSeqTime
)
{
    printk("%s!!\n", __func__);

    return mtk_wcn_cmb_hw_init(pPwrSeqTime);
}

INT32
wmt_plat_deinit(VOID)
{
    printk("%s!!\n", __func__);

    return mtk_wcn_cmb_hw_deinit();
}

INT32
wmt_plat_wake_lock_ctrl(
    ENUM_WL_OP opId
)
{
    printk("%s!!\n", __func__);
    return 0;
}

INT32 wmt_plat_gpio_ctrl(
    ENUM_PIN_ID id,
    ENUM_PIN_STATE state
)
{
    INT32 ret = -1;

    switch (state) {
    case FUNC_ON:
        ret = mtk_wcn_cmb_hw_pwr_on();
        break;
    case FUNC_OFF:
        ret = mtk_wcn_cmb_hw_pwr_off();
        break;
    case FUNC_RST:
        ret = mtk_wcn_cmb_hw_rst();
        break;
    case FUNC_STAT:
        ret = mtk_wcn_cmb_hw_state_show();
        break;
    default:
        WMT_WARN_FUNC("WMT-PLAT:Warnning, invalid state(%d) in pwr_ctrl\n", state);
        break;
    }

    return ret;
}

INT32
wmt_plat_eirq_ctrl(
    ENUM_PIN_ID id,
    ENUM_PIN_STATE state
)
{
    printk("%s!! id=%d state=%d\n", __func__, id, state);
    return 0;
}

INT32
wmt_plat_sdio_ctrl(
    UINT32 sdioPortNum,
    ENUM_FUNC_STATE on
)
{
    printk("%s!! port=%d on=%d\n", __func__, sdioPortNum, on);
    return 0;
}

INT32
wmt_plat_pwr_ctrl(
    ENUM_FUNC_STATE state
)
{
    printk("%s!!\n", __func__);
    return 0;
}

#if defined(WMT_PLAT_ALPS) && WMT_PLAT_ALPS
VOID
wmt_lib_plat_aif_cb_reg(
    device_audio_if_cb aif_ctrl_cb
)
{
    printk("%s!!\n", __func__);
}
#endif

int
mtk_wcn_sdio_irq_flag_set(
    int falg
)
{
    printk("%s!!\n", __func__);
    g_sdio_irq = falg;
    return 0;
}

int
mtk_wcn_sdio_irq_flag_get(VOID)
{
    printk("%s!!\n", __func__);
    return g_sdio_irq;
}

VOID
wmt_lib_plat_irq_cb_reg(
    irq_cb bgf_irq_cb
)
{
    printk("%s!!\n", __func__);
}

#if defined(WMT_PLAT_ALPS) && WMT_PLAT_ALPS
int
mt_combo_plt_enter_deep_idle(
    COMBO_IF src
)
{
    printk("%s!!\n", __func__);
    return 0;
}

int
mt_combo_plt_exit_deep_idle(
    COMBO_IF src
)
{
    printk("%s!!\n", __func__);
    return 0;
}
#endif

INT32
wmt_plat_merge_if_flag_ctrl(
    UINT32 enagle
)
{
    printk("%s!!\n", __func__);
    return 0;
}

INT32
wmt_plat_merge_if_flag_get(VOID)
{
    printk("%s!!\n", __func__);
    return 0;
}

VOID
stop_log(VOID)
{
    printk("%s!!\n", __func__);
}

VOID
dump_uart_history(VOID)
{
    printk("%s!!\n", __func__);
}

VOID
aed_combo_exception(
    const int *arg1,
    int arg2,
    const int * arg3,
    int arg4,
    const char * arg5
)
{
    printk("%s!!\n", __func__);
}

INT32
wmt_plat_set_comm_if_type (ENUM_STP_TX_IF_TYPE type)
{
    printk("%s!! type=%d\n", __func__, type);
    return 0;
}

INT32 wmt_plat_stub_init (void)
{
    printk("%s!!\n", __func__);
    return 0;
}
