/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Sony Mobile Communications AB.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/spmi.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/radix-tree.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/power_supply.h>
#include <linux/bitops.h>
#include <linux/ratelimit.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/input.h>
#include <linux/switch.h>

/* Interrupt offsets */
#define INT_RT_STS(base)			(base + 0x10)
#define INT_SET_TYPE(base)			(base + 0x11)
#define INT_POLARITY_HIGH(base)			(base + 0x12)
#define INT_POLARITY_LOW(base)			(base + 0x13)
#define INT_LATCHED_CLR(base)			(base + 0x14)
#define INT_EN_SET(base)			(base + 0x15)
#define INT_EN_CLR(base)			(base + 0x16)
#define INT_LATCHED_STS(base)			(base + 0x18)
#define INT_PENDING_STS(base)			(base + 0x19)
#define INT_MID_SEL(base)			(base + 0x1A)
#define INT_PRIORITY(base)			(base + 0x1B)

/* Peripheral register offsets */
#define CHGR_CHG_OPTION				0x08
#define CHGR_ATC_STATUS				0x0A
#define CHGR_VBAT_STATUS			0x0B
#define CHGR_IBAT_BMS				0x0C
#define CHGR_IBAT_STS				0x0D
#define CHGR_VDD_MAX				0x40
#define CHGR_VDD_SAFE				0x41
#define CHGR_VDD_MAX_STEP			0x42
#define CHGR_IBAT_MAX				0x44
#define CHGR_IBAT_SAFE				0x45
#define CHGR_VIN_MIN				0x47
#define CHGR_VIN_MIN_STEP			0x48
#define CHGR_CHG_CTRL				0x49
#define CHGR_CHG_FAILED				0x4A
#define CHGR_ATC_CTRL				0x4B
#define CHGR_ATC_FAILED				0x4C
#define CHGR_VBAT_TRKL				0x50
#define CHGR_VBAT_WEAK				0x52
#define CHGR_IBAT_ATC_A				0x54
#define CHGR_IBAT_ATC_B				0x55
#define CHGR_IBAT_TERM_CHGR			0x5B
#define CHGR_IBAT_TERM_BMS			0x5C
#define CHGR_VBAT_DET				0x5D
#define CHGR_TTRKL_MAX				0x5F
#define CHGR_TTRKL_MAX_EN			0x60
#define CHGR_TCHG_MAX				0x61
#define CHGR_CHG_WDOG_TIME			0x62
#define CHGR_CHG_WDOG_DLY			0x63
#define CHGR_CHG_WDOG_PET			0x64
#define CHGR_CHG_WDOG_EN			0x65
#define CHGR_IR_DROP_COMPEN			0x67
#define CHGR_I_MAX_REG			0x44
#define CHGR_USB_USB_SUSP			0x47
#define CHGR_USB_USB_OTG_CTL			0x48
#define CHGR_USB_ENUM_T_STOP			0x4E
#define CHGR_CHG_TEMP_THRESH			0x66
#define CHGR_BAT_IF_PRES_STATUS			0x08
#define CHGR_BAT_IF_BAT_FET_STATUS		0x0B
#define CHGR_STATUS				0x09
#define CHGR_BAT_IF_VCP				0x42
#define CHGR_BAT_IF_BATFET_CTRL1		0x90
#define CHGR_MISC_BOOT_DONE			0x42
#define CHGR_BUCK_COMPARATOR_OVRIDE_1		0xEB
#define CHGR_BUCK_COMPARATOR_OVRIDE_3		0xED
#define CHGR_BUCK_BCK_VBAT_REG_MODE		0x74
#define MISC_REVISION2				0x01
#define MISC_REVISION3				0x02
#define USB_OVP_CTL				0x42
#define USB_CHG_GONE_REV_BST			0xED
#define BUCK_VCHG_OV				0x77
#define BUCK_TEST_SMBC_MODES			0xE6
#define SEC_ACCESS				0xD0
#define BAT_IF_VREF_BAT_THM_CTRL		0x4A
#define BAT_IF_BPD_CTRL				0x48
#define BOOST_VSET				0x41
#define BOOST_ENABLE_CONTROL			0x46
#define COMP_OVR1				0xEA

#define REG_OFFSET_PERP_SUBTYPE			0x05

/* SMBB peripheral subtype values */
#define SMBB_CHGR_SUBTYPE			0x01
#define SMBB_BUCK_SUBTYPE			0x02
#define SMBB_BAT_IF_SUBTYPE			0x03
#define SMBB_USB_CHGPTH_SUBTYPE			0x04
#define SMBB_DC_CHGPTH_SUBTYPE			0x05
#define SMBB_BOOST_SUBTYPE			0x06
#define SMBB_MISC_SUBTYPE			0x07

/* SMBB peripheral subtype values */
#define SMBBP_CHGR_SUBTYPE			0x31
#define SMBBP_BUCK_SUBTYPE			0x32
#define SMBBP_BAT_IF_SUBTYPE			0x33
#define SMBBP_USB_CHGPTH_SUBTYPE		0x34
#define SMBBP_BOOST_SUBTYPE			0x36
#define SMBBP_MISC_SUBTYPE			0x37

/* SMBCL peripheral subtype values */
#define SMBCL_CHGR_SUBTYPE			0x41
#define SMBCL_BUCK_SUBTYPE			0x42
#define SMBCL_BAT_IF_SUBTYPE			0x43
#define SMBCL_USB_CHGPTH_SUBTYPE		0x44
#define SMBCL_MISC_SUBTYPE			0x47

#define QPNP_CHARGER_DEV_NAME	"qcom,qpnp-charger"

/* Status bits and masks */
#define CHGR_BOOT_DONE			BIT(7)
#define CHGR_CHG_EN			BIT(7)
#define CHGR_ON_BAT_FORCE_BIT		BIT(0)
#define CHGR_BAT_FET_ON			BIT(7)

#define USB_VALID_DEB_20MS		0x03
#define BUCK_VBAT_REG_NODE_SEL_BIT	BIT(0)
#define VREF_BATT_THERM_FORCE_ON	0xC0
#define BAT_IF_BPD_CTRL_SEL		0x03
#define VREF_BAT_THM_ENABLED_FSM	0x80
#define REV_BST_DETECTED		BIT(0)
#define BAT_THM_EN			BIT(1)
#define BAT_ID_EN			BIT(0)
#define BOOST_PWR_EN			BIT(7)

/* Interrupt definitions */
/* smbb_chg_interrupts */
#define CHG_DONE_IRQ			BIT(7)
#define CHG_FAILED_IRQ			BIT(6)
#define FAST_CHG_ON_IRQ			BIT(5)
#define TRKL_CHG_ON_IRQ			BIT(4)
#define STATE_CHANGE_ON_IR		BIT(3)
#define CHGWDDOG_IRQ			BIT(2)
#define VBAT_DET_HI_IRQ			BIT(1)
#define VBAT_DET_LOW_IRQ		BIT(0)

/* smbb_buck_interrupts */
#define VDD_LOOP_IRQ			BIT(6)
#define IBAT_LOOP_IRQ			BIT(5)
#define ICHG_LOOP_IRQ			BIT(4)
#define VCHG_LOOP_IRQ			BIT(3)
#define OVERTEMP_IRQ			BIT(2)
#define VREF_OV_IRQ			BIT(1)
#define VBAT_OV_IRQ			BIT(0)

/* smbb_bat_if_interrupts */
#define PSI_IRQ				BIT(4)
#define VCP_ON_IRQ			BIT(3)
#define BAT_FET_ON_IRQ			BIT(2)
#define BAT_TEMP_OK_IRQ			BIT(1)
#define BATT_PRES_IRQ			BIT(0)

/* smbb_usb_interrupts */
#define CHG_GONE_IRQ			BIT(2)
#define USBIN_VALID_IRQ			BIT(1)
#define COARSE_DET_USB_IRQ		BIT(0)

/* smbb_dc_interrupts */
#define DCIN_VALID_IRQ			BIT(1)
#define COARSE_DET_DC_IRQ		BIT(0)

/* smbb_boost_interrupts */
#define LIMIT_ERROR_IRQ			BIT(1)
#define BOOST_PWR_OK_IRQ		BIT(0)

/* smbb_misc_interrupts */
#define TFTWDOG_IRQ			BIT(0)

/* SMBB types */
#define SMBB				BIT(1)
#define SMBBP				BIT(2)
#define SMBCL				BIT(3)

/* Workaround flags */
#define CHG_FLAGS_VCP_WA		BIT(0)
#define BOOST_FLASH_WA			BIT(1)

struct qpnp_chg_irq {
	unsigned int		irq;
	unsigned long		disabled;
};

struct qpnp_chg_regulator {
	struct regulator_desc			rdesc;
	struct regulator_dev			*rdev;
};

/* ARB workaround */
#define ARB_CHECK_WAIT_PERIOD_MS_FIRST 5000
#define ARB_CHECK_WAIT_PERIOD_MS 1000

/* Wake locking time after charger unplugged */
#define UNPLUG_WAKELOCK_TIME_SEC	(2 * HZ)

/* Wake locking time after chg_gone interrupt */
#define CHG_GONE_WAKELOCK_TIME_MS	600

/* Unit change */
#define MA_TO_UA			1000

/* Charging can be triggered based on SOC % */
#define MAX_SOC_CHARGE_LEVEL 100

/* USB coarse det wa */
#define SMBB_HW_CONTROLLED_CLK_MS 25

#define AICL_PERIOD_MS	200

#define HEALTH_CHECK_PERIOD_MS 10000

#define NO_CHANGE_LIMIT (-1)

#define BATTERY_IS_PRESENT 1

enum {
	BAT_FET_STATUS = 0,
	IBAT_MAX,
	VDD_MAX,
	VBAT_DET,
	CHG_DONE,
	FAST_CHG_ON,
	VBAT_DET_LOW,
	CHG_GONE,
	USBIN_VALID,
	DCIN_VALID,
	IUSB_MAX,
	IDC_MAX,
	REVISION,
	BUCK_STS,
	CHG_CTRL,
};

enum main_present_bit {
	MAIN_DC_PRESENT_BIT,
	MAIN_DCP_PRESENT_BIT,
};

enum main_present {
	MAIN_DC_PRESENT		= BIT(MAIN_DC_PRESENT_BIT),
	MAIN_DCP_PRESENT	= BIT(MAIN_DCP_PRESENT_BIT),
};

enum dock_state_event {
	CHG_DOCK_UNDOCK,
	CHG_DOCK_DESK,
};

enum batfet_status {
	BATFET_OPEN,
	BATFET_CLOSE,
};

struct aicl_current {
	int			limit;
	int			set;
};

static DEFINE_SPINLOCK(aicl_lock);

struct qpnp_somc_params {
	unsigned int		decirevision;
	bool			enable_arb_check_work;
	bool			enable_chg_done_fastchg_work;
	struct delayed_work	arb_check_work;
	struct delayed_work	chg_done_fastchg_work;
	struct work_struct	ovp_check_work;
	struct qpnp_chg_irq	usb_coarse_det;
	struct qpnp_chg_irq	chg_gone;
	struct work_struct	smbb_clk_work;
	struct delayed_work	smbb_hw_clk_work;
	u8			main_presents;
	bool			dcp_present;
	int			iusb_max;
	bool			enable_stop_charging_at_low_battery;
	struct wake_lock	unplug_wake_lock;
	struct wake_lock	chg_gone_wake_lock;
	struct input_dev	*chg_unplug_key;
	unsigned int		resume_delta_soc;
	bool			kick_rb_workaround;
	bool			ovp_chg_dis;
	struct delayed_work	aicl_work;
	struct work_struct	aicl_set_work;
	struct aicl_current	iusb;
	struct aicl_current	idc;
	int			usb_current_max;
	int			charging_disabled_for_shutdown;
	struct switch_dev	swdev;
	enum dock_state_event	dock_event;
	struct wake_lock	dock_lock;
	struct work_struct	dock_work;
	struct delayed_work	health_check_work;
	int			last_health;
	bool			health_change;
	bool			warm_disable_charging;
	bool			workaround_batfet_close;
	int			disconnect_count;
	bool			enabling_regulator_boost;
};

/**
 * struct qpnp_chg_chip - device information
 * @dev:			device pointer to access the parent
 * @spmi:			spmi pointer to access spmi information
 * @chgr_base:			charger peripheral base address
 * @buck_base:			buck  peripheral base address
 * @bat_if_base:		battery interface  peripheral base address
 * @usb_chgpth_base:		USB charge path peripheral base address
 * @dc_chgpth_base:		DC charge path peripheral base address
 * @boost_base:			boost peripheral base address
 * @misc_base:			misc peripheral base address
 * @freq_base:			freq peripheral base address
 * @bat_is_cool:		indicates that battery is cool
 * @bat_is_warm:		indicates that battery is warm
 * @chg_done:			indicates that charging is completed
 * @usb_present:		present status of usb
 * @dc_present:			present status of dc
 * @batt_present:		present status of battery
 * @use_default_batt_values:	flag to report default battery properties
 * @max_voltage_mv:		the max volts the batt should be charged up to
 * @min_voltage_mv:		min battery voltage before turning the FET on
 * @max_bat_chg_current:	maximum battery charge current in mA
 * @warm_bat_chg_ma:	warm battery maximum charge current in mA
 * @cool_bat_chg_ma:	cool battery maximum charge current in mA
 * @warm_bat_mv:		warm temperature battery target voltage
 * @cool_bat_mv:		cool temperature battery target voltage
 * @resume_delta_mv:		voltage delta at which battery resumes charging
 * @term_current:		the charging based term current
 * @safe_current:		battery safety current setting
 * @maxinput_usb_ma:		Maximum Input current USB
 * @maxinput_dc_ma:		Maximum Input current DC
 * @warm_bat_decidegc		Warm battery temperature in degree Celsius
 * @cool_bat_decidegc		Cool battery temperature in degree Celsius
 * @revision:			PMIC revision
 * @type:			SMBB type
 * @tchg_mins			maximum allowed software initiated charge time
 * @thermal_levels		amount of thermal mitigation levels
 * @thermal_mitigation		thermal mitigation level values
 * @therm_lvl_sel		thermal mitigation level selection
 * @dc_psy			power supply to export information to userspace
 * @usb_psy			power supply to export information to userspace
 * @bms_psy			power supply to export information to userspace
 * @batt_psy:			power supply to export information to userspace
 * @flags:			flags to activate specific workarounds
 *				throughout the driver
 * @qpnp_somc_params:		specific parameters for somc
 *
 */
struct qpnp_chg_chip {
	struct device			*dev;
	struct spmi_device		*spmi;
	u16				chgr_base;
	u16				buck_base;
	u16				bat_if_base;
	u16				usb_chgpth_base;
	u16				dc_chgpth_base;
	u16				boost_base;
	u16				misc_base;
	u16				freq_base;
	struct qpnp_chg_irq		usbin_valid;
	struct qpnp_chg_irq		dcin_valid;
	struct qpnp_chg_irq		chg_fastchg;
	struct qpnp_chg_irq		chg_trklchg;
	struct qpnp_chg_irq		chg_failed;
	struct qpnp_chg_irq		chg_vbatdet_lo;
	struct qpnp_chg_irq		batt_pres;
	bool				bat_is_cool;
	bool				bat_is_warm;
	bool				chg_done;
	bool				usb_present;
	bool				dc_present;
	bool				batt_present;
	bool				charging_disabled;
	bool				use_default_batt_values;
	bool				duty_cycle_100p;
	unsigned int			bpd_detection;
	unsigned int			max_bat_chg_current;
	unsigned int			warm_bat_chg_ma;
	unsigned int			cool_bat_chg_ma;
	unsigned int			safe_voltage_mv;
	unsigned int			max_voltage_mv;
	unsigned int			min_voltage_mv;
	int				set_vddmax_mv;
	int				delta_vddmax_mv;
	unsigned int			warm_bat_mv;
	unsigned int			cool_bat_mv;
	unsigned int			resume_delta_mv;
	int				term_current;
	unsigned int			maxinput_usb_ma;
	unsigned int			maxinput_dc_ma;
	int				warm_bat_decidegc;
	int				cool_bat_decidegc;
	unsigned int			safe_current;
	unsigned int			revision;
	unsigned int			type;
	unsigned int			tchg_mins;
	unsigned int			thermal_levels;
	unsigned int			therm_lvl_sel;
	unsigned int			*thermal_mitigation;
	struct power_supply		dc_psy;
	struct power_supply		*usb_psy;
	struct power_supply		*bms_psy;
	struct power_supply		batt_psy;
	uint32_t			flags;
	struct qpnp_adc_tm_btm_param	adc_param;
	struct work_struct		adc_measure_work;
	struct delayed_work		arb_stop_work;
	struct delayed_work		eoc_work;
	struct wake_lock		eoc_wake_lock;
	struct qpnp_chg_regulator	otg_vreg;
	struct qpnp_chg_regulator	boost_vreg;
	struct qpnp_somc_params		somc_params;
};


static struct of_device_id qpnp_charger_match_table[] = {
	{ .compatible = QPNP_CHARGER_DEV_NAME, },
	{}
};

enum bpd_type {
	BPD_TYPE_BAT_ID,
	BPD_TYPE_BAT_THM,
	BPD_TYPE_BAT_THM_BAT_ID,
};
static void check_unplug_wakelock(struct qpnp_chg_chip *chip);
static void clear_safety_timer_chg_failed(struct qpnp_chg_chip *chip);
static void notify_input_chg_unplug(struct qpnp_chg_chip *chip, bool value);
static void qpnp_chg_set_appropriate_vbatdet(struct qpnp_chg_chip *chip);
static bool check_if_over_voltage(struct qpnp_chg_chip *chip);
static void qpnp_ovp_check_work(struct work_struct *work);
static void qpnp_smbb_sw_controlled_clk_work(struct work_struct *work);
static void qpnp_chg_aicl_iusb_set(struct qpnp_chg_chip *chip, int limit_ma);
static void qpnp_chg_aicl_idc_set(struct qpnp_chg_chip *chip, int limit_ma);
static void qpnp_chg_aicl_set_work(struct work_struct *work);


static const char * const bpd_label[] = {
	[BPD_TYPE_BAT_ID] = "bpd_id",
	[BPD_TYPE_BAT_THM] = "bpd_thm",
	[BPD_TYPE_BAT_THM_BAT_ID] = "bpd_thm_id",
};

static inline int
get_bpd(const char *name)
{
	int i = 0;
	for (i = 0; i < ARRAY_SIZE(bpd_label); i++) {
		if (strcmp(bpd_label[i], name) == 0)
			return i;
	}
	return -EINVAL;
}

static int
qpnp_chg_read(struct qpnp_chg_chip *chip, u8 *val,
			u16 base, int count)
{
	int rc = 0;
	struct spmi_device *spmi = chip->spmi;

	if (base == 0) {
		pr_err("base cannot be zero base=0x%02x sid=0x%02x rc=%d\n",
			base, spmi->sid, rc);
		return -EINVAL;
	}

	rc = spmi_ext_register_readl(spmi->ctrl, spmi->sid, base, val, count);
	if (rc) {
		pr_err("SPMI read failed base=0x%02x sid=0x%02x rc=%d\n", base,
				spmi->sid, rc);
		return rc;
	}
	return 0;
}

static int
qpnp_chg_write(struct qpnp_chg_chip *chip, u8 *val,
			u16 base, int count)
{
	int rc = 0;
	struct spmi_device *spmi = chip->spmi;

	if (base == 0) {
		pr_err("base cannot be zero base=0x%02x sid=0x%02x rc=%d\n",
			base, spmi->sid, rc);
		return -EINVAL;
	}

	rc = spmi_ext_register_writel(spmi->ctrl, spmi->sid, base, val, count);
	if (rc) {
		pr_err("write failed base=0x%02x sid=0x%02x rc=%d\n",
			base, spmi->sid, rc);
		return rc;
	}

	return 0;
}

static int
qpnp_chg_masked_write(struct qpnp_chg_chip *chip, u16 base,
						u8 mask, u8 val, int count)
{
	int rc;
	u8 reg;

	rc = qpnp_chg_read(chip, &reg, base, count);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n", base, rc);
		return rc;
	}
	pr_debug("addr = 0x%x read 0x%x\n", base, reg);

	reg &= ~mask;
	reg |= val & mask;

	pr_debug("Writing 0x%x\n", reg);

	rc = qpnp_chg_write(chip, &reg, base, count);
	if (rc) {
		pr_err("spmi write failed: addr=%03X, rc=%d\n", base, rc);
		return rc;
	}

	return 0;
}

static void
qpnp_chg_enable_irq(struct qpnp_chg_irq *irq)
{
	if (__test_and_clear_bit(0, &irq->disabled)) {
		pr_debug("number = %d\n", irq->irq);
		enable_irq(irq->irq);
	}
}

static void
qpnp_chg_disable_irq(struct qpnp_chg_irq *irq)
{
	if (!__test_and_set_bit(0, &irq->disabled)) {
		pr_debug("number = %d\n", irq->irq);
		disable_irq_nosync(irq->irq);
	}
}

#define USB_OTG_EN_BIT	BIT(0)
static int
qpnp_chg_is_otg_en_set(struct qpnp_chg_chip *chip)
{
	u8 usb_otg_en;
	int rc;

	rc = qpnp_chg_read(chip, &usb_otg_en,
				 chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
				 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->usb_chgpth_base + CHGR_STATUS, rc);
		return rc;
	}
	pr_debug("usb otg en 0x%x\n", usb_otg_en);

	return (usb_otg_en & USB_OTG_EN_BIT) ? 1 : 0;
}

static int
qpnp_chg_is_boost_en_set(struct qpnp_chg_chip *chip)
{
	u8 boost_en_ctl;
	int rc;

	rc = qpnp_chg_read(chip, &boost_en_ctl,
		chip->boost_base + BOOST_ENABLE_CONTROL, 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->boost_base + BOOST_ENABLE_CONTROL, rc);
		return rc;
	}

	pr_debug("boost en 0x%x\n", boost_en_ctl);

	return (boost_en_ctl & BOOST_PWR_EN) ? 1 : 0;
}

static int
qpnp_chg_is_batt_present(struct qpnp_chg_chip *chip)
{
	return BATTERY_IS_PRESENT;
}

#define USB_VALID_BIT	BIT(7)
static int
qpnp_chg_is_usb_chg_plugged_in(struct qpnp_chg_chip *chip)
{
	u8 usbin_valid_rt_sts;
	int rc;

	rc = qpnp_chg_read(chip, &usbin_valid_rt_sts,
				 chip->usb_chgpth_base + CHGR_STATUS , 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->usb_chgpth_base + CHGR_STATUS, rc);
		return rc;
	}
	pr_debug("chgr usb sts 0x%x\n", usbin_valid_rt_sts);

	return (usbin_valid_rt_sts & USB_VALID_BIT) ? 1 : 0;
}

static int
qpnp_chg_is_dc_chg_plugged_in(struct qpnp_chg_chip *chip)
{
	u8 dcin_valid_rt_sts;
	int rc;

	if (!chip->dc_chgpth_base)
		return 0;

	rc = qpnp_chg_read(chip, &dcin_valid_rt_sts,
				 INT_RT_STS(chip->dc_chgpth_base), 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->dc_chgpth_base), rc);
		return rc;
	}

	return (dcin_valid_rt_sts & DCIN_VALID_IRQ) ? 1 : 0;
}

#define QPNP_CHG_I_MAX_MIN_100		100
#define QPNP_CHG_I_MAX_MIN_150		150
#define QPNP_CHG_I_MAX_MIN_200		200
#define QPNP_CHG_I_MAX_MIN_MA		200
#define QPNP_CHG_I_MAX_MAX_MA		2500
#define QPNP_CHG_I_MAXSTEP_MA		100
static int
qpnp_chg_idcmax_set(struct qpnp_chg_chip *chip, int mA)
{
	int rc = 0;
	u8 dc = 0;

	if (mA < QPNP_CHG_I_MAX_MIN_100
			|| mA > QPNP_CHG_I_MAX_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", mA);
		return -EINVAL;
	}

	if (mA == QPNP_CHG_I_MAX_MIN_100) {
		dc = 0x00;
		pr_debug("current=%d setting %02x\n", mA, dc);
		return qpnp_chg_write(chip, &dc,
			chip->dc_chgpth_base + CHGR_I_MAX_REG, 1);
	} else if (mA == QPNP_CHG_I_MAX_MIN_150) {
		dc = 0x01;
		pr_debug("current=%d setting %02x\n", mA, dc);
		return qpnp_chg_write(chip, &dc,
			chip->dc_chgpth_base + CHGR_I_MAX_REG, 1);
	}

	dc = mA / QPNP_CHG_I_MAXSTEP_MA;

	pr_debug("current=%d setting 0x%x\n", mA, dc);
	rc = qpnp_chg_write(chip, &dc,
		chip->dc_chgpth_base + CHGR_I_MAX_REG, 1);

	return rc;
}

static int
qpnp_chg_iusbmax_set(struct qpnp_chg_chip *chip, int mA)
{
	int rc = 0;
	u8 usb_reg = 0, temp = 8;

	if (chip->somc_params.enable_arb_check_work)
		chip->somc_params.iusb_max = mA;

	if (mA < QPNP_CHG_I_MAX_MIN_100
			|| mA > QPNP_CHG_I_MAX_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", mA);
		return -EINVAL;
	}

	if (mA == QPNP_CHG_I_MAX_MIN_100) {
		usb_reg = 0x00;
		pr_debug("current=%d setting %02x\n", mA, usb_reg);
		return qpnp_chg_write(chip, &usb_reg,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);
	} else if (mA == QPNP_CHG_I_MAX_MIN_150) {
		usb_reg = 0x01;
		pr_debug("current=%d setting %02x\n", mA, usb_reg);
		return qpnp_chg_write(chip, &usb_reg,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);
	}

	/* Impose input current limit */
	if (chip->maxinput_usb_ma)
		mA = (chip->maxinput_usb_ma) <= mA ? chip->maxinput_usb_ma : mA;

	usb_reg = mA / QPNP_CHG_I_MAXSTEP_MA;

	if (chip->flags & CHG_FLAGS_VCP_WA) {
		temp = 0xA5;
		rc =  qpnp_chg_write(chip, &temp,
			chip->buck_base + SEC_ACCESS, 1);
		rc =  qpnp_chg_masked_write(chip,
			chip->buck_base + CHGR_BUCK_COMPARATOR_OVRIDE_3,
			0x0C, 0x0C, 1);
	}

	pr_debug("current=%d setting 0x%x\n", mA, usb_reg);
	rc = qpnp_chg_write(chip, &usb_reg,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);

	if (chip->flags & CHG_FLAGS_VCP_WA) {
		temp = 0xA5;
		udelay(200);
		rc =  qpnp_chg_write(chip, &temp,
			chip->buck_base + SEC_ACCESS, 1);
		rc =  qpnp_chg_masked_write(chip,
			chip->buck_base + CHGR_BUCK_COMPARATOR_OVRIDE_3,
			0x0C, 0x00, 1);
	}

	return rc;
}

#define USB_SUSPEND_BIT	BIT(0)
static int
qpnp_chg_usb_suspend_enable(struct qpnp_chg_chip *chip, int enable)
{
	if (chip->somc_params.ovp_chg_dis && !enable)
		return 0;

	return qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_SUSP,
			USB_SUSPEND_BIT,
			enable ? USB_SUSPEND_BIT : 0, 1);
}

static int
qpnp_chg_charge_en(struct qpnp_chg_chip *chip, int enable)
{
	if (chip->somc_params.ovp_chg_dis && enable)
		return 0;


	return qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_CHG_CTRL,
			CHGR_CHG_EN,
			enable ? CHGR_CHG_EN : 0, 1);
}

static int
qpnp_chg_force_run_on_batt(struct qpnp_chg_chip *chip, int disable)
{
	/* Don't run on battery for batteryless hardware */
	if (chip->use_default_batt_values)
		return 0;
	/* Don't force on battery if battery is not present */
	if (!qpnp_chg_is_batt_present(chip))
		return 0;

	/* This bit forces the charger to run off of the battery rather
	 * than a connected charger */
	return qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_CHG_CTRL,
			CHGR_ON_BAT_FORCE_BIT,
			disable ? CHGR_ON_BAT_FORCE_BIT : 0, 1);
}

static void
qpnp_chg_enable_charge(struct qpnp_chg_chip *chip, int enable)
{
	int rc;

	pr_debug("enable = %d\n", enable);

	if (!enable) {
		qpnp_chg_aicl_iusb_set(chip, NO_CHANGE_LIMIT);
		qpnp_chg_aicl_idc_set(chip, NO_CHANGE_LIMIT);
	}

	rc = qpnp_chg_charge_en(chip, enable);
	if (rc)
		pr_err("Failed to control charging %d rc = %d\n",
			enable, rc);

	rc = qpnp_chg_force_run_on_batt(chip, !enable);
	if (rc)
		pr_err("Failed to force run on battery rc = %d\n", rc);
}

static void
qpnp_chg_disable_charge_with_batfet(struct qpnp_chg_chip *chip, int batfet)
{
	int rc;

	pr_debug("BATFET = %s\n", batfet ? "Close" : "Open");

	qpnp_chg_aicl_iusb_set(chip, NO_CHANGE_LIMIT);
	qpnp_chg_aicl_idc_set(chip, NO_CHANGE_LIMIT);

	rc = qpnp_chg_force_run_on_batt(chip, batfet);
	if (rc)
		pr_err("Failed to force run on battery %d rc = %d\n",
			batfet, rc);

	rc = qpnp_chg_charge_en(chip, 0);
	if (rc)
		pr_err("Failed to disable charging rc = %d\n", rc);
}

#define BUCK_DUTY_MASK_100P	0x30
static int
qpnp_buck_set_100_duty_cycle_enable(struct qpnp_chg_chip *chip, int enable)
{
	int rc;

	pr_debug("enable: %d\n", enable);

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + SEC_ACCESS, 0xA5, 0xA5, 1);
	if (rc) {
		pr_debug("failed to write sec access rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + BUCK_TEST_SMBC_MODES,
			BUCK_DUTY_MASK_100P, enable ? 0x00 : 0x10, 1);
	if (rc) {
		pr_debug("failed enable 100p duty cycle rc=%d\n", rc);
		return rc;
	}

	return rc;
}

#define COMPATATOR_OVERRIDE_0	0x80
static int
qpnp_chg_toggle_chg_done_logic(struct qpnp_chg_chip *chip, int enable)
{
	int rc;

	pr_debug("toggle: %d\n", enable);

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + SEC_ACCESS, 0xA5, 0xA5, 1);
	if (rc) {
		pr_debug("failed to write sec access rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + CHGR_BUCK_COMPARATOR_OVRIDE_1,
			0xC0, enable ? 0x00 : COMPATATOR_OVERRIDE_0, 1);
	if (rc) {
		pr_debug("failed to toggle chg done override rc=%d\n", rc);
		return rc;
	}

	return rc;
}

#define QPNP_CHG_VBATDET_MIN_MV	3240
#define QPNP_CHG_VBATDET_MAX_MV	5780
#define QPNP_CHG_VBATDET_STEP_MV	20
static int
qpnp_chg_vbatdet_set(struct qpnp_chg_chip *chip, int vbatdet_mv)
{
	u8 temp;

	if (vbatdet_mv < QPNP_CHG_VBATDET_MIN_MV
			|| vbatdet_mv > QPNP_CHG_VBATDET_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", vbatdet_mv);
		return -EINVAL;
	}
	temp = (vbatdet_mv - QPNP_CHG_VBATDET_MIN_MV)
			/ QPNP_CHG_VBATDET_STEP_MV;

	pr_debug("voltage=%d setting %02x\n", vbatdet_mv, temp);
	return qpnp_chg_write(chip, &temp,
		chip->chgr_base + CHGR_VBAT_DET, 1);
}

static void
qpnp_arb_stop_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, arb_stop_work);

	chip->somc_params.kick_rb_workaround = false;

	if (chip->chg_done ||
	    chip->somc_params.charging_disabled_for_shutdown ||
	    chip->somc_params.workaround_batfet_close) {
		pr_debug("Cancel stopping RB workaround\n");
		return;
	}
	qpnp_chg_enable_charge(chip, !chip->charging_disabled);
	pr_debug("Stop RB workaround\n");
}

static void
qpnp_bat_if_adc_measure_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, adc_measure_work);

	if (qpnp_adc_tm_channel_measure(&chip->adc_param))
		pr_err("request ADC error\n");
}

#define EOC_CHECK_PERIOD_MS	10000
static irqreturn_t
qpnp_chg_vbatdet_lo_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	u8 chg_sts = 0;
	int rc;

	pr_debug("vbatdet-lo triggered\n");

	rc = qpnp_chg_read(chip, &chg_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc)
		pr_err("failed to read chg_sts rc=%d\n", rc);

	pr_debug("chg_done chg_sts: 0x%x triggered\n", chg_sts);
	if (!chip->charging_disabled && (chg_sts & FAST_CHG_ON_IRQ)) {
		schedule_delayed_work(&chip->eoc_work,
			msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
		wake_lock(&chip->eoc_wake_lock);
		qpnp_chg_disable_irq(&chip->chg_vbatdet_lo);
	} else {
		qpnp_chg_charge_en(chip, !chip->charging_disabled);
	}

	power_supply_changed(chip->usb_psy);
	if (chip->dc_chgpth_base)
		power_supply_changed(&chip->dc_psy);
	if (chip->bat_if_base)
		power_supply_changed(&chip->batt_psy);
	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_usb_chg_gone_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;

	if (!chip)
		goto chg_gone_exit;

	pr_info("chg_gone triggered\n");

	wake_lock_timeout(&chip->somc_params.chg_gone_wake_lock,
			msecs_to_jiffies(CHG_GONE_WAKELOCK_TIME_MS));
	schedule_delayed_work(&chip->somc_params.aicl_work, 0);
chg_gone_exit:
	return IRQ_HANDLED;
}

#define ENUM_T_STOP_BIT		BIT(0)
static irqreturn_t
qpnp_chg_usb_usbin_valid_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int usb_present, host_mode;

	check_unplug_wakelock(chip);

	usb_present = qpnp_chg_is_usb_chg_plugged_in(chip);
	host_mode = qpnp_chg_is_otg_en_set(chip);
	pr_debug("usbin-valid triggered: %d host_mode: %d\n",
		usb_present, host_mode);

	/* In host mode notifications cmoe from USB supply */
	if (host_mode)
		return IRQ_HANDLED;

	if (chip->usb_present ^ usb_present) {
		chip->usb_present = usb_present;
		if (!usb_present) {
			clear_safety_timer_chg_failed(chip);
			qpnp_chg_set_appropriate_vbatdet(chip);
			qpnp_chg_aicl_iusb_set(chip, NO_CHANGE_LIMIT);
			schedule_work(&chip->somc_params.aicl_set_work);
		} else {
			wake_lock(&chip->eoc_wake_lock);
			schedule_delayed_work(&chip->eoc_work,
				msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
		}

		power_supply_set_present(chip->usb_psy, chip->usb_present);
	}

	if (!chip->usb_present && !chip->dc_present) {
		chip->chg_done = false;
		notify_input_chg_unplug(chip, true);
		if (chip->somc_params.ovp_chg_dis) {
			chip->somc_params.ovp_chg_dis = false;
			qpnp_chg_usb_suspend_enable(chip, 1);
			qpnp_chg_enable_charge(chip,
					!chip->charging_disabled);
		}
	} else {
		notify_input_chg_unplug(chip, false);
		schedule_work(&chip->somc_params.ovp_check_work);
	}

	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_usb_coarse_det_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;

	pr_debug("usb coarse det triggered.");

	if (!qpnp_chg_is_otg_en_set(chip))
		schedule_work(&chip->somc_params.smbb_clk_work);

	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_bat_if_batt_pres_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int batt_present;

	batt_present = qpnp_chg_is_batt_present(chip);
	pr_debug("batt-pres triggered: %d\n", batt_present);

	if (chip->batt_present ^ batt_present) {
		chip->batt_present = batt_present;
		power_supply_changed(&chip->batt_psy);
		power_supply_changed(chip->usb_psy);

		if (chip->cool_bat_decidegc && chip->warm_bat_decidegc
						&& batt_present) {
			schedule_work(&chip->adc_measure_work);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_dc_dcin_valid_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int dc_present;

	check_unplug_wakelock(chip);

	dc_present = qpnp_chg_is_dc_chg_plugged_in(chip);
	pr_debug("dcin-valid triggered: %d\n", dc_present);

	if (chip->dc_present ^ dc_present) {
		dc_present ?
		(chip->somc_params.main_presents |= MAIN_DC_PRESENT) :
		(chip->somc_params.main_presents &= ~MAIN_DC_PRESENT);
		chip->dc_present = dc_present;
		if (!dc_present) {
			clear_safety_timer_chg_failed(chip);
			qpnp_chg_set_appropriate_vbatdet(chip);
			qpnp_chg_aicl_idc_set(chip, NO_CHANGE_LIMIT);
			qpnp_chg_idcmax_set(chip, chip->somc_params.idc.set);
		} else {
			wake_lock(&chip->eoc_wake_lock);
			schedule_delayed_work(&chip->eoc_work,
				msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
		}
		if (qpnp_chg_is_otg_en_set(chip))
			qpnp_chg_force_run_on_batt(chip, !dc_present ? 1 : 0);
		power_supply_changed(&chip->dc_psy);
		power_supply_changed(&chip->batt_psy);
		schedule_work(&chip->somc_params.dock_work);
	}

	if (!chip->dc_present && !chip->usb_present) {
		chip->chg_done = false;
		notify_input_chg_unplug(chip, true);
		if (chip->somc_params.ovp_chg_dis) {
			chip->somc_params.ovp_chg_dis = false;
			qpnp_chg_usb_suspend_enable(chip, 1);
			qpnp_chg_enable_charge(chip,
					!chip->charging_disabled);
		}
	} else {
		notify_input_chg_unplug(chip, false);
		schedule_work(&chip->somc_params.ovp_check_work);
	}

	return IRQ_HANDLED;
}

#define CHGR_CHG_FAILED_BIT	BIT(7)
static irqreturn_t
qpnp_chg_chgr_chg_failed_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;

	pr_debug("chg_failed triggered\n");

	if (chip->bat_if_base)
		power_supply_changed(&chip->batt_psy);
	power_supply_changed(chip->usb_psy);
	if (chip->dc_chgpth_base)
		power_supply_changed(&chip->dc_psy);
	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_chgr_chg_trklchg_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;

	pr_debug("TRKL IRQ triggered\n");

	chip->chg_done = false;
	if (chip->bat_if_base)
		power_supply_changed(&chip->batt_psy);

	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_chgr_chg_fastchg_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;

	pr_debug("FAST_CHG IRQ triggered\n");
	chip->chg_done = false;
	if (chip->bat_if_base)
		power_supply_changed(&chip->batt_psy);
	power_supply_changed(chip->usb_psy);
	if (chip->dc_chgpth_base)
		power_supply_changed(&chip->dc_psy);
	if (!chip->somc_params.resume_delta_soc)
		qpnp_chg_enable_irq(&chip->chg_vbatdet_lo);

	if (!delayed_work_pending(&chip->eoc_work)) {
		wake_lock(&chip->eoc_wake_lock);
		schedule_delayed_work(&chip->eoc_work,
				msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
	}

	return IRQ_HANDLED;
}

static int
qpnp_batt_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
	case POWER_SUPPLY_PROP_ENABLE_STOP_CHARGING_AT_LOW_BATTERY:
		return 1;
	default:
		break;
	}

	return 0;
}

static int
qpnp_chg_buck_control(struct qpnp_chg_chip *chip, int enable)
{
	int rc;

	if (chip->charging_disabled && enable) {
		pr_debug("Charging disabled\n");
		return 0;
	}

	qpnp_chg_enable_charge(chip, enable);

	return rc;
}

static int
switch_usb_to_charge_mode(struct qpnp_chg_chip *chip)
{
	int rc;

	pr_debug("switch to charge mode\n");
	if (!qpnp_chg_is_otg_en_set(chip))
		return 0;

	/* enable usb ovp fet */
	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
			USB_OTG_EN_BIT,
			0, 1);
	if (rc) {
		pr_err("Failed to turn on usb ovp rc = %d\n", rc);
		return rc;
	}

	rc = qpnp_chg_force_run_on_batt(chip, chip->charging_disabled);
	if (rc) {
		pr_err("Failed re-enable charging rc = %d\n", rc);
		return rc;
	}

	return 0;
}

static int
switch_usb_to_host_mode(struct qpnp_chg_chip *chip)
{
	int rc;

	pr_debug("switch to host mode\n");
	if (qpnp_chg_is_otg_en_set(chip))
		return 0;

	if (!qpnp_chg_is_dc_chg_plugged_in(chip)) {
		rc = qpnp_chg_force_run_on_batt(chip, 1);
		if (rc) {
			pr_err("Failed to disable charging rc = %d\n", rc);
			return rc;
		}
	}

	/* force usb ovp fet off */
	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
			USB_OTG_EN_BIT,
			USB_OTG_EN_BIT, 1);
	if (rc) {
		pr_err("Failed to turn off usb ovp rc = %d\n", rc);
		return rc;
	}

	return 0;
}

static enum power_supply_property pm_power_props_mains[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static enum power_supply_property msm_batt_power_props[] = {
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_ENABLE_STOP_CHARGING_AT_LOW_BATTERY,
};

static char *pm_power_supplied_to[] = {
	"battery",
};

static char *pm_batt_supplied_to[] = {
	"bms",
};

#define USB_WALL_THRESHOLD_MA	500
static int
qpnp_power_get_property_mains(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								dc_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = !!chip->somc_params.main_presents;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = chip->maxinput_dc_ma * MA_TO_UA;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int
get_prop_battery_voltage_now(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct qpnp_vadc_result results;

	if (chip->revision == 0 && chip->type == SMBB) {
		pr_err("vbat reading not supported for 1.0 rc=%d\n", rc);
		return 0;
	} else {
		rc = qpnp_vadc_read(VBAT_SNS, &results);
		if (rc) {
			pr_err("Unable to read vbat rc=%d\n", rc);
			return 0;
		}
		return results.physical;
	}
}

static int
get_prop_batt_present(struct qpnp_chg_chip *chip)
{
	return BATTERY_IS_PRESENT;
}

static void
check_if_health_change(struct qpnp_chg_chip *chip, int health)
{
	if (!chip->somc_params.health_change &&
		chip->somc_params.last_health != health)
		chip->somc_params.health_change = true;
}

#define BATT_TEMP_HOT	BIT(6)
#define BATT_TEMP_OK	BIT(7)
static int
get_prop_batt_health(struct qpnp_chg_chip *chip)
{
	u8 batt_health;
	int rc;
	int health;

	rc = qpnp_chg_read(chip, &batt_health,
				chip->bat_if_base + CHGR_STATUS, 1);
	if (rc) {
		pr_err("Couldn't read battery health read failed rc=%d\n", rc);
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	};

	if (BATT_TEMP_OK & batt_health)
		health = POWER_SUPPLY_HEALTH_GOOD;
	else if (BATT_TEMP_HOT & batt_health)
		health = POWER_SUPPLY_HEALTH_OVERHEAT;
	else
		health = POWER_SUPPLY_HEALTH_COLD;

	check_if_health_change(chip, health);
	return health;
}

static int
get_prop_charge_type(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 chgr_sts;

	if (!get_prop_batt_present(chip))
		return POWER_SUPPLY_CHARGE_TYPE_NONE;

	rc = qpnp_chg_read(chip, &chgr_sts,
				INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read interrupt sts %d\n", rc);
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	if (chgr_sts & TRKL_CHG_ON_IRQ)
		return POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	if (chgr_sts & FAST_CHG_ON_IRQ)
		return POWER_SUPPLY_CHARGE_TYPE_FAST;

	return POWER_SUPPLY_CHARGE_TYPE_NONE;
}

static int
get_prop_batt_status(struct qpnp_chg_chip *chip)
{
	int rc;
	int health;
	u8 chgr_sts;

	if ((qpnp_chg_is_usb_chg_plugged_in(chip) ||
		qpnp_chg_is_dc_chg_plugged_in(chip)) && chip->chg_done) {
		return POWER_SUPPLY_STATUS_FULL;
	}

	health = get_prop_batt_health(chip);
	if (health == POWER_SUPPLY_HEALTH_COLD ||
		health == POWER_SUPPLY_HEALTH_OVERHEAT)
		return POWER_SUPPLY_STATUS_DISCHARGING;

	rc = qpnp_chg_read(chip, &chgr_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read interrupt sts %d\n", rc);
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	if (chgr_sts & TRKL_CHG_ON_IRQ)
		return POWER_SUPPLY_STATUS_CHARGING;
	if (chgr_sts & FAST_CHG_ON_IRQ)
		return POWER_SUPPLY_STATUS_CHARGING;

	return POWER_SUPPLY_STATUS_DISCHARGING;
}

static int
get_prop_current_now(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CURRENT_NOW, &ret);
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 0\n");
	}

	return 0;
}

static int
get_prop_full_design(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &ret);
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 0\n");
	}

	return 0;
}

static int
get_prop_charge_full(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CHARGE_FULL, &ret);
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 0\n");
	}

	return 0;
}

#define DEFAULT_CAPACITY	50
static int
get_prop_capacity(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->use_default_batt_values || !get_prop_batt_present(chip))
		return DEFAULT_CAPACITY;

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CAPACITY, &ret);
		if (ret.intval == 0) {
			if (!qpnp_chg_is_usb_chg_plugged_in(chip)
				&& !qpnp_chg_is_usb_chg_plugged_in(chip))
				pr_warn_ratelimited("Battery 0, CHG absent\n");
		}
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 50\n");
	}

	/* return default capacity to avoid userspace
	 * from shutting down unecessarily */
	return DEFAULT_CAPACITY;
}

#define DEFAULT_TEMP		250
#define MAX_TOLERABLE_BATT_TEMP_DDC	680
static int
get_prop_batt_temp(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct qpnp_vadc_result results;

	if (chip->use_default_batt_values || !get_prop_batt_present(chip))
		return DEFAULT_TEMP;

	if (chip->revision > 0) {
		rc = qpnp_vadc_read(LR_MUX1_BATT_THERM, &results);
		if (rc) {
			pr_debug("Unable to read batt temperature rc=%d\n", rc);
			return 0;
		}
		pr_debug("get_bat_temp %d %lld\n",
			results.adc_code, results.physical);
		return (int)results.physical;
	} else {
		pr_debug("batt temp not supported for PMIC 1.0 rc=%d\n", rc);
	}

	/* return default temperature to avoid userspace
	 * from shutting down unecessarily */
	return DEFAULT_TEMP;
}

static int get_prop_cycle_count(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy)
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CYCLE_COUNT, &ret);
	return ret.intval;
}

#define USB_MAX_CURRENT_MIN 2
static void
qpnp_batt_external_power_changed(struct power_supply *psy)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								batt_psy);
	struct qpnp_somc_params *sp = &chip->somc_params;
	union power_supply_propval ret = {0,};
	int usb_current_max;

	if (!chip)
		return;

	if (!chip->bms_psy)
		chip->bms_psy = power_supply_get_by_name("bms");

	if (!chip->bms_psy || !chip->usb_psy)
		return;

	if (sp && sp->enable_stop_charging_at_low_battery) {
		int current_ua;

		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CAPACITY, &ret);

		current_ua = get_prop_current_now(chip);

		if (ret.intval == 0 && current_ua > 0) {
			pr_info("Disabled charging since detected soc=0 " \
				"current %d uA > 0\n", current_ua);
			chip->charging_disabled = true;
			sp->charging_disabled_for_shutdown = true;
			qpnp_chg_disable_charge_with_batfet(chip,
							BATFET_CLOSE);
		}
	}

	chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_ONLINE, &ret);

	/* Only honour requests while USB is present */
	if (qpnp_chg_is_usb_chg_plugged_in(chip)) {
		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);
		usb_current_max = ret.intval / 1000;
		if (usb_current_max != chip->somc_params.usb_current_max) {
			if (usb_current_max <= USB_MAX_CURRENT_MIN &&
				!chip->use_default_batt_values &&
				get_prop_batt_present(chip)) {
				qpnp_chg_usb_suspend_enable(chip, 1);
				qpnp_chg_aicl_iusb_set(chip, 0);
			} else {
				qpnp_chg_usb_suspend_enable(chip, 0);
				qpnp_chg_aicl_iusb_set(chip, usb_current_max);
			}
			chip->somc_params.usb_current_max = usb_current_max;
		}

		if (chip->somc_params.enable_arb_check_work) {
			cancel_delayed_work_sync(
				&chip->somc_params.arb_check_work);
			schedule_delayed_work(
				&chip->somc_params.arb_check_work,
				round_jiffies_relative(msecs_to_jiffies
					(ARB_CHECK_WAIT_PERIOD_MS_FIRST)));
		}
	}
	if (!delayed_work_pending(&chip->somc_params.aicl_work) &&
	    (qpnp_chg_is_usb_chg_plugged_in(chip) ||
	     qpnp_chg_is_dc_chg_plugged_in(chip))) {
		pr_debug("Start aicl worker\n");
		schedule_delayed_work(
			&chip->somc_params.aicl_work,
			msecs_to_jiffies(AICL_PERIOD_MS));
	}

	if (!delayed_work_pending(&chip->somc_params.health_check_work) &&
	    (qpnp_chg_is_usb_chg_plugged_in(chip) ||
	     qpnp_chg_is_dc_chg_plugged_in(chip))) {
		pr_debug("Start health check worker\n");
		schedule_delayed_work(
			&chip->somc_params.health_check_work,
			msecs_to_jiffies(HEALTH_CHECK_PERIOD_MS));
	}

	if (chip->somc_params.resume_delta_soc) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CAPACITY, &ret);
		if (!wake_lock_active(&chip->eoc_wake_lock) &&
			chip->chg_done && ret.intval <=
			MAX_SOC_CHARGE_LEVEL -
			chip->somc_params.resume_delta_soc) {
			wake_lock(&chip->eoc_wake_lock);
			chip->chg_done = false;
			pr_info("Start recharging\n");
			/* Prevent BAT_FET_OPEN during recharging */
			qpnp_chg_vbatdet_set(chip, chip->max_voltage_mv);
			qpnp_chg_enable_charge(chip,
					!chip->charging_disabled);
			schedule_delayed_work(&chip->eoc_work,
				msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
		}
	}
	pr_debug("end of power supply changed\n");
	power_supply_changed(&chip->batt_psy);
}

static int
qpnp_batt_power_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								batt_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = get_prop_batt_status(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = get_prop_charge_type(chip);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = get_prop_batt_health(chip);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = get_prop_batt_present(chip);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = chip->max_voltage_mv * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = chip->min_voltage_mv * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = get_prop_battery_voltage_now(chip);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = get_prop_batt_temp(chip);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = get_prop_capacity(chip);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = -get_prop_current_now(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = get_prop_full_design(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = get_prop_charge_full(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		val->intval = !(chip->charging_disabled);
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		val->intval = chip->therm_lvl_sel;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = get_prop_cycle_count(chip);
		break;
	case POWER_SUPPLY_PROP_ENABLE_STOP_CHARGING_AT_LOW_BATTERY:
		val->intval =
			chip->somc_params.enable_stop_charging_at_low_battery;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#define QPNP_CHG_VINMIN_MIN_MV		3400
#define QPNP_CHG_VINMIN_HIGH_MIN_MV	5600
#define QPNP_CHG_VINMIN_HIGH_MIN_VAL	0x2B
#define QPNP_CHG_VINMIN_MAX_MV		9600
#define QPNP_CHG_VINMIN_STEP_MV		50
#define QPNP_CHG_VINMIN_STEP_HIGH_MV	200
#define QPNP_CHG_VINMIN_MASK		0x1F
static int
qpnp_chg_vinmin_set(struct qpnp_chg_chip *chip, int voltage)
{
	u8 temp;

	if (voltage < QPNP_CHG_VINMIN_MIN_MV
			|| voltage > QPNP_CHG_VINMIN_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}
	if (voltage >= QPNP_CHG_VINMIN_HIGH_MIN_MV) {
		temp = QPNP_CHG_VINMIN_HIGH_MIN_VAL;
		temp += (voltage - QPNP_CHG_VINMIN_MIN_MV)
			/ QPNP_CHG_VINMIN_STEP_HIGH_MV;
	} else {
		temp = (voltage - QPNP_CHG_VINMIN_MIN_MV)
			/ QPNP_CHG_VINMIN_STEP_MV;
	}

	pr_debug("voltage=%d setting %02x\n", voltage, temp);
	return qpnp_chg_masked_write(chip,
			chip->chgr_base + CHGR_VIN_MIN,
			QPNP_CHG_VINMIN_MASK, temp, 1);
}

#define QPNP_CHG_IBATSAFE_MIN_MA		200
#define QPNP_CHG_IBATSAFE_MAX_MA		3000
#define QPNP_CHG_I_STEP_MA		50
#define QPNP_CHG_I_MIN_MA		100
#define QPNP_CHG_I_MASK			0x3F
static int
qpnp_chg_ibatsafe_set(struct qpnp_chg_chip *chip, int safe_current)
{
	u8 temp;

	if (safe_current < QPNP_CHG_IBATSAFE_MIN_MA
			|| safe_current > QPNP_CHG_IBATSAFE_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", safe_current);
		return -EINVAL;
	}

	temp = safe_current / QPNP_CHG_I_STEP_MA;
	return qpnp_chg_masked_write(chip,
			chip->chgr_base + CHGR_IBAT_SAFE,
			QPNP_CHG_I_MASK, temp, 1);
}

#define QPNP_CHG_ITERM_MIN_MA		100
#define QPNP_CHG_ITERM_MAX_MA		250
#define QPNP_CHG_ITERM_STEP_MA		50
#define QPNP_CHG_ITERM_MASK			0x03
static int
qpnp_chg_ibatterm_set(struct qpnp_chg_chip *chip, int term_current)
{
	u8 temp;

	if (term_current < QPNP_CHG_ITERM_MIN_MA
			|| term_current > QPNP_CHG_ITERM_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", term_current);
		return -EINVAL;
	}

	temp = (term_current - QPNP_CHG_ITERM_MIN_MA)
				/ QPNP_CHG_ITERM_STEP_MA;
	return qpnp_chg_masked_write(chip,
			chip->chgr_base + CHGR_IBAT_TERM_CHGR,
			QPNP_CHG_ITERM_MASK, temp, 1);
}

#define QPNP_CHG_IBATMAX_MIN	50
#define QPNP_CHG_IBATMAX_MAX	3250
static int
qpnp_chg_ibatmax_set(struct qpnp_chg_chip *chip, int chg_current)
{
	u8 temp;

	if (chg_current < QPNP_CHG_IBATMAX_MIN
			|| chg_current > QPNP_CHG_IBATMAX_MAX) {
		pr_err("bad mA=%d asked to set\n", chg_current);
		return -EINVAL;
	}
	temp = chg_current / QPNP_CHG_I_STEP_MA;
	return qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_IBAT_MAX,
			QPNP_CHG_I_MASK, temp, 1);
}

#define QPNP_CHG_TCHG_MASK	0x7F
#define QPNP_CHG_TCHG_MIN	4
#define QPNP_CHG_TCHG_MAX	512
#define QPNP_CHG_TCHG_STEP	4
static int qpnp_chg_tchg_max_set(struct qpnp_chg_chip *chip, int minutes)
{
	u8 temp;

	if (minutes < QPNP_CHG_TCHG_MIN || minutes > QPNP_CHG_TCHG_MAX) {
		pr_err("bad max minutes =%d asked to set\n", minutes);
		return -EINVAL;
	}

	temp = (minutes - 1)/QPNP_CHG_TCHG_STEP;
	return qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_TCHG_MAX,
			QPNP_CHG_TCHG_MASK, temp, 1);
}


#define QPNP_CHG_V_MIN_MV	3240
#define QPNP_CHG_V_MAX_MV	4500
#define QPNP_CHG_V_STEP_MV	10
static int
qpnp_chg_vddsafe_set(struct qpnp_chg_chip *chip, int voltage)
{
	u8 temp;

	if (voltage < QPNP_CHG_V_MIN_MV
			|| voltage > QPNP_CHG_V_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}
	temp = (voltage - QPNP_CHG_V_MIN_MV) / QPNP_CHG_V_STEP_MV;
	pr_debug("voltage=%d setting %02x\n", voltage, temp);
	return qpnp_chg_write(chip, &temp,
		chip->chgr_base + CHGR_VDD_SAFE, 1);
}

#define QPNP_CHG_VDDMAX_MIN	3400
static int
qpnp_chg_vddmax_set(struct qpnp_chg_chip *chip, int voltage)
{
	u8 temp = 0;

	if (voltage < QPNP_CHG_VDDMAX_MIN
			|| voltage > QPNP_CHG_V_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}
	chip->set_vddmax_mv = voltage + chip->delta_vddmax_mv;

	temp = (chip->set_vddmax_mv - QPNP_CHG_V_MIN_MV) / QPNP_CHG_V_STEP_MV;

	pr_debug("voltage=%d setting %02x\n", chip->set_vddmax_mv, temp);
	return qpnp_chg_write(chip, &temp, chip->chgr_base + CHGR_VDD_MAX, 1);
}

#define BOOST_MIN_UV	4200000
#define BOOST_MAX_UV	5500000
#define BOOST_STEP_UV	50000
#define BOOST_MIN	16
#define N_BOOST_V	((BOOST_MAX_UV - BOOST_MIN_UV) / BOOST_STEP_UV + 1)
static int
qpnp_boost_vset(struct qpnp_chg_chip *chip, int voltage)
{
	u8 reg = 0;

	if (voltage < BOOST_MIN_UV || voltage > BOOST_MAX_UV) {
		pr_err("invalid voltage requested %d uV\n", voltage);
		return -EINVAL;
	}

	reg = DIV_ROUND_UP(voltage - BOOST_MIN_UV, BOOST_STEP_UV) + BOOST_MIN;

	pr_debug("voltage=%d setting %02x\n", voltage, reg);
	return qpnp_chg_write(chip, &reg, chip->boost_base + BOOST_VSET, 1);
}

static int
qpnp_boost_vget_uv(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 boost_reg;

	rc = qpnp_chg_read(chip, &boost_reg,
		 chip->boost_base + BOOST_VSET, 1);
	if (rc) {
		pr_err("failed to read BOOST_VSET rc=%d\n", rc);
		return rc;
	}

	if (boost_reg < BOOST_MIN) {
		pr_err("Invalid reading from 0x%x\n", boost_reg);
		return -EINVAL;
	}

	return BOOST_MIN_UV + ((boost_reg - BOOST_MIN) * BOOST_STEP_UV);
}

/* JEITA compliance logic */
static void
qpnp_chg_set_appropriate_vddmax(struct qpnp_chg_chip *chip)
{
	if (chip->bat_is_cool)
		qpnp_chg_vddmax_set(chip, chip->cool_bat_mv);
	else if (chip->bat_is_warm)
		qpnp_chg_vddmax_set(chip, chip->warm_bat_mv);
	else
		qpnp_chg_vddmax_set(chip, chip->max_voltage_mv);
}

static void
qpnp_chg_set_appropriate_vbatdet(struct qpnp_chg_chip *chip)
{
	if (chip->bat_is_cool)
		qpnp_chg_vbatdet_set(chip, chip->cool_bat_mv
			- chip->resume_delta_mv);
	else if (chip->bat_is_warm)
		qpnp_chg_vbatdet_set(chip, chip->warm_bat_mv
			- chip->resume_delta_mv);
	else
		qpnp_chg_vbatdet_set(chip, chip->max_voltage_mv
			- chip->resume_delta_mv);
}

#define QPNP_CHG_IDC_MAX_MASK			0x1F
static int
qpnp_chg_idcmax_get(struct qpnp_chg_chip *chip, int *mA)
{
	int rc = 0;
	u8 dc_reg = 0;

	rc = qpnp_chg_read(chip, &dc_reg,
		chip->dc_chgpth_base + CHGR_I_MAX_REG, 1);
	if (rc) {
		pr_err("idc_max read failed: rc = %d\n", rc);
		return rc;
	}
	dc_reg &= QPNP_CHG_IDC_MAX_MASK;

	if (dc_reg == 0x00)
		*mA = QPNP_CHG_I_MAX_MIN_100;
	else if (dc_reg == 0x01)
		*mA = QPNP_CHG_I_MAX_MIN_150;
	else
		*mA = (int)dc_reg * QPNP_CHG_I_MAXSTEP_MA;

	return rc;
}

#define QPNP_CHG_IUSB_MAX_MASK			0x1F
static int
qpnp_chg_iusbmax_get(struct qpnp_chg_chip *chip, int *mA)
{
	int rc = 0;
	u8 usb_reg = 0;

	rc = qpnp_chg_read(chip, &usb_reg,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);
	if (rc) {
		pr_err("iusb_max read failed: rc = %d\n", rc);
		return rc;
	}
	usb_reg &= QPNP_CHG_IUSB_MAX_MASK;

	if (usb_reg == 0x00)
		*mA = QPNP_CHG_I_MAX_MIN_100;
	else if (usb_reg == 0x01)
		*mA = QPNP_CHG_I_MAX_MIN_150;
	else
		*mA = (int)usb_reg * QPNP_CHG_I_MAXSTEP_MA;

	return rc;
}

static int
qpnp_power_set_property_mains(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								dc_psy);
	int dcp_present;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		dcp_present = val->intval;
		pr_debug("DCP-valid triggered: %d\n", dcp_present);

		if (chip->somc_params.dcp_present ^ dcp_present) {
			dcp_present ?
			(chip->somc_params.main_presents |= MAIN_DCP_PRESENT) :
			(chip->somc_params.main_presents &= ~MAIN_DCP_PRESENT);
			chip->somc_params.dcp_present = dcp_present;
			power_supply_changed(&chip->dc_psy);
		}
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (chip->dc_chgpth_base) {
			qpnp_chg_aicl_idc_set(chip, val->intval / MA_TO_UA);
			chip->maxinput_dc_ma = val->intval / MA_TO_UA;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
qpnp_dc_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	int writeable = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		writeable = 1;
		break;
	default:
		break;
	}

	return writeable;
}

static int
qpnp_chg_ibatmax_get(struct qpnp_chg_chip *chip, int *ibat_ma)
{
	u8 temp;
	int rc;

	rc = qpnp_chg_read(chip, &temp, chip->chgr_base + CHGR_IBAT_MAX, 1);
	if (rc) {
		pr_err("rc = %d while reading ibat max\n", rc);
		*ibat_ma = 0;
		return rc;
	}
	*ibat_ma = (int)(temp & QPNP_CHG_I_MASK) * QPNP_CHG_I_STEP_MA;
	pr_debug("ibat_max = %d ma\n", *ibat_ma);
	return 0;
}

#define QPNP_CHG_VBATDET_MASK 0x7f
static int
qpnp_chg_vbatdet_get(struct qpnp_chg_chip *chip, int *vbatdet_mv)
{
	u8 temp;
	int rc;

	rc = qpnp_chg_read(chip, &temp, chip->chgr_base + CHGR_VBAT_DET, 1);
	if (rc) {
		pr_err("rc = %d while reading vbat_det\n", rc);
		*vbatdet_mv = 0;
		return rc;
	}
	*vbatdet_mv = (int)(temp & QPNP_CHG_VBATDET_MASK) *
			QPNP_CHG_VBATDET_STEP_MV + QPNP_CHG_VBATDET_MIN_MV;
	pr_debug("vbatdet= %d mv\n", *vbatdet_mv);
	return 0;
}

static int
qpnp_chg_vddmax_get(struct qpnp_chg_chip *chip, int *voltage)
{
	u8 temp;
	int rc;

	rc = qpnp_chg_read(chip, &temp, chip->chgr_base + CHGR_VDD_MAX, 1);
	if (rc) {
		pr_err("rc = %d while reading vdd max\n", rc);
		*voltage = 0;
		return rc;
	}
	*voltage = (int)temp * QPNP_CHG_V_STEP_MV + QPNP_CHG_V_MIN_MV;
	pr_debug("voltage= %d mv\n", *voltage);
	return 0;
}

static int
qpnp_chg_smbb_frequency_1p6MHz_set(struct qpnp_chg_chip *chip)
{
	int rc = 0;

	/* frequency 1.6MHz */
	rc = qpnp_chg_masked_write(chip, chip->chgr_base + 0x750,
		0xFF, 0x0B, 1);
	if (rc) {
		pr_err("failed setting frequency 1.6MHz rc=%d\n", rc);
		goto out;
	}

	/* max duty unlimit */
	rc = qpnp_chg_masked_write(chip, chip->chgr_base + 0x1D0,
		0xFF, 0xA5, 1);
	if (rc) {
		pr_err("failed setting max duty unlimit 0x11D0 rc=%d\n", rc);
		goto out;
	}
	rc = qpnp_chg_masked_write(chip, chip->chgr_base + 0x1E6,
		0xFF, 0x00, 1);
	if (rc)
		pr_err("failed setting max duty unlimit 0x11E6 rc=%d\n", rc);

out:
	return rc;
}

static void
check_unplug_wakelock(struct qpnp_chg_chip *chip)
{
	if (!qpnp_chg_is_usb_chg_plugged_in(chip) &&
		!qpnp_chg_is_dc_chg_plugged_in(chip)) {
		pr_debug("Set unplug wake_lock\n");
		wake_lock_timeout(&chip->somc_params.unplug_wake_lock,
				UNPLUG_WAKELOCK_TIME_SEC);
	}
}

static void
clear_safety_timer_chg_failed(struct qpnp_chg_chip *chip)
{
	int rc = 0;

	pr_info("clear CHG_FAILED_IRQ triggered\n");
	rc = qpnp_chg_masked_write(chip,
		chip->chgr_base + CHGR_CHG_FAILED,
		CHGR_CHG_FAILED_BIT,
		CHGR_CHG_FAILED_BIT, 1);
	if (rc)
		pr_err("Failed to write chg_fail clear bit!\n");
}

static void
notify_input_chg_unplug(struct qpnp_chg_chip *chip, bool value)
{
	if (chip->somc_params.chg_unplug_key) {
		input_report_key(chip->somc_params.chg_unplug_key,
				 KEY_F24, value ? 1 : 0);
		input_sync(chip->somc_params.chg_unplug_key);
	}
}

#define OVP_THRESHOLD_VOLTAGE_UV 6500000
static bool
check_if_over_voltage(struct qpnp_chg_chip *chip)
{
	int dcin_rc = -1;
	int usbin_rc = -1;
	struct qpnp_vadc_result dcin_res;
	struct qpnp_vadc_result usbin_res;

	if (chip->dc_present) {
		dcin_rc = qpnp_vadc_read(DCIN, &dcin_res);
		if (dcin_rc) {
			pr_err("Unable to dcin read vadc rc=%d\n", dcin_rc);
		} else if (dcin_res.physical >= OVP_THRESHOLD_VOLTAGE_UV) {
			pr_info("detected OVP in DCIN %lld\n",
				dcin_res.physical);
			return true;
		}
	}

	if (chip->usb_present) {
		usbin_rc = qpnp_vadc_read(USBIN, &usbin_res);
		if (usbin_rc) {
			pr_err("Unable to usbin read vadc rc=%d\n", usbin_rc);
		} else if (usbin_res.physical >= OVP_THRESHOLD_VOLTAGE_UV) {
			pr_info("detected OVP in USB %lld\n",
				usbin_res.physical);
			return true;
		}
	}

	return false;
}

static void
qpnp_ovp_check_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
		struct qpnp_chg_chip, somc_params.ovp_check_work);

	if (check_if_over_voltage(chip)) {
		pr_info("Disabled charging since detected OVP\n");
		chip->somc_params.ovp_chg_dis = true;
		qpnp_chg_disable_charge_with_batfet(chip, BATFET_CLOSE);
		qpnp_chg_usb_suspend_enable(chip, 1);
	}
}

static void
qpnp_health_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
					struct qpnp_chg_chip,
					somc_params.health_check_work);

	if (!qpnp_chg_is_usb_chg_plugged_in(chip) &&
		!qpnp_chg_is_dc_chg_plugged_in(chip)) {
		pr_info("stopping worker usb/dc disconnected");
		goto health_check_work_exit;
	}

	chip->somc_params.last_health = get_prop_batt_health(chip);
	if (chip->somc_params.health_change) {
		chip->somc_params.health_change = false;
		power_supply_changed(&chip->batt_psy);
	}

	schedule_delayed_work(&chip->somc_params.health_check_work,
		msecs_to_jiffies(HEALTH_CHECK_PERIOD_MS));

health_check_work_exit:
	return;
}

static void
qpnp_smbb_sw_controlled_clk_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
		struct qpnp_chg_chip, somc_params.smbb_clk_work);
	int rc;
	u8 sec = 0xA5;
	u8 sw_ctrl = 0x07;

	chip->somc_params.workaround_batfet_close = true;
	qpnp_chg_disable_charge_with_batfet(chip, BATFET_CLOSE);

	pr_info("usb coarse det: SW clock\n");

	rc = qpnp_chg_write(chip, &sec,
			chip->misc_base + SEC_ACCESS, 1);
	if (rc) {
		pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
		goto exit_sw_clk;
	}

	/* Enable SW controlled 19p2M clk */
	rc = qpnp_chg_write(chip, &sw_ctrl,
			chip->misc_base + 0xE2, 1);
	if (rc) {
		pr_err("failed to set SMBB SW control clk 19p2M:%d\n", rc);
		goto exit_sw_clk;
	}
exit_sw_clk:
	/* Schedule HW controlled clk */
	schedule_delayed_work(&chip->somc_params.smbb_hw_clk_work,
		round_jiffies_relative(msecs_to_jiffies
			(SMBB_HW_CONTROLLED_CLK_MS)));

}

static void
qpnp_smbb_hw_clk_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
					struct qpnp_chg_chip,
					somc_params.smbb_hw_clk_work);
	int rc;
	u8 sec = 0xA5;
	u8 hw_ctrl = 0x0;

	pr_info("usb coarse det: HW clock\n");

	rc = qpnp_chg_write(chip, &sec,
			chip->misc_base + SEC_ACCESS, 1);
	if (rc) {
		pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
		goto exit_hw_clk;
	}

	/* HW controlled clk */
	rc = qpnp_chg_write(chip, &hw_ctrl,
			chip->misc_base + 0xE2, 1);
	if (rc) {
		pr_err("failed to set SMBB HW controlled clk :%d\n", rc);
		goto exit_hw_clk;
	}
exit_hw_clk:
	qpnp_chg_enable_charge(chip, !chip->charging_disabled);
	chip->somc_params.workaround_batfet_close = false;
}


#define MIN_PON_SEL_MASK	0x3
#define MIN_PON_100NS		0x3
#define MIN_PON_50NS		0x2
#define ARB_VIN_MIN_MV		5500
#define BMS_SIGN_BIT		BIT(7)
#define CHK_RETRY_MAX_CNT	5
#define CHK_SLEEP_TIME_MS	5
static void qpnp_chg_arb_check_worker(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip,
				somc_params.arb_check_work);
	u8 ibat_sts;
	int rc = 0;
	u8 bat_fet_sts;
	int i;
	int iusb_max_restore;

	if (!qpnp_chg_is_usb_chg_plugged_in(chip)) {
		pr_debug("stopping worker usb disconnected");
		return;
	}

	rc = qpnp_chg_read(chip, &ibat_sts, chip->chgr_base + CHGR_IBAT_STS, 1);
	if (rc)
		pr_err("IBAT_STS read failed: rc=%d\n", rc);

	if (ibat_sts & BMS_SIGN_BIT) {
		/* Charging so return */
		pr_debug("charging - check again later\n");
		goto check_again_later;
	}

	rc = qpnp_chg_read(chip, &bat_fet_sts,
		chip->bat_if_base + CHGR_BAT_IF_BAT_FET_STATUS, 1);
	if (rc)
		pr_err("BAT_FET_STATUS read failed: rc=%d\n", rc);

	if (bat_fet_sts & CHGR_BAT_FET_ON) {
		pr_debug("BAT_FET is ON\n");
	} else {
		pr_debug("BAT_FET is OFF - check again later\n");
		goto check_again_later;
	}

	pr_debug("set usb_suspend\n");
	rc = qpnp_chg_usb_suspend_enable(chip, 1);
	if (rc)
		pr_err("failed setting usb_suspend enable rc=%d\n", rc);

	for (i = 0; i < CHK_RETRY_MAX_CNT; i++) {
		msleep(CHK_SLEEP_TIME_MS);
		if (!qpnp_chg_is_usb_chg_plugged_in(chip)) {
			pr_debug("Charger removal detected\n");
			break;
		}
	}

	iusb_max_restore = chip->somc_params.iusb_max;
	rc = qpnp_chg_iusbmax_set(chip, QPNP_CHG_I_MAX_MIN_100);
	if (rc)
		pr_err("failed setting iusbmax rc=%d\n", rc);

	rc = qpnp_chg_usb_suspend_enable(chip, 0);
	if (rc)
		pr_err("failed setting usb_suspend disable rc=%d\n", rc);

	if (!qpnp_chg_is_usb_chg_plugged_in(chip)) {
		pr_debug("Charger removal detected and stopping worker\n");
		return;
	} else {
		pr_debug("iusb_max_restore = %d\n", iusb_max_restore);
		rc = qpnp_chg_iusbmax_set(chip, iusb_max_restore);
		if (rc)
			pr_err("failed setting iusbmax rc=%d\n", rc);
	}
check_again_later:
	schedule_delayed_work(&chip->somc_params.arb_check_work,
		round_jiffies_relative(msecs_to_jiffies
			(ARB_CHECK_WAIT_PERIOD_MS)));
}

#define CHG_DONE_PERIOD_MS 5000
static void check_chg_done_fastchg(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork, struct qpnp_chg_chip,
					somc_params.chg_done_fastchg_work);
	int rc;
	u8 chgr_sts;

	rc = qpnp_chg_read(chip, &chgr_sts,
			INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read interrupt sts %d\n", rc);
		goto check_again;
	}

	pr_debug("chgr_sts: 0x%x\n", chgr_sts);

	if (chgr_sts & FAST_CHG_ON_IRQ)
		qpnp_chg_chgr_chg_fastchg_irq_handler(chip->chg_fastchg.irq,
							chip);
check_again:
	schedule_delayed_work(&chip->somc_params.chg_done_fastchg_work,
			msecs_to_jiffies(CHG_DONE_PERIOD_MS));
}

static void
qpnp_poll_chg_done_fastchg_irqs(struct qpnp_chg_chip *chip)
{
	disable_irq(chip->chg_fastchg.irq);
	INIT_DELAYED_WORK(&chip->somc_params.chg_done_fastchg_work,
			check_chg_done_fastchg);
	schedule_delayed_work(&chip->somc_params.chg_done_fastchg_work,
			msecs_to_jiffies(CHG_DONE_PERIOD_MS));
}

static void
qpnp_chg_aicl_set_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
		struct qpnp_chg_chip, somc_params.aicl_set_work);
	int rc;

	rc = qpnp_chg_iusbmax_set(chip, chip->somc_params.iusb.set);
	if (rc)
		pr_err("IUSB_MAX setting failed: rc = %d\n", rc);
}

#define ARB_STOP_WORK_MS	500
static void
qpnp_chg_exec_rb_workaround(struct qpnp_chg_chip *chip)
{
	qpnp_chg_disable_charge_with_batfet(chip, BATFET_CLOSE);
	schedule_delayed_work(&chip->arb_stop_work,
		msecs_to_jiffies(ARB_STOP_WORK_MS));
}

static void
qpnp_chg_aicl_iusb_set(struct qpnp_chg_chip *chip, int limit_ma)
{
	unsigned long flags;

	pr_debug("Set limit = %d\n", limit_ma);

	spin_lock_irqsave(&aicl_lock, flags);
	if (limit_ma != NO_CHANGE_LIMIT)
		chip->somc_params.iusb.limit = limit_ma;
	chip->somc_params.iusb.set = QPNP_CHG_I_MAX_MIN_100;
	spin_unlock_irqrestore(&aicl_lock, flags);
}

static void
qpnp_chg_aicl_idc_set(struct qpnp_chg_chip *chip, int limit_ma)
{
	unsigned long flags;

	pr_debug("Set limit = %d\n", limit_ma);

	spin_lock_irqsave(&aicl_lock, flags);
	if (limit_ma != NO_CHANGE_LIMIT)
		chip->somc_params.idc.limit = limit_ma;
	chip->somc_params.idc.set = QPNP_CHG_I_MAX_MIN_100;
	spin_unlock_irqrestore(&aicl_lock, flags);
}

static int
qpnp_chg_current_step_increase(struct qpnp_chg_chip *chip, int ma)
{
	switch (ma) {
	case QPNP_CHG_I_MAX_MIN_100:
		ma = QPNP_CHG_I_MAX_MIN_150;
		break;
	case QPNP_CHG_I_MAX_MIN_150:
		ma = QPNP_CHG_I_MAX_MIN_200;
		break;
	default:
		ma += QPNP_CHG_I_MAXSTEP_MA;
		break;
	}
	if (ma > QPNP_CHG_I_MAX_MAX_MA)
		ma = QPNP_CHG_I_MAX_MAX_MA;
	return ma;
}

static void
qpnp_chg_check_unpluged_charger(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 ibat_sts;
	u8 chg_gone_sts;

	pr_debug("checking unplugged charger.\n");

	rc = qpnp_chg_read(chip, &ibat_sts,
		chip->chgr_base + CHGR_IBAT_STS, 1);
	if (!rc)
		rc = qpnp_chg_read(chip, &chg_gone_sts,
			INT_RT_STS(chip->usb_chgpth_base), 1);

	if (!rc && !(ibat_sts & BMS_SIGN_BIT) &&
		(chg_gone_sts & CHG_GONE_IRQ) &&
		!chip->somc_params.charging_disabled_for_shutdown &&
		!chip->somc_params.workaround_batfet_close) {
		pr_info("Assume reverse boost issue happens\n");
		chip->somc_params.kick_rb_workaround = true;
		qpnp_chg_exec_rb_workaround(chip);
	}
}
static int
qpnp_chg_vchg_get(struct qpnp_chg_chip *chip, int *ma)
{
	int rc;
	struct qpnp_vadc_result results;

	rc = qpnp_vadc_read(VCHG_SNS, &results);
	if (rc) {
		pr_err("VCHG read failed: rc=%d\n", rc);
		return rc;
	}
	*ma = (int)results.physical / 1000;
	return 0;
}

#define VCHG_AICL_THRESHOLD	4200
static int
qpnp_chg_somc_aicl(struct qpnp_chg_chip *chip)
{
	unsigned long flags;
	int idc_ma = 0;
	int iusb_ma = 0;
	int vchg_mv = 0;
	int dc_present;
	int rc;
	bool aicl_working = false;

	rc = qpnp_chg_vchg_get(chip, &vchg_mv);
	if (rc)
		goto aicl_exit;

	spin_lock_irqsave(&aicl_lock, flags);

	iusb_ma = chip->somc_params.iusb.set;
	idc_ma = chip->somc_params.idc.set;

	/*
	 * If aicl worker is started and usb max current is not passed from
	 * usb driver, set the flag true to avoid checking unplugged charger.
	 */
	if (qpnp_chg_is_usb_chg_plugged_in(chip) &&
		!qpnp_chg_is_dc_chg_plugged_in(chip) &&
		!chip->somc_params.iusb.limit) {
		pr_debug("Max current is not passed.\n");
		aicl_working = true;
		goto aicl_unlock;
	}

	pr_debug("iusb/idc/iusb_lim/idc_lim/vchg/vchg_th: %d,%d,%d,%d,%d,%d\n",
		iusb_ma, idc_ma,
		chip->somc_params.iusb.limit, chip->somc_params.idc.limit,
		vchg_mv, VCHG_AICL_THRESHOLD);

	/* AICL in IDC */
	dc_present = qpnp_chg_is_dc_chg_plugged_in(chip);
	if (dc_present &&
		idc_ma < chip->somc_params.idc.limit &&
		vchg_mv > VCHG_AICL_THRESHOLD) {
		chip->somc_params.idc.set =
			qpnp_chg_current_step_increase(chip, idc_ma);
		aicl_working = true;
	}

	if (dc_present)
		goto aicl_unlock;

	/* AICL in IUSB */
	if (qpnp_chg_is_usb_chg_plugged_in(chip) &&
		iusb_ma < chip->somc_params.iusb.limit &&
		vchg_mv > VCHG_AICL_THRESHOLD) {
		chip->somc_params.iusb.set =
			qpnp_chg_current_step_increase(chip, iusb_ma);
		aicl_working = true;
	}
aicl_unlock:
	spin_unlock_irqrestore(&aicl_lock, flags);

	if (chip->somc_params.idc.set != idc_ma) {
		rc = qpnp_chg_idcmax_set(chip, chip->somc_params.idc.set);
		if (!rc) {
			pr_debug("Increased IDC_MAX: %d -> %d\n",
				idc_ma, chip->somc_params.idc.set);
		} else {
			pr_err("IDC_MAX setting failed: rc = %d\n", rc);
			chip->somc_params.idc.set = idc_ma;
		}
	}
	if (chip->somc_params.iusb.set != iusb_ma) {
		rc = qpnp_chg_iusbmax_set(chip, chip->somc_params.iusb.set);
		if (!rc) {
			pr_debug("Increased IUSB_MAX: %d -> %d\n",
				iusb_ma, chip->somc_params.iusb.set);
		} else {
			pr_err("IUSB_MAX setting failed: rc = %d\n", rc);
			chip->somc_params.iusb.set = iusb_ma;
		}
	}

aicl_exit:
	return aicl_working;
}

#define DISC_DETECT_COUNT_MAX 3

static void
qpnp_chg_aicl_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork, struct qpnp_chg_chip,
				somc_params.aicl_work);

	if (!qpnp_chg_is_usb_chg_plugged_in(chip) &&
		!qpnp_chg_is_dc_chg_plugged_in(chip)) {
		if (++chip->somc_params.disconnect_count >=
			DISC_DETECT_COUNT_MAX) {
			chip->somc_params.kick_rb_workaround = false;
			chip->somc_params.disconnect_count = 0;
			power_supply_set_present(chip->usb_psy, 0);
			power_supply_set_present(&chip->dc_psy, 0);
			pr_info("stopping worker usb/dc disconnected");
			goto aicl_work_exit;
		}
	} else {
		chip->somc_params.disconnect_count = 0;
	}

	if (chip->somc_params.kick_rb_workaround) {
		pr_info("Retry worker\n");
		goto aicl_work_again;
	}

	/* If AICL is working, skip unplug check */
	if (!qpnp_chg_somc_aicl(chip))
		qpnp_chg_check_unpluged_charger(chip);

aicl_work_again:
	schedule_delayed_work(&chip->somc_params.aicl_work,
		msecs_to_jiffies(AICL_PERIOD_MS));

aicl_work_exit:
	return;
}

static void create_dock_event(struct qpnp_chg_chip *chip,
				enum dock_state_event event)
{
	struct qpnp_somc_params *sp = &chip->somc_params;

	if (sp->dock_event != event) {
		wake_lock_timeout(&sp->dock_lock, HZ / 2);
		switch_set_state(&sp->swdev, event);
		sp->dock_event = event;
		pr_debug("dock_event = %d\n", sp->dock_event);
	}
}

static void dock_worker(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip,
				somc_params.dock_work);

	if (chip->somc_params.enabling_regulator_boost)
		return;

	if (chip->dc_present)
		create_dock_event(chip, CHG_DOCK_DESK);
	else
		create_dock_event(chip, CHG_DOCK_UNDOCK);
}

static int register_dock_event(struct qpnp_chg_chip *chip)
{
	int rc;

	wake_lock_init(&chip->somc_params.dock_lock,
			WAKE_LOCK_SUSPEND, "qpnp_dock");
	INIT_WORK(&chip->somc_params.dock_work, dock_worker);

	chip->somc_params.swdev.name = "dock";
	rc = switch_dev_register(&chip->somc_params.swdev);
	if (rc < 0)
		pr_err("switch_dev_register failed rc = %d\n", rc);
	return rc;
}

static void
qpnp_chg_set_appropriate_battery_current(struct qpnp_chg_chip *chip)
{
	unsigned int chg_current = chip->max_bat_chg_current;

	if (chip->bat_is_cool)
		chg_current = min(chg_current, chip->cool_bat_chg_ma);

	if (chip->bat_is_warm)
		chg_current = min(chg_current, chip->warm_bat_chg_ma);

	if (chip->therm_lvl_sel != 0 && chip->thermal_mitigation)
		chg_current = min(chg_current,
			chip->thermal_mitigation[chip->therm_lvl_sel]);

	pr_debug("setting %d mA\n", chg_current);
	qpnp_chg_ibatmax_set(chip, chg_current);
}

static void
qpnp_batt_system_temp_level_set(struct qpnp_chg_chip *chip, int lvl_sel)
{
	if (lvl_sel >= 0 && lvl_sel < chip->thermal_levels) {
		chip->therm_lvl_sel = lvl_sel;
		if (lvl_sel == (chip->thermal_levels - 1)) {
			/* disable charging if highest value selected */
			qpnp_chg_buck_control(chip, 0);
		} else {
			qpnp_chg_buck_control(chip, 1);
			qpnp_chg_set_appropriate_battery_current(chip);
		}
	} else {
		pr_err("Unsupported level selected %d\n", lvl_sel);
	}
}

/* OTG regulator operations */
static int
qpnp_chg_regulator_otg_enable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return switch_usb_to_host_mode(chip);
}

static int
qpnp_chg_regulator_otg_disable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return switch_usb_to_charge_mode(chip);
}

static int
qpnp_chg_regulator_otg_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return qpnp_chg_is_otg_en_set(chip);
}

static int
qpnp_chg_regulator_boost_enable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc;

	chip->somc_params.enabling_regulator_boost = true;

	if (qpnp_chg_is_usb_chg_plugged_in(chip) &&
			(chip->flags & BOOST_FLASH_WA)) {
		qpnp_chg_usb_suspend_enable(chip, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + COMP_OVR1,
			0xFF,
			0x2F, 1);
		if (rc) {
			pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
			return rc;
		}
	}

	return qpnp_chg_masked_write(chip,
		chip->boost_base + BOOST_ENABLE_CONTROL,
		BOOST_PWR_EN,
		BOOST_PWR_EN, 1);
}

/* Boost regulator operations */
#define ABOVE_VBAT_WEAK		BIT(1)
static int
qpnp_chg_regulator_boost_disable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc;
	u8 vbat_sts;

	rc = qpnp_chg_masked_write(chip,
		chip->boost_base + BOOST_ENABLE_CONTROL,
		BOOST_PWR_EN,
		0, 1);
	if (rc) {
		pr_err("failed to disable boost rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_read(chip, &vbat_sts,
			chip->chgr_base + CHGR_VBAT_STATUS, 1);
	if (rc) {
		pr_err("failed to read bat sts rc=%d\n", rc);
		return rc;
	}

	if (!(vbat_sts & ABOVE_VBAT_WEAK) && (chip->flags & BOOST_FLASH_WA)) {
		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + COMP_OVR1,
			0xFF,
			0x20, 1);
		if (rc) {
			pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
			return rc;
		}

		usleep(2000);

		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + COMP_OVR1,
			0xFF,
			0x00, 1);
		if (rc) {
			pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
			return rc;
		}
	}

	if (qpnp_chg_is_usb_chg_plugged_in(chip)
			&& (chip->flags & BOOST_FLASH_WA)) {
		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + COMP_OVR1,
			0xFF,
			0x00, 1);
		if (rc) {
			pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
			return rc;
		}

		usleep(1000);

		qpnp_chg_usb_suspend_enable(chip, 0);
	}

	chip->somc_params.enabling_regulator_boost = false;
	return rc;
}

static int
qpnp_chg_regulator_boost_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return qpnp_chg_is_boost_en_set(chip);
}

static int
qpnp_chg_regulator_boost_set_voltage(struct regulator_dev *rdev,
		int min_uV, int max_uV, unsigned *selector)
{
	int uV = min_uV;
	int rc;
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	if (uV < BOOST_MIN_UV && max_uV >= BOOST_MIN_UV)
		uV = BOOST_MIN_UV;


	if (uV < BOOST_MIN_UV || uV > BOOST_MAX_UV) {
		pr_err("request %d uV is out of bounds\n", uV);
		return -EINVAL;
	}

	*selector = DIV_ROUND_UP(uV - BOOST_MIN_UV, BOOST_STEP_UV);
	if ((*selector * BOOST_STEP_UV + BOOST_MIN_UV) > max_uV) {
		pr_err("no available setpoint [%d, %d] uV\n", min_uV, max_uV);
		return -EINVAL;
	}

	rc = qpnp_boost_vset(chip, uV);

	return rc;
}

static int
qpnp_chg_regulator_boost_get_voltage(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return qpnp_boost_vget_uv(chip);
}

static int
qpnp_chg_regulator_boost_list_voltage(struct regulator_dev *rdev,
			unsigned selector)
{
	if (selector >= N_BOOST_V)
		return 0;

	return BOOST_MIN_UV + (selector * BOOST_STEP_UV);
}

static struct regulator_ops qpnp_chg_otg_reg_ops = {
	.enable			= qpnp_chg_regulator_otg_enable,
	.disable		= qpnp_chg_regulator_otg_disable,
	.is_enabled		= qpnp_chg_regulator_otg_is_enabled,
};

static struct regulator_ops qpnp_chg_boost_reg_ops = {
	.enable			= qpnp_chg_regulator_boost_enable,
	.disable		= qpnp_chg_regulator_boost_disable,
	.is_enabled		= qpnp_chg_regulator_boost_is_enabled,
	.set_voltage		= qpnp_chg_regulator_boost_set_voltage,
	.get_voltage		= qpnp_chg_regulator_boost_get_voltage,
	.list_voltage		= qpnp_chg_regulator_boost_list_voltage,
};

#define MIN_DELTA_MV_TO_INCREASE_VDD_MAX	13
#define MAX_DELTA_VDD_MAX_MV			50
static void
qpnp_chg_adjust_vddmax(struct qpnp_chg_chip *chip, int vbat_mv)
{
	int delta_mv, closest_delta_mv, sign;

	delta_mv = chip->max_voltage_mv - vbat_mv;
	if (delta_mv > 0 && delta_mv < MIN_DELTA_MV_TO_INCREASE_VDD_MAX) {
		pr_debug("vbat is not low enough to increase vdd\n");
		return;
	}

	sign = delta_mv > 0 ? 1 : -1;
	closest_delta_mv = ((delta_mv + sign * QPNP_CHG_V_STEP_MV / 2)
			/ QPNP_CHG_V_STEP_MV) * QPNP_CHG_V_STEP_MV;
	pr_debug("max_voltage = %d, vbat_mv = %d, delta_mv = %d, closest = %d\n",
			chip->max_voltage_mv, vbat_mv,
			delta_mv, closest_delta_mv);
	chip->delta_vddmax_mv = clamp(chip->delta_vddmax_mv + closest_delta_mv,
			-MAX_DELTA_VDD_MAX_MV, MAX_DELTA_VDD_MAX_MV);
	pr_debug("using delta_vddmax_mv = %d\n", chip->delta_vddmax_mv);
	qpnp_chg_set_appropriate_vddmax(chip);
}

#define CONSECUTIVE_COUNT	3
static void
qpnp_eoc_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, eoc_work);
	static int count;
	int ibat_ma, vbat_mv, rc = 0;
	u8 batt_sts = 0, buck_sts = 0, chg_sts = 0;
	int capacity;

	if (!chip->somc_params.kick_rb_workaround &&
	    !chip->somc_params.charging_disabled_for_shutdown &&
	    !chip->somc_params.workaround_batfet_close)
		qpnp_chg_enable_charge(chip, !chip->charging_disabled);

	rc = qpnp_chg_read(chip, &batt_sts, INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("failed to read batt_if rc=%d\n", rc);
		return;
	}

	rc = qpnp_chg_read(chip, &buck_sts, INT_RT_STS(chip->buck_base), 1);
	if (rc) {
		pr_err("failed to read buck rc=%d\n", rc);
		return;
	}

	rc = qpnp_chg_read(chip, &chg_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read chg_sts rc=%d\n", rc);
		return;
	}

	pr_debug("chgr: 0x%x, bat_if: 0x%x, buck: 0x%x\n",
		chg_sts, batt_sts, buck_sts);

	if (!qpnp_chg_is_usb_chg_plugged_in(chip) &&
			!qpnp_chg_is_dc_chg_plugged_in(chip)) {
		pr_debug("no chg connected, stopping\n");
		goto stop_eoc;
	}

	if ((batt_sts & BAT_FET_ON_IRQ) && (chg_sts & FAST_CHG_ON_IRQ
					|| chg_sts & TRKL_CHG_ON_IRQ)) {
		ibat_ma = get_prop_current_now(chip) / 1000;
		vbat_mv = get_prop_battery_voltage_now(chip) / 1000;

		pr_debug("ibat_ma = %d vbat_mv = %d term_current_ma = %d\n",
				ibat_ma, vbat_mv, chip->term_current);

		if (!chip->somc_params.resume_delta_soc &&
			(!(chg_sts & VBAT_DET_LOW_IRQ)) && (vbat_mv <
			(chip->max_voltage_mv - chip->resume_delta_mv))) {
			pr_debug("woke up too early\n");
			qpnp_chg_enable_irq(&chip->chg_vbatdet_lo);
			goto stop_eoc;
		}

		if (buck_sts & VDD_LOOP_IRQ)
			qpnp_chg_adjust_vddmax(chip, vbat_mv);

		if (!(buck_sts & VDD_LOOP_IRQ)) {
			pr_debug("Not in CV\n");
			count = 0;
		} else if ((ibat_ma * -1) > chip->term_current) {
			pr_debug("Not at EOC, battery current too high\n");
			count = 0;
		} else if (ibat_ma > 0) {
			pr_debug("Charging but system demand increased\n");
			count = 0;
		} else if (vbat_mv >= chip->max_voltage_mv -
				chip->resume_delta_mv) {
			if (chip->somc_params.workaround_batfet_close) {
				pr_info("Skip EOC since the workaround\n");
			} else if (count >= CONSECUTIVE_COUNT) {
				pr_info("End of Charging\n");
				qpnp_chg_disable_charge_with_batfet(chip,
								BATFET_OPEN);
				chip->chg_done = true;
				power_supply_changed(&chip->batt_psy);
				if (!chip->somc_params.resume_delta_soc)
					qpnp_chg_enable_irq(
						&chip->chg_vbatdet_lo);
				goto stop_eoc;
			} else {
				count += 1;
				pr_debug("EOC count = %d\n", count);
			}
		}
	} else {
		pr_debug("not charging\n");
		capacity = get_prop_capacity(chip);
		if (capacity >= MAX_SOC_CHARGE_LEVEL) {
			if (qpnp_chg_is_usb_chg_plugged_in(chip) &&
				!qpnp_chg_is_dc_chg_plugged_in(chip) &&
				chip->somc_params.usb_current_max <=
				USB_MAX_CURRENT_MIN) {
				pr_info("max charge current isn't notified\n");
			} else if (!chip->somc_params.workaround_batfet_close) {
				pr_info("Assumed End of Charging\n");
				qpnp_chg_disable_charge_with_batfet(chip,
								BATFET_OPEN);
				chip->chg_done = true;
				power_supply_changed(&chip->batt_psy);
				if (!chip->somc_params.resume_delta_soc)
					qpnp_chg_enable_irq(
						&chip->chg_vbatdet_lo);
				goto stop_eoc;
			}
		}
	}
	schedule_delayed_work(&chip->eoc_work,
		msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
	return;

stop_eoc:
	count = 0;
	qpnp_chg_set_appropriate_vbatdet(chip);
	wake_unlock(&chip->eoc_wake_lock);
}

#define HYSTERISIS_DECIDEGC 20
static void
qpnp_chg_adc_notification(enum qpnp_tm_state state, void *ctx)
{
	struct qpnp_chg_chip *chip = ctx;
	bool bat_warm = 0, bat_cool = 0;
	int temp;

	if (state >= ADC_TM_STATE_NUM) {
		pr_err("invalid notification %d\n", state);
		return;
	}

	temp = get_prop_batt_temp(chip);

	pr_debug("temp = %d state = %s\n", temp,
			state == ADC_TM_WARM_STATE ? "warm" : "cool");

	if (state == ADC_TM_WARM_STATE) {
		if (temp > chip->warm_bat_decidegc) {
			/* Normal to warm */
			bat_warm = true;
			bat_cool = false;
			chip->adc_param.low_temp =
				chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC;
			chip->adc_param.state_request =
				ADC_TM_COOL_THR_ENABLE;

			if (chip->somc_params.warm_disable_charging &&
				chip->thermal_mitigation &&
				!chip->somc_params.
					enable_stop_charging_at_low_battery)
				qpnp_batt_system_temp_level_set(chip,
						chip->thermal_levels - 1);
		} else if (temp >
				chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC){
			/* Cool to normal */
			bat_warm = false;
			bat_cool = false;

			chip->adc_param.low_temp = chip->cool_bat_decidegc;
			chip->adc_param.high_temp = chip->warm_bat_decidegc;
			chip->adc_param.state_request =
					ADC_TM_HIGH_LOW_THR_ENABLE;
		}
	} else {
		if (temp < chip->cool_bat_decidegc) {
			/* Normal to cool */
			bat_warm = false;
			bat_cool = true;
			chip->adc_param.high_temp =
				chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC;
			chip->adc_param.state_request =
				ADC_TM_WARM_THR_ENABLE;
		} else if (temp <
				chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC){
			/* Warm to normal */
			bat_warm = false;
			bat_cool = false;

			chip->adc_param.low_temp = chip->cool_bat_decidegc;
			chip->adc_param.high_temp = chip->warm_bat_decidegc;
			chip->adc_param.state_request =
					ADC_TM_HIGH_LOW_THR_ENABLE;

			if (chip->somc_params.warm_disable_charging &&
				chip->thermal_mitigation &&
				!chip->somc_params.
					enable_stop_charging_at_low_battery)
				qpnp_batt_system_temp_level_set(chip, 0);
		}
	}

	if (chip->bat_is_cool ^ bat_cool || chip->bat_is_warm ^ bat_warm) {
		chip->bat_is_cool = bat_cool;
		chip->bat_is_warm = bat_warm;

		/* set appropriate voltages and currents */
		qpnp_chg_set_appropriate_vddmax(chip);
		qpnp_chg_set_appropriate_battery_current(chip);
		qpnp_chg_set_appropriate_vbatdet(chip);
	}

	if (qpnp_adc_tm_channel_measure(&chip->adc_param))
		pr_err("request ADC error\n");
}

static int
qpnp_batt_power_set_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								batt_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		chip->charging_disabled = !(val->intval);
		qpnp_chg_enable_charge(chip, !chip->charging_disabled);
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		qpnp_batt_system_temp_level_set(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_ENABLE_STOP_CHARGING_AT_LOW_BATTERY:
		chip->somc_params.enable_stop_charging_at_low_battery =
							(bool)val->intval;
		if (chip->somc_params.warm_disable_charging &&
			chip->thermal_mitigation &&
			chip->somc_params.enable_stop_charging_at_low_battery)
			qpnp_batt_system_temp_level_set(chip, 0);
		break;
	default:
		return -EINVAL;
	}

	power_supply_changed(&chip->batt_psy);
	return 0;
}

static void
qpnp_chg_setup_flags(struct qpnp_chg_chip *chip)
{
	if (chip->revision > 0 && chip->type == SMBB)
		chip->flags |= CHG_FLAGS_VCP_WA;
	if (chip->type == SMBB)
		chip->flags |= BOOST_FLASH_WA;
	if (chip->type == SMBBP)
		chip->flags |= BOOST_FLASH_WA;
}

static int
qpnp_chg_request_irqs(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct resource *resource;
	struct spmi_resource *spmi_resource;
	u8 subtype;
	struct spmi_device *spmi = chip->spmi;

	spmi_for_each_container_dev(spmi_resource, chip->spmi) {
		if (!spmi_resource) {
				pr_err("qpnp_chg: spmi resource absent\n");
			return rc;
		}

		resource = spmi_get_resource(spmi, spmi_resource,
						IORESOURCE_MEM, 0);
		if (!(resource && resource->start)) {
			pr_err("node %s IO resource absent!\n",
				spmi->dev.of_node->full_name);
			return rc;
		}

		rc = qpnp_chg_read(chip, &subtype,
				resource->start + REG_OFFSET_PERP_SUBTYPE, 1);
		if (rc) {
			pr_err("Peripheral subtype read failed rc=%d\n", rc);
			return rc;
		}

		switch (subtype) {
		case SMBB_CHGR_SUBTYPE:
		case SMBBP_CHGR_SUBTYPE:
		case SMBCL_CHGR_SUBTYPE:
			chip->chg_fastchg.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "fast-chg-on");
			if (chip->chg_fastchg.irq < 0) {
				pr_err("Unable to get fast-chg-on irq\n");
				return rc;
			}

			chip->chg_trklchg.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "trkl-chg-on");
			if (chip->chg_trklchg.irq < 0) {
				pr_err("Unable to get trkl-chg-on irq\n");
				return rc;
			}

			chip->chg_failed.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "chg-failed");
			if (chip->chg_failed.irq < 0) {
				pr_err("Unable to get chg_failed irq\n");
				return rc;
			}

			chip->chg_vbatdet_lo.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "vbat-det-lo");
			if (chip->chg_vbatdet_lo.irq < 0) {
				pr_err("Unable to get fast-chg-on irq\n");
				return rc;
			}

			rc |= devm_request_irq(chip->dev, chip->chg_failed.irq,
				qpnp_chg_chgr_chg_failed_irq_handler,
				IRQF_TRIGGER_RISING, "chg-failed", chip);
			if (rc < 0) {
				pr_err("Can't request %d chg-failed: %d\n",
						chip->chg_failed.irq, rc);
				return rc;
			}

			rc |= devm_request_irq(chip->dev, chip->chg_fastchg.irq,
					qpnp_chg_chgr_chg_fastchg_irq_handler,
					IRQF_TRIGGER_RISING,
					"fast-chg-on", chip);
			if (rc < 0) {
				pr_err("Can't request %d fast-chg-on: %d\n",
						chip->chg_fastchg.irq, rc);
				return rc;
			}

			rc |= devm_request_irq(chip->dev, chip->chg_trklchg.irq,
				qpnp_chg_chgr_chg_trklchg_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"trkl-chg-on", chip);
			if (rc < 0) {
				pr_err("Can't request %d trkl-chg-on: %d\n",
						chip->chg_trklchg.irq, rc);
				return rc;
			}

			rc |= devm_request_irq(chip->dev,
				chip->chg_vbatdet_lo.irq,
				qpnp_chg_vbatdet_lo_irq_handler,
				IRQF_TRIGGER_RISING,
				"vbat-det-lo", chip);
			if (rc < 0) {
				pr_err("Can't request %d vbat-det-lo: %d\n",
						chip->chg_vbatdet_lo.irq, rc);
				return rc;
			}

			enable_irq_wake(chip->chg_fastchg.irq);
			enable_irq_wake(chip->chg_trklchg.irq);
			enable_irq_wake(chip->chg_failed.irq);
			qpnp_chg_disable_irq(&chip->chg_vbatdet_lo);
			if (!chip->somc_params.resume_delta_soc)
				enable_irq_wake(chip->chg_vbatdet_lo.irq);

			break;
		case SMBB_BAT_IF_SUBTYPE:
		case SMBBP_BAT_IF_SUBTYPE:
		case SMBCL_BAT_IF_SUBTYPE:
			chip->batt_pres.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "batt-pres");
			if (chip->batt_pres.irq < 0) {
				pr_err("Unable to get batt-pres irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->batt_pres.irq,
				qpnp_chg_bat_if_batt_pres_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"batt-pres", chip);
			if (rc < 0) {
				pr_err("Can't request %d batt-pres irq: %d\n",
						chip->batt_pres.irq, rc);
				return rc;
			}

			enable_irq_wake(chip->batt_pres.irq);
			break;
		case SMBB_USB_CHGPTH_SUBTYPE:
		case SMBBP_USB_CHGPTH_SUBTYPE:
		case SMBCL_USB_CHGPTH_SUBTYPE:
			chip->somc_params.usb_coarse_det.irq =
				spmi_get_irq_byname(spmi,
					spmi_resource, "coarse-det-usb");
			if (chip->somc_params.usb_coarse_det.irq < 0) {
				pr_err("Unable to get usb coarse det irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev,
				chip->somc_params.usb_coarse_det.irq,
				qpnp_chg_usb_coarse_det_irq_handler,
				IRQF_TRIGGER_RISING,
					"coarse-det-usb", chip);
			if (rc < 0) {
				pr_err("Can't request %d coarse-det-usb: %d\n",
					chip->somc_params.usb_coarse_det.irq,
									rc);
				return rc;
			}

			chip->usbin_valid.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "usbin-valid");
			if (chip->usbin_valid.irq < 0) {
				pr_err("Unable to get usbin irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->usbin_valid.irq,
				qpnp_chg_usb_usbin_valid_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
					"usbin-valid", chip);
			if (rc < 0) {
				pr_err("Can't request %d usbin-valid: %d\n",
						chip->usbin_valid.irq, rc);
				return rc;
			}

			chip->somc_params.chg_gone.irq = spmi_get_irq_byname(
					spmi, spmi_resource, "chg-gone");
			if (chip->somc_params.chg_gone.irq < 0) {
				pr_err("Unable to get chg-gone irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev,
				chip->somc_params.chg_gone.irq,
				qpnp_chg_usb_chg_gone_irq_handler,
				IRQF_TRIGGER_RISING,
					"chg-gone", chip);
			if (rc < 0) {
				pr_err("Can't request %d chg-gone: %d\n",
						chip->somc_params.chg_gone.irq,
						rc);
				return rc;
			}

			enable_irq_wake(chip->usbin_valid.irq);
			enable_irq_wake(chip->somc_params.usb_coarse_det.irq);
			enable_irq_wake(chip->somc_params.chg_gone.irq);
			break;
		case SMBB_DC_CHGPTH_SUBTYPE:
			rc = register_dock_event(chip);
			if (rc < 0)
				return rc;
			chip->dcin_valid.irq = spmi_get_irq_byname(spmi,
					spmi_resource, "dcin-valid");
			if (chip->dcin_valid.irq < 0) {
				pr_err("Unable to get dcin irq\n");
				return -rc;
			}
			rc = devm_request_irq(chip->dev, chip->dcin_valid.irq,
				qpnp_chg_dc_dcin_valid_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"dcin-valid", chip);
			if (rc < 0) {
				pr_err("Can't request %d dcin-valid: %d\n",
						chip->dcin_valid.irq, rc);
				return rc;
			}

			enable_irq_wake(chip->dcin_valid.irq);
			break;
		}
	}

	return rc;
}

#define WDOG_EN_BIT	BIT(7)
static int
qpnp_chg_hwinit(struct qpnp_chg_chip *chip, u8 subtype,
				struct spmi_resource *spmi_resource)
{
	int rc = 0;
	u8 reg = 0;
	struct regulator_init_data *init_data;
	struct regulator_desc *rdesc;

	switch (subtype) {
	case SMBB_CHGR_SUBTYPE:
	case SMBBP_CHGR_SUBTYPE:
	case SMBCL_CHGR_SUBTYPE:
		rc = qpnp_chg_vinmin_set(chip, chip->min_voltage_mv);
		if (rc) {
			pr_debug("failed setting  min_voltage rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_vddmax_set(chip, chip->max_voltage_mv);
		if (rc) {
			pr_debug("failed setting max_voltage rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_vddsafe_set(chip, chip->safe_voltage_mv);
		if (rc) {
			pr_debug("failed setting safe_voltage rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_vbatdet_set(chip,
				chip->max_voltage_mv - chip->resume_delta_mv);
		if (rc) {
			pr_debug("failed setting resume_voltage rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_ibatmax_set(chip, chip->max_bat_chg_current);
		if (rc) {
			pr_debug("failed setting ibatmax rc=%d\n", rc);
			return rc;
		}
		if (chip->term_current) {
			rc = qpnp_chg_ibatterm_set(chip, chip->term_current);
			if (rc) {
				pr_debug("failed setting ibatterm rc=%d\n", rc);
				return rc;
			}
		}
		rc = qpnp_chg_ibatsafe_set(chip, chip->safe_current);
		if (rc) {
			pr_debug("failed setting ibat_Safe rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_tchg_max_set(chip, chip->tchg_mins);
		if (rc) {
			pr_debug("failed setting tchg_mins rc=%d\n", rc);
			return rc;
		}

		/* HACK: Disable wdog */
		rc = qpnp_chg_masked_write(chip, chip->chgr_base + 0x62,
			0xFF, 0xA0, 1);

		/* HACK: use analog EOC */
		rc = qpnp_chg_masked_write(chip, chip->chgr_base +
			CHGR_IBAT_TERM_CHGR,
			0xFF, 0x08, 1);

		rc = qpnp_chg_smbb_frequency_1p6MHz_set(chip);
		if (rc)
			return rc;

		rc = qpnp_chg_masked_write(chip, chip->chgr_base +
			CHGR_VBAT_WEAK,
			0xFF, 0x0A, 1);
		if (rc) {
			pr_debug("failed setting VBAT_WEAK rc=%d\n", rc);
			return rc;
		}

		break;
	case SMBB_BUCK_SUBTYPE:
	case SMBBP_BUCK_SUBTYPE:
	case SMBCL_BUCK_SUBTYPE:
		rc = qpnp_chg_toggle_chg_done_logic(chip, 0);
		if (rc)
			return rc;

		rc = qpnp_chg_masked_write(chip,
			chip->buck_base + CHGR_BUCK_BCK_VBAT_REG_MODE,
			BUCK_VBAT_REG_NODE_SEL_BIT,
			BUCK_VBAT_REG_NODE_SEL_BIT, 1);
		if (rc) {
			pr_debug("failed to enable IR drop comp rc=%d\n", rc);
			return rc;
		}
		break;
	case SMBB_BAT_IF_SUBTYPE:
	case SMBBP_BAT_IF_SUBTYPE:
	case SMBCL_BAT_IF_SUBTYPE:
		/* Select battery presence detection */
		switch (chip->bpd_detection) {
		case BPD_TYPE_BAT_THM:
			reg = BAT_THM_EN;
			break;
		case BPD_TYPE_BAT_ID:
			reg = BAT_ID_EN;
			break;
		case BPD_TYPE_BAT_THM_BAT_ID:
			reg = BAT_THM_EN | BAT_ID_EN;
			break;
		default:
			reg = BAT_THM_EN;
			break;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_BPD_CTRL,
			BAT_IF_BPD_CTRL_SEL,
			reg, 1);
		if (rc) {
			pr_debug("failed to chose BPD rc=%d\n", rc);
			return rc;
		}
		/* Force on VREF_BAT_THM */
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL,
			VREF_BATT_THERM_FORCE_ON,
			VREF_BATT_THERM_FORCE_ON, 1);
		if (rc) {
			pr_debug("failed to force on VREF_BAT_THM rc=%d\n", rc);
			return rc;
		}
		break;
	case SMBB_USB_CHGPTH_SUBTYPE:
	case SMBBP_USB_CHGPTH_SUBTYPE:
	case SMBCL_USB_CHGPTH_SUBTYPE:
		if (qpnp_chg_is_usb_chg_plugged_in(chip)) {
			rc = qpnp_chg_masked_write(chip,
				chip->usb_chgpth_base + CHGR_USB_ENUM_T_STOP,
				ENUM_T_STOP_BIT,
				ENUM_T_STOP_BIT, 1);
			if (rc) {
				pr_err("failed to write enum stop rc=%d\n", rc);
				return -ENXIO;
			}
		}

		init_data = of_get_regulator_init_data(chip->dev,
						       spmi_resource->of_node);
		if (!init_data) {
			pr_err("unable to allocate memory\n");
			return -ENOMEM;
		}

		if (init_data->constraints.name) {
			if (of_get_property(chip->dev->of_node,
						"otg-parent-supply", NULL))
				init_data->supply_regulator = "otg-parent";

			rdesc			= &(chip->otg_vreg.rdesc);
			rdesc->owner		= THIS_MODULE;
			rdesc->type		= REGULATOR_VOLTAGE;
			rdesc->ops		= &qpnp_chg_otg_reg_ops;
			rdesc->name		= init_data->constraints.name;

			init_data->constraints.valid_ops_mask
				|= REGULATOR_CHANGE_STATUS;

			chip->otg_vreg.rdev = regulator_register(rdesc,
					chip->dev, init_data, chip,
					spmi_resource->of_node);
			if (IS_ERR(chip->otg_vreg.rdev)) {
				rc = PTR_ERR(chip->otg_vreg.rdev);
				chip->otg_vreg.rdev = NULL;
				if (rc != -EPROBE_DEFER)
					pr_err("OTG reg failed, rc=%d\n", rc);
				return rc;
			}
		}

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + USB_OVP_CTL,
			USB_VALID_DEB_20MS,
			USB_VALID_DEB_20MS, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_ENUM_T_STOP,
			ENUM_T_STOP_BIT,
			ENUM_T_STOP_BIT, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + USB_CHG_GONE_REV_BST,
			0xFF,
			0x80, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);

		/* bypass debounce timer */
		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + 0xE9,
			0x04,
			0x04, 1);

		break;
	case SMBB_DC_CHGPTH_SUBTYPE:
		break;
	case SMBB_BOOST_SUBTYPE:
	case SMBBP_BOOST_SUBTYPE:
		init_data = of_get_regulator_init_data(chip->dev,
					       spmi_resource->of_node);
		if (!init_data) {
			pr_err("unable to allocate memory\n");
			return -ENOMEM;
		}

		if (init_data->constraints.name) {
			if (of_get_property(chip->dev->of_node,
						"boost-parent-supply", NULL))
				init_data->supply_regulator = "boost-parent";

			rdesc			= &(chip->boost_vreg.rdesc);
			rdesc->owner		= THIS_MODULE;
			rdesc->type		= REGULATOR_VOLTAGE;
			rdesc->ops		= &qpnp_chg_boost_reg_ops;
			rdesc->name		= init_data->constraints.name;

			init_data->constraints.valid_ops_mask
				|= REGULATOR_CHANGE_STATUS
					| REGULATOR_CHANGE_VOLTAGE;

			chip->boost_vreg.rdev = regulator_register(rdesc,
					chip->dev, init_data, chip,
					spmi_resource->of_node);
			if (IS_ERR(chip->boost_vreg.rdev)) {
				rc = PTR_ERR(chip->boost_vreg.rdev);
				chip->boost_vreg.rdev = NULL;
				if (rc != -EPROBE_DEFER)
					pr_err("boost reg failed, rc=%d\n", rc);
				return rc;
			}
		}
		break;
	case SMBB_MISC_SUBTYPE:
	case SMBBP_MISC_SUBTYPE:
	case SMBCL_MISC_SUBTYPE:
		if (subtype == SMBB_MISC_SUBTYPE)
			chip->type = SMBB;
		else if (subtype == SMBBP_MISC_SUBTYPE)
			chip->type = SMBBP;
		else if (subtype == SMBCL_MISC_SUBTYPE)
			chip->type = SMBCL;

		pr_debug("Setting BOOT_DONE\n");
		rc = qpnp_chg_masked_write(chip,
			chip->misc_base + CHGR_MISC_BOOT_DONE,
			CHGR_BOOT_DONE, CHGR_BOOT_DONE, 1);
		rc = qpnp_chg_read(chip, &reg,
				 chip->misc_base + MISC_REVISION2, 1);
		if (rc) {
			pr_err("failed to read revision register rc=%d\n", rc);
			return rc;
		}

		chip->revision = reg;
		rc = qpnp_chg_read(chip, &reg,
				 chip->misc_base + MISC_REVISION3, 1);
		if (rc) {
			pr_err("failed to read decirevision register rc=%d\n",
									 rc);
			return rc;
		}

		chip->somc_params.decirevision = reg;
		break;
	default:
		pr_err("Invalid peripheral subtype\n");
	}
	return rc;
}

#define OF_PROP_READ(chip, prop, qpnp_dt_property, retval, optional)	\
do {									\
	if (retval)							\
		break;							\
									\
	retval = of_property_read_u32(chip->spmi->dev.of_node,		\
					"qcom," qpnp_dt_property,	\
					&chip->prop);			\
									\
	if ((retval == -EINVAL) && optional)				\
		retval = 0;						\
	else if (retval)						\
		pr_err("Error reading " #qpnp_dt_property		\
				" property rc = %d\n", rc);		\
} while (0)

static ssize_t qpnp_chg_param_show(struct device *dev,
				struct device_attribute *attr,
				char *buf);

static struct device_attribute qpnp_chg_attrs[] = {
	__ATTR(bat_fet_status, S_IRUGO, qpnp_chg_param_show, NULL),
	__ATTR(ibat_max, S_IRUGO, qpnp_chg_param_show, NULL),
	__ATTR(vdd_max, S_IRUGO, qpnp_chg_param_show, NULL),
	__ATTR(vbat_det, S_IRUGO, qpnp_chg_param_show, NULL),
	__ATTR(chg_done, S_IRUGO, qpnp_chg_param_show, NULL),
	__ATTR(fast_chg_on, S_IRUGO, qpnp_chg_param_show, NULL),
	__ATTR(vbat_det_low, S_IRUGO, qpnp_chg_param_show, NULL),
	__ATTR(chg_gone, S_IRUGO, qpnp_chg_param_show, NULL),
	__ATTR(usbin_valid, S_IRUGO, qpnp_chg_param_show, NULL),
	__ATTR(dcin_valid, S_IRUGO, qpnp_chg_param_show, NULL),
	__ATTR(iusb_max, S_IRUGO, qpnp_chg_param_show, NULL),
	__ATTR(idc_max, S_IRUGO, qpnp_chg_param_show, NULL),
	__ATTR(revision, S_IRUGO, qpnp_chg_param_show, NULL),
	__ATTR(buck_sts, S_IRUGO, qpnp_chg_param_show, NULL),
	__ATTR(chg_ctrl, S_IRUGO, qpnp_chg_param_show, NULL),
};

static ssize_t qpnp_chg_param_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int rc;
	ssize_t size = 0;
	const ptrdiff_t off = attr - qpnp_chg_attrs;
	struct qpnp_chg_chip *chip = dev_get_drvdata(dev);
	u8 reg;
	int data;

	switch (off) {
	case BAT_FET_STATUS:
		rc = qpnp_chg_read(chip, &reg,
				chip->bat_if_base + CHGR_BAT_IF_BAT_FET_STATUS,
				1);
		if (rc) {
			pr_err("BAT_FET_STATUS read failed: rc=%d\n", rc);
			return rc;
		}
		size = scnprintf(buf, PAGE_SIZE, "%d\n", reg);
		break;
	case IBAT_MAX:
		rc = qpnp_chg_ibatmax_get(chip, &data);
		if (rc) {
			pr_err("IBAT_MAX read failed: rc=%d\n", rc);
			return rc;
		}
		size = scnprintf(buf, PAGE_SIZE, "%d\n", data);
		break;
	case VDD_MAX:
		rc = qpnp_chg_vddmax_get(chip, &data);
		if (rc) {
			pr_err("VDD_MAX read failed: rc=%d\n", rc);
			return rc;
		}
		size = scnprintf(buf, PAGE_SIZE, "%d\n", data);
		break;
	case VBAT_DET:
		rc = qpnp_chg_vbatdet_get(chip, &data);
		if (rc) {
			pr_err("VBAT_DET read failed: rc=%d\n", rc);
			return rc;
		}
		size = scnprintf(buf, PAGE_SIZE, "%d\n", data);
		break;
	case CHG_DONE:
		rc = qpnp_chg_read(chip, &reg,
				INT_RT_STS(chip->chgr_base), 1);
		if (rc) {
			pr_err("failed to read chg_done irq sts %d\n", rc);
			return rc;
		}
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
					!!(reg & CHG_DONE_IRQ));
		break;
	case FAST_CHG_ON:
		rc = qpnp_chg_read(chip, &reg,
				INT_RT_STS(chip->chgr_base), 1);
		if (rc) {
			pr_err("failed to read fast_chg_on irq sts %d\n", rc);
			return rc;
		}
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
					!!(reg & FAST_CHG_ON_IRQ));
		break;
	case VBAT_DET_LOW:
		rc = qpnp_chg_read(chip, &reg,
				INT_RT_STS(chip->chgr_base), 1);
		if (rc) {
			pr_err("failed to read vbat_det_low irq sts %d\n", rc);
			return rc;
		}
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
					!!(reg & VBAT_DET_LOW_IRQ));
		break;
	case CHG_GONE:
		rc = qpnp_chg_read(chip, &reg,
				INT_RT_STS(chip->usb_chgpth_base), 1);
		if (rc) {
			pr_err("failed to read chg_gone irq sts %d\n", rc);
			return rc;
		}
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
					!!(reg & CHG_GONE_IRQ));
		break;
	case USBIN_VALID:
		rc = qpnp_chg_read(chip, &reg,
				INT_RT_STS(chip->usb_chgpth_base), 1);
		if (rc) {
			pr_err("failed to read usbin_valid irq sts %d\n", rc);
			return rc;
		}
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
					!!(reg & USBIN_VALID_IRQ));
		break;
	case DCIN_VALID:
		rc = qpnp_chg_read(chip, &reg,
				INT_RT_STS(chip->dc_chgpth_base), 1);
		if (rc) {
			pr_err("failed to read dcin_valid irq sts %d\n", rc);
			return rc;
		}
		size = scnprintf(buf, PAGE_SIZE, "%d\n",
					!!(reg & DCIN_VALID_IRQ));
		break;
	case IUSB_MAX:
		rc = qpnp_chg_iusbmax_get(chip, &data);
		if (rc) {
			pr_err("IUSB_MAX read failed: rc=%d\n", rc);
			return rc;
		}
		size = scnprintf(buf, PAGE_SIZE, "%d\n", data);
		break;
	case IDC_MAX:
		rc = qpnp_chg_idcmax_get(chip, &data);
		if (rc) {
			pr_err("IDC_MAX read failed: rc=%d\n", rc);
			return rc;
		}
		size = scnprintf(buf, PAGE_SIZE, "%d\n", data);
		break;
	case REVISION:
		size = scnprintf(buf, PAGE_SIZE, "%d.%d\n",
			chip->revision, chip->somc_params.decirevision);
		break;
	case BUCK_STS:
		rc = qpnp_chg_read(chip, &reg, INT_RT_STS(chip->buck_base), 1);
		if (rc) {
			pr_err("failed to read buck rc=%d\n", rc);
			return rc;
		}
		size = scnprintf(buf, PAGE_SIZE, "%d\n", reg);
		break;
	case CHG_CTRL:
		rc = qpnp_chg_read(chip, &reg,
				chip->chgr_base + CHGR_CHG_CTRL, 1);
		if (rc) {
			pr_err("CHGR_CHG_CTRL read failed: rc=%d\n", rc);
			return rc;
		}
		size = scnprintf(buf, PAGE_SIZE, "%d\n", reg);
		break;
	default:
		size = 0;
		break;
	}

	return size;
}

static int create_sysfs_entries(struct qpnp_chg_chip *chip)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(qpnp_chg_attrs); i++) {
		rc = device_create_file(chip->dev, &qpnp_chg_attrs[i]);
		if (rc < 0)
			goto revert;
	}

	return 0;

revert:
	for (; i >= 0; i--)
		device_remove_file(chip->dev, &qpnp_chg_attrs[i]);

	return rc;
}

static void remove_sysfs_entries(struct qpnp_chg_chip *chip)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qpnp_chg_attrs); i++)
		device_remove_file(chip->dev, &qpnp_chg_attrs[i]);
}

static int
qpnp_charger_read_dt_props(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	const char *bpd;

	OF_PROP_READ(chip, max_voltage_mv, "vddmax-mv", rc, 0);
	OF_PROP_READ(chip, min_voltage_mv, "vinmin-mv", rc, 0);
	OF_PROP_READ(chip, safe_voltage_mv, "vddsafe-mv", rc, 0);
	OF_PROP_READ(chip, resume_delta_mv, "vbatdet-delta-mv", rc, 0);
	OF_PROP_READ(chip, somc_params.resume_delta_soc, "vbatdet-delta-soc",
			rc, 0);
	OF_PROP_READ(chip, safe_current, "ibatsafe-ma", rc, 0);
	OF_PROP_READ(chip, max_bat_chg_current, "ibatmax-ma", rc, 0);
	if (rc)
		pr_err("failed to read required dt parameters %d\n", rc);

	OF_PROP_READ(chip, term_current, "ibatterm-ma", rc, 1);
	OF_PROP_READ(chip, maxinput_dc_ma, "maxinput-dc-ma", rc, 1);
	OF_PROP_READ(chip, maxinput_usb_ma, "maxinput-usb-ma", rc, 1);
	OF_PROP_READ(chip, warm_bat_decidegc, "warm-bat-decidegc", rc, 1);
	OF_PROP_READ(chip, cool_bat_decidegc, "cool-bat-decidegc", rc, 1);
	OF_PROP_READ(chip, tchg_mins, "tchg-mins", rc, 1);
	if (rc)
		return rc;
	/* Get the enable-arb-check-work property */
	chip->somc_params.enable_arb_check_work = of_property_read_bool(
			chip->spmi->dev.of_node,
			"qcom,enable-arb-check-work");

	/* Get the enable-done-fastchg-work property */
	chip->somc_params.enable_chg_done_fastchg_work = of_property_read_bool(
			chip->spmi->dev.of_node,
			"qcom,enable-done-fastchg-work");

	/* Get the warm-disable-charging property */
	chip->somc_params.warm_disable_charging = of_property_read_bool(
			chip->spmi->dev.of_node,
			"qcom,warm-disable-charging");

	rc = of_property_read_string(chip->spmi->dev.of_node,
		"qcom,bpd-detection", &bpd);
	if (rc) {
		/* Select BAT_THM as default BPD scheme */
		chip->bpd_detection = BPD_TYPE_BAT_THM;
	} else {
		chip->bpd_detection = get_bpd(bpd);
		if (chip->bpd_detection < 0) {
			pr_err("failed to determine bpd schema %d\n", rc);
			return rc;
		}
	}

	/* Look up JEITA compliance parameters if cool and warm temp provided */
	if (chip->cool_bat_decidegc && chip->warm_bat_decidegc) {
		rc = qpnp_adc_tm_is_ready();
		if (rc) {
			pr_err("tm not ready %d\n", rc);
			return rc;
		}

		OF_PROP_READ(chip, warm_bat_chg_ma, "ibatmax-warm-ma", rc, 1);
		OF_PROP_READ(chip, cool_bat_chg_ma, "ibatmax-cool-ma", rc, 1);
		OF_PROP_READ(chip, warm_bat_mv, "warm-bat-mv", rc, 1);
		OF_PROP_READ(chip, cool_bat_mv, "cool-bat-mv", rc, 1);
		if (rc)
			return rc;
	}

	/* Get the charging-disabled property */
	chip->charging_disabled = of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,charging-disabled");

	/* Get the duty-cycle-100p property */
	chip->duty_cycle_100p = of_property_read_bool(
					chip->spmi->dev.of_node,
					"qcom,duty-cycle-100p");
	if (chip->duty_cycle_100p) {
		rc = qpnp_buck_set_100_duty_cycle_enable(chip, 1);
		if (rc) {
			pr_err("failed to enable duty cycle %d\n", rc);
			return rc;
		}
	}

	/* Get the fake-batt-values property */
	chip->use_default_batt_values =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,use-default-batt-values");

	/* Disable charging when faking battery values */
	if (chip->use_default_batt_values)
		chip->charging_disabled = true;

	of_get_property(chip->spmi->dev.of_node, "qcom,thermal-mitigation",
		&(chip->thermal_levels));

	if (chip->thermal_levels > sizeof(int)) {
		chip->thermal_mitigation = kzalloc(
			chip->thermal_levels,
			GFP_KERNEL);

		if (chip->thermal_mitigation == NULL) {
			pr_err("thermal mitigation kzalloc() failed.\n");
			return rc;
		}

		chip->thermal_levels /= sizeof(int);
		rc = of_property_read_u32_array(chip->spmi->dev.of_node,
				"qcom,thermal-mitigation",
				chip->thermal_mitigation, chip->thermal_levels);
		if (rc) {
			pr_err("qcom,thermal-mitigation missing in dt\n");
			return rc;
		}
	}

	return rc;
}

static int __devinit
qpnp_charger_probe(struct spmi_device *spmi)
{
	u8 subtype;
	struct qpnp_chg_chip	*chip;
	struct resource *resource;
	struct spmi_resource *spmi_resource;
	int rc = 0;

	chip = kzalloc(sizeof *chip, GFP_KERNEL);
	if (chip == NULL) {
		pr_err("kzalloc() failed.\n");
		return -ENOMEM;
	}

	chip->dev = &(spmi->dev);
	chip->spmi = spmi;

	chip->usb_psy = power_supply_get_by_name("usb");
	if (!chip->usb_psy) {
		pr_err("usb supply not found deferring probe\n");
		rc = -EPROBE_DEFER;
		goto fail_chg_enable;
	}

	/* Get all device tree properties */
	rc = qpnp_charger_read_dt_props(chip);
	if (rc)
		goto fail_chg_enable;

	spmi_for_each_container_dev(spmi_resource, spmi) {
		if (!spmi_resource) {
			pr_err("qpnp_chg: spmi resource absent\n");
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		resource = spmi_get_resource(spmi, spmi_resource,
						IORESOURCE_MEM, 0);
		if (!(resource && resource->start)) {
			pr_err("node %s IO resource absent!\n",
				spmi->dev.of_node->full_name);
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		rc = qpnp_chg_read(chip, &subtype,
				resource->start + REG_OFFSET_PERP_SUBTYPE, 1);
		if (rc) {
			pr_err("Peripheral subtype read failed rc=%d\n", rc);
			goto fail_chg_enable;
		}

		switch (subtype) {
		case SMBB_CHGR_SUBTYPE:
		case SMBBP_CHGR_SUBTYPE:
		case SMBCL_CHGR_SUBTYPE:
			chip->chgr_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_BUCK_SUBTYPE:
		case SMBBP_BUCK_SUBTYPE:
		case SMBCL_BUCK_SUBTYPE:
			chip->buck_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}

			rc = qpnp_chg_masked_write(chip,
				chip->buck_base + SEC_ACCESS,
				0xFF,
				0xA5, 1);

			rc = qpnp_chg_masked_write(chip,
				chip->buck_base + BUCK_VCHG_OV,
				0xff,
				0x00, 1);

			rc = qpnp_chg_masked_write(chip,
				chip->buck_base + SEC_ACCESS,
				0xFF,
				0xA5, 1);

			rc = qpnp_chg_masked_write(chip,
				chip->buck_base + BUCK_TEST_SMBC_MODES,
				0xFF,
				0x80, 1);

			break;
		case SMBB_BAT_IF_SUBTYPE:
		case SMBBP_BAT_IF_SUBTYPE:
		case SMBCL_BAT_IF_SUBTYPE:
			chip->bat_if_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_USB_CHGPTH_SUBTYPE:
		case SMBBP_USB_CHGPTH_SUBTYPE:
		case SMBCL_USB_CHGPTH_SUBTYPE:
			chip->usb_chgpth_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				if (rc != -EPROBE_DEFER)
					pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_DC_CHGPTH_SUBTYPE:
			chip->dc_chgpth_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_BOOST_SUBTYPE:
		case SMBBP_BOOST_SUBTYPE:
			chip->boost_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				if (rc != -EPROBE_DEFER)
					pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_MISC_SUBTYPE:
		case SMBBP_MISC_SUBTYPE:
		case SMBCL_MISC_SUBTYPE:
			chip->misc_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype=0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		default:
			pr_err("Invalid peripheral subtype=0x%x\n", subtype);
			rc = -EINVAL;
			goto fail_chg_enable;
		}
	}
	dev_set_drvdata(&spmi->dev, chip);
	device_init_wakeup(&spmi->dev, 1);

	INIT_DELAYED_WORK(&chip->somc_params.aicl_work,
					qpnp_chg_aicl_work);

	if (chip->bat_if_base) {
		rc = qpnp_vadc_is_ready();
		if (rc)
			goto fail_chg_enable;

		chip->batt_psy.name = "battery";
		chip->batt_psy.type = POWER_SUPPLY_TYPE_BATTERY;
		chip->batt_psy.properties = msm_batt_power_props;
		chip->batt_psy.num_properties =
			ARRAY_SIZE(msm_batt_power_props);
		chip->batt_psy.get_property = qpnp_batt_power_get_property;
		chip->batt_psy.set_property = qpnp_batt_power_set_property;
		chip->batt_psy.property_is_writeable =
				qpnp_batt_property_is_writeable;
		chip->batt_psy.external_power_changed =
				qpnp_batt_external_power_changed;
		chip->batt_psy.supplied_to = pm_batt_supplied_to;
		chip->batt_psy.num_supplicants =
				ARRAY_SIZE(pm_batt_supplied_to);

		rc = power_supply_register(chip->dev, &chip->batt_psy);
		if (rc < 0) {
			pr_err("batt failed to register rc = %d\n", rc);
			goto fail_chg_enable;
		}
		INIT_WORK(&chip->adc_measure_work,
			qpnp_bat_if_adc_measure_work);
	}

	wake_lock_init(&chip->eoc_wake_lock,
		WAKE_LOCK_SUSPEND, "qpnp-chg-eoc-lock");
	INIT_DELAYED_WORK(&chip->eoc_work, qpnp_eoc_work);
	INIT_DELAYED_WORK(&chip->arb_stop_work, qpnp_arb_stop_work);
	INIT_WORK(&chip->somc_params.ovp_check_work, qpnp_ovp_check_work);
	INIT_DELAYED_WORK(&chip->somc_params.health_check_work,
			qpnp_health_check_work);
	INIT_WORK(&chip->somc_params.smbb_clk_work,
			qpnp_smbb_sw_controlled_clk_work);
	INIT_DELAYED_WORK(&chip->somc_params.smbb_hw_clk_work,
			qpnp_smbb_hw_clk_work);
	INIT_WORK(&chip->somc_params.aicl_set_work, qpnp_chg_aicl_set_work);

	if (chip->dc_chgpth_base) {
		chip->dc_psy.name = "qpnp-dc";
		chip->dc_psy.type = POWER_SUPPLY_TYPE_MAINS;
		chip->dc_psy.supplied_to = pm_power_supplied_to;
		chip->dc_psy.num_supplicants = ARRAY_SIZE(pm_power_supplied_to);
		chip->dc_psy.properties = pm_power_props_mains;
		chip->dc_psy.num_properties = ARRAY_SIZE(pm_power_props_mains);
		chip->dc_psy.get_property = qpnp_power_get_property_mains;
		chip->dc_psy.set_property = qpnp_power_set_property_mains;
		chip->dc_psy.property_is_writeable =
				qpnp_dc_property_is_writeable;

		rc = power_supply_register(chip->dev, &chip->dc_psy);
		if (rc < 0) {
			pr_err("power_supply_register dc failed rc=%d\n", rc);
			goto unregister_batt;
		}
	}

	/* Turn on appropriate workaround flags */
	qpnp_chg_setup_flags(chip);

	if (chip->maxinput_dc_ma && chip->dc_chgpth_base)
		qpnp_chg_aicl_idc_set(chip, chip->maxinput_dc_ma);

	if (chip->cool_bat_decidegc && chip->warm_bat_decidegc) {
		chip->adc_param.low_temp = chip->cool_bat_decidegc;
		chip->adc_param.high_temp = chip->warm_bat_decidegc;
		chip->adc_param.timer_interval = ADC_MEAS2_INTERVAL_1S;
		chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
		chip->adc_param.btm_ctx = chip;
		chip->adc_param.threshold_notification =
						qpnp_chg_adc_notification;
		chip->adc_param.channel = LR_MUX1_BATT_THERM;

		if (get_prop_batt_present(chip)) {
			rc = qpnp_adc_tm_channel_measure(&chip->adc_param);
			if (rc) {
				pr_err("request ADC error %d\n", rc);
				goto fail_chg_enable;
			}
		}
	}
	wake_lock_init(&chip->somc_params.unplug_wake_lock,
			WAKE_LOCK_SUSPEND, "qpnp_unplug_charger");
	wake_lock_init(&chip->somc_params.chg_gone_wake_lock,
			WAKE_LOCK_SUSPEND, "qpnp_chg_gone");

	qpnp_chg_charge_en(chip, !chip->charging_disabled);
	qpnp_chg_force_run_on_batt(chip, chip->charging_disabled);
	qpnp_chg_set_appropriate_vddmax(chip);

	rc = qpnp_chg_request_irqs(chip);
	if (rc) {
		pr_err("failed to request interrupts %d\n", rc);
		goto unregister_batt;
	}

	qpnp_chg_usb_usbin_valid_irq_handler(USBIN_VALID_IRQ, chip);
	power_supply_set_present(chip->usb_psy,
			qpnp_chg_is_usb_chg_plugged_in(chip));

	if (chip->somc_params.enable_arb_check_work)
		INIT_DELAYED_WORK(&chip->somc_params.arb_check_work,
						qpnp_chg_arb_check_worker);

	qpnp_chg_dc_dcin_valid_irq_handler(DCIN_VALID_IRQ, chip);

	if (chip->somc_params.enable_chg_done_fastchg_work)
		qpnp_poll_chg_done_fastchg_irqs(chip);

	/* register input device */
	chip->somc_params.chg_unplug_key = input_allocate_device();
	if (!chip->somc_params.chg_unplug_key) {
		pr_err("can't allocate unplug virtual button\n");
		rc = -ENOMEM;
		goto unregister_batt;
	}

	input_set_capability(chip->somc_params.chg_unplug_key,
				EV_KEY, KEY_F24);
	chip->somc_params.chg_unplug_key->name = "qpnp_chg_unplug_key";
	chip->somc_params.chg_unplug_key->dev.parent = &(spmi->dev);

	rc = input_register_device(chip->somc_params.chg_unplug_key);
	if (rc) {
		pr_err("can't register power key: %d\n", rc);
		goto free_input_dev;
	}

	if (qpnp_chg_is_usb_chg_plugged_in(chip) ||
	    qpnp_chg_is_dc_chg_plugged_in(chip))
		notify_input_chg_unplug(chip, false);

	rc = create_sysfs_entries(chip);
	if (rc < 0)
		pr_err("sysfs create failed rc = %d\n", rc);

	pr_info("success chg_dis = %d, bpd = %d, usb = %d, dc = %d b_health = %d batt_present = %d\n",
			chip->charging_disabled,
			chip->bpd_detection,
			qpnp_chg_is_usb_chg_plugged_in(chip),
			qpnp_chg_is_dc_chg_plugged_in(chip),
			get_prop_batt_present(chip),
			get_prop_batt_health(chip));
	return 0;

free_input_dev:
	input_free_device(chip->somc_params.chg_unplug_key);
unregister_batt:
	if (chip->bat_if_base)
		power_supply_unregister(&chip->batt_psy);
fail_chg_enable:
	regulator_unregister(chip->otg_vreg.rdev);
	regulator_unregister(chip->boost_vreg.rdev);
	kfree(chip->thermal_mitigation);
	kfree(chip);
	dev_set_drvdata(&spmi->dev, NULL);
	return rc;
}

static int __devexit
qpnp_charger_remove(struct spmi_device *spmi)
{
	struct qpnp_chg_chip *chip = dev_get_drvdata(&spmi->dev);
	remove_sysfs_entries(chip);
	if (chip->cool_bat_decidegc && chip->warm_bat_decidegc
						&& chip->batt_present) {
		qpnp_adc_tm_disable_chan_meas(&chip->adc_param);
	}
	cancel_work_sync(&chip->adc_measure_work);
	cancel_delayed_work_sync(&chip->eoc_work);
	input_unregister_device(chip->somc_params.chg_unplug_key);

	regulator_unregister(chip->otg_vreg.rdev);
	regulator_unregister(chip->boost_vreg.rdev);

	switch_dev_unregister(&chip->somc_params.swdev);

	dev_set_drvdata(&spmi->dev, NULL);
	kfree(chip);

	return 0;
}

static int qpnp_chg_resume(struct device *dev)
{
	struct qpnp_chg_chip *chip = dev_get_drvdata(dev);
	int rc = 0;

	if (chip->bat_if_base) {
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL,
			VREF_BATT_THERM_FORCE_ON,
			VREF_BATT_THERM_FORCE_ON, 1);
		if (rc)
			pr_debug("failed to force on VREF_BAT_THM rc=%d\n", rc);
	}

	return rc;
}

static int qpnp_chg_suspend(struct device *dev)
{
	struct qpnp_chg_chip *chip = dev_get_drvdata(dev);
	int rc = 0;

	if (wake_lock_active(&chip->eoc_wake_lock) ||
		wake_lock_active(&chip->somc_params.unplug_wake_lock) ||
		wake_lock_active(&chip->somc_params.chg_gone_wake_lock)) {
		pr_debug("Abort PM suspend!! active eoc_wake_lock\n");
		return -EBUSY;
	}

	flush_work(&chip->somc_params.dock_work);

	if (chip->bat_if_base) {
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL,
			VREF_BATT_THERM_FORCE_ON,
			VREF_BAT_THM_ENABLED_FSM, 1);
		if (rc)
			pr_debug("failed to set FSM VREF_BAT_THM rc=%d\n", rc);
	}

	return rc;
}

static const struct dev_pm_ops qpnp_chg_pm_ops = {
	.resume		= qpnp_chg_resume,
	.suspend	= qpnp_chg_suspend,
};

static struct spmi_driver qpnp_charger_driver = {
	.probe		= qpnp_charger_probe,
	.remove		= __devexit_p(qpnp_charger_remove),
	.driver		= {
		.name		= QPNP_CHARGER_DEV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= qpnp_charger_match_table,
		.pm		= &qpnp_chg_pm_ops,
	},
};

/**
 * qpnp_chg_init() - register spmi driver for qpnp-chg
 */
int __init
qpnp_chg_init(void)
{
	return spmi_driver_register(&qpnp_charger_driver);
}
module_init(qpnp_chg_init);

static void __exit
qpnp_chg_exit(void)
{
	spmi_driver_unregister(&qpnp_charger_driver);
}
module_exit(qpnp_chg_exit);


MODULE_DESCRIPTION("QPNP charger driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" QPNP_CHARGER_DEV_NAME);
