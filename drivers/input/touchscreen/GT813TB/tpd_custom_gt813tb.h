#ifndef TOUCHPANEL_H__
#define TOUCHPANEL_H__

#include <linux/ioctl.h>
#include <cust/cust_gt8xx.h>

/* Pre-defined definition */
#define TPD_TYPE_CAPACITIVE
#define TPD_TYPE_RESISTIVE
/* #define TPD_POWER_SOURCE         MT6575_POWER_VGP2 */
/* mt8135 */
#define TPD_POWER_LDO            MT65XX_POWER_LDO_VGP6
#define TPD_I2C_NUMBER           0
#define TPD_WAKEUP_TRIAL         60
#define TPD_WAKEUP_DELAY         100

#define TPD_DELAY                (2*HZ/100)
/* #define TPD_RES_X                480 */
/* #define TPD_RES_Y                800 */
#define TPD_CALIBRATION_MATRIX_ROTATION_FACTORY  {4096, 0, 0, 0, 4096, 0, 0, 0};
/* #define TPD_CALIBRATION_MATRIX_ROTATION_NORMAL  {6832, 0, 0, 0, -2456, 1961984, 0, 0};        //wisky 7" 800*400 panel data */
#define TPD_CALIBRATION_MATRIX_ROTATION_NORMAL  {0, 4096, 0, -4096, 0, 2453504, 0, 0};	/* wisky 7" 1024*600 panel data */
/* #define TPD_CALIBRATION_MATRIX_ROTATION_NORMAL  {5463, 0, 0, 0, 3071, 0, 0, 0};       //wisky 8" 1024*768 panel data */

#define TPD_HAVE_CALIBRATION
/* #define TPD_HAVE_BUTTON */
/* #define TPD_HAVE_TOUCH_KEY */
#define TPD_HAVE_TREMBLE_ELIMINATION

#define TPD_RESET_ISSUE_WORKAROUND

#define TPD_HAVE_POWER_ON_OFF

#define MAX_TRANSACTION_LENGTH 8
#define I2C_DEVICE_ADDRESS_LEN 2
#define MAX_I2C_TRANSFER_SIZE (MAX_TRANSACTION_LENGTH - I2C_DEVICE_ADDRESS_LEN)
#define MAX_I2C_MAX_TRANSFER_SIZE 8


#define TPD_TOUCH_INFO_REG_BASE 0xF40
#define TPD_KEY_INFO_REG_BASE 0xF41
#define TPD_POINT_INFO_REG_BASE 0xF42
#define TPD_POWER_MODE_REG 0xFF2
#define TPD_I2C_ENABLE_REG 0x0FFF
#define TPD_I2C_DISABLE_REG 0x8000
/* #define TPD_VERSION_INFO_REG 240 */
#define TPD_CONFIG_REG_BASE 0xF80

#define TPD_KEY_HOME 0x02
#define TPD_KEY_BACK 0x04
#define TPD_KEY_MENU 0x01

#define INT_TRIGGER 0x01
#define MAX_FINGER_NUM 5
#define TPD_POINT_INFO_LEN 5
#define TPD_TOUCH_INFO_LENGTH 1

#ifdef TPD_HAVE_TOUCH_KEY
const u8 touchKeyArray[] = { KEY_MENU, KEY_HOMEPAGE, KEY_BACK };

#define TPD_TOUCH_KEY_NUM (sizeof(touchKeyArray) / sizeof(touchKeyArray[0]))
#endif

/* #define CREATE_WR_NODE */
#ifdef CREATE_WR_NODE
extern s32 init_wr_node(struct i2c_client *);
extern void uninit_wr_node(void);
#endif
/* #define AUTO_UPDATE_GUITAR */
#ifdef AUTO_UPDATE_GUITAR
extern s32 init_update_proc(struct i2c_client *);
#endif

extern int i2c_read_bytes(struct i2c_client *client, u16 addr, u8 *rxbuf, int len);
extern int i2c_write_bytes(struct i2c_client *client, u16 addr, u8 *txbuf, int len);
extern int i2c_enable_commands(struct i2c_client *, u16);
extern int tpd_init_panel(void);

/* wisky 7" 800x480 */
/*
static u8 cfg_data[] =
{
0x1A,0x0B,0x19,0x0A,0x18,0x09,0x17,0x08,
0x16,0x07,0x15,0x06,0x14,0x05,0x13,0x04,
0x12,0x03,0x11,0x02,0xFF,0xFF,0xFF,0xFF,
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x03,0x0D,
0x04,0x0E,0x05,0x0F,0x06,0x10,0x07,0x11,
0x08,0x12,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
0xFF,0xFF,0x0F,0x03,0x88,0x00,0x00,0x21,
0x00,0x00,0x09,0x00,0x00,0x0A,0x45,0x2A,
0x34,0x03,0x00,0x05,0x00,0x01,0xE0,0x03,
0x20,0x3C,0x3B,0x3F,0x3D,0x27,0x00,0x25,
0x23,0x05,0x14,0x10,0x01,0x26,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01
};
*/

/* wisky 7" 1024x600 */
static u8 cfg_data[] = {
	0x02, 0x11, 0x03, 0x12, 0x04, 0x13, 0x05, 0x14,
	0x06, 0x15, 0x07, 0x16, 0x08, 0x17, 0x09, 0x18,
	0x0A, 0x19, 0x0B, 0x1A, 0xFF, 0x1A, 0x1B, 0xFF,
	0x18, 0x19, 0x1A, 0x1B, 0x1C, 0xFF, 0x03, 0x0D,
	0x04, 0x0E, 0x05, 0x0F, 0x06, 0x10, 0x07, 0x11,
	0x08, 0x12, 0xFF, 0x0D, 0xFF, 0x07, 0x10, 0x05,
	0x0E, 0x03, 0x1F, 0x03, 0xE8, 0x00, 0x00, 0x23,
	0x00, 0x00, 0x08, 0x00, 0x00, 0x02, 0x48, 0x38,
	0x14, 0x03, 0x00, 0x05, 0x04, 0x02, 0x58, 0x04,
	0x00, 0x42, 0x41, 0x3E, 0x3D, 0x0D, 0x00, 0x26,
	0x21, 0x05, 0x14, 0x10, 0x02, 0x94, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x42, 0x35, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};


/* wisky 8" 1024x768 */
/*
static u8 cfg_data[] =
{
0x02,0x11,0x03,0x12,0x04,0x13,0x05,0x14,
0x06,0x15,0x07,0x16,0x08,0x17,0x09,0x18,
0x0A,0x19,0xFF,0x13,0xFF,0x1A,0x1B,0xFF,
0x18,0x19,0x1A,0x1B,0x1C,0xFF,0x03,0x0D,
0x04,0x0E,0x05,0x0F,0x06,0x10,0x07,0x11,
0x08,0x12,0x09,0x13,0xFF,0x07,0x10,0x05,
0x0E,0x03,0x1F,0x03,0xE8,0x00,0x00,0x23,
0x00,0x00,0x0B,0x00,0x00,0x02,0x48,0x38,
0x14,0x03,0x00,0x05,0x04,0x03,0x00,0x04,
0x00,0x3A,0x38,0x36,0x34,0x0D,0x00,0x26,
0x21,0x05,0x14,0x10,0x02,0x44,0x00,0x00,
0x00,0x00,0x00,0x00,0x42,0x35,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01
};
*/

/*For factory Mode*/
#define TPD_NAME				"GT813"
#define TPD_IOCTL_MAGIC			't'
#define TPD_IOCTL_ENABLE_I2C	_IO(TPD_IOCTL_MAGIC, 1)
#define TPD_IOCTL_DISABLE_I2C	_IO(TPD_IOCTL_MAGIC, 2)

extern struct gt8xx_platform_data *p_gt8xx_platform_data;
#define GPIO_CTP_RST_PIN	(p_gt8xx_platform_data->gtp_rst_port)
#define GPIO_CTP_EINT_PIN	(p_gt8xx_platform_data->gtp_int_port)
#define GPIO_CTP_RST_PIN_M_GPIO	(p_gt8xx_platform_data->gtp_rst_m_gpio)
#define GPIO_CTP_EINT_PIN_M_GPIO	(p_gt8xx_platform_data->gtp_int_m_gpio)
#define GPIO_CTP_EINT_PIN_M_EINT	(p_gt8xx_platform_data->gtp_int_m_eint)
#define CUST_EINT_TOUCH_PANEL_NUM	(p_gt8xx_platform_data->irq)
#endif				/* TOUCHPANEL_H__ */
