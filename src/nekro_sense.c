// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Acer WMI Laptop Extras
 *
 *  Copyright (C) 2007-2009	Carlos Corbacho <carlos@strangeworlds.co.uk>
 *
 *  Based on acer_acpi:
 *    Copyright (C) 2005-2007	E.M. Smith
 *    Copyright (C) 2007-2008	Carlos Corbacho <cathectic@gmail.com>
 */

 #define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
 
 #include <linux/delay.h>
 #include <linux/kernel.h>
 #include <linux/module.h>
 #include <linux/init.h>
 #include <linux/types.h>
 #include <linux/dmi.h>
 #include <linux/backlight.h>
 #include <linux/leds.h>
 #include <linux/platform_device.h>
 #include <linux/platform_profile.h>
 #include <linux/acpi.h>
 #include <linux/i8042.h>
 #include <linux/rfkill.h>
 #include <linux/workqueue.h>
 #include <linux/debugfs.h>
 #include <linux/slab.h>
 #include <linux/input.h>
 #include <linux/input/sparse-keymap.h>
 #include <acpi/video.h>
 #include <linux/hwmon.h>
 #include <linux/fs.h>
 #include <linux/units.h>
 #include <linux/unaligned.h>
 #include <linux/bitfield.h>
 #include <linux/bitmap.h>
 
 MODULE_AUTHOR("Carlos Corbacho");
 MODULE_DESCRIPTION("Acer Laptop WMI Extras Driver");
 MODULE_LICENSE("GPL");
/*
 * Method IDs for WMID interface
 */
#define ACER_WMID_GET_WIRELESS_METHODID		1
#define ACER_WMID_GET_BLUETOOTH_METHODID	2
#define ACER_WMID_GET_BRIGHTNESS_METHODID	3
#define ACER_WMID_SET_WIRELESS_METHODID		4
#define ACER_WMID_SET_BLUETOOTH_METHODID	5
#define ACER_WMID_SET_BRIGHTNESS_METHODID	6
#define ACER_WMID_GET_THREEG_METHODID		10
#define ACER_WMID_SET_THREEG_METHODID		11
#define ACER_WMID_SET_FUNCTION 1
#define ACER_WMID_GET_FUNCTION 2

#define ACER_WMID_GET_GAMING_PROFILE_METHODID 3
#define ACER_WMID_SET_GAMING_PROFILE_METHODID 1
#define ACER_WMID_SET_GAMING_LED_METHODID 2
#define ACER_WMID_GET_GAMING_LED_METHODID 4
#define ACER_WMID_GET_GAMING_SYS_INFO_METHODID 5
#define ACER_WMID_SET_GAMING_FAN_BEHAVIOR_METHODID 14
#define ACER_WMID_SET_GAMING_FAN_SPEED_METHODID 16
#define ACER_WMID_SET_GAMING_MISC_SETTING_METHODID 22
#define ACER_WMID_GET_GAMING_MISC_SETTING_METHODID 23
#define ACER_WMID_GET_BATTERY_HEALTH_CONTROL_STATUS_METHODID 20
#define ACER_WMID_SET_BATTERY_HEALTH_CONTROL_METHODID 21
#define ACER_WMID_GET_GAMING_KB_BACKLIGHT_METHODID 21
#define ACER_WMID_SET_GAMING_KB_BACKLIGHT_METHODID 20
#define ACER_WMID_SET_GAMING_RGB_KB_METHODID 6
#define ACER_WMID_GET_GAMING_RGB_KB_METHODID 7

#define ACER_PREDATOR_V4_FAN_SPEED_READ_BIT_MASK GENMASK(20, 8)
#define ACER_GAMING_MISC_SETTING_STATUS_MASK GENMASK_ULL(7, 0)
#define ACER_GAMING_MISC_SETTING_INDEX_MASK GENMASK_ULL(7, 0)
#define ACER_GAMING_MISC_SETTING_VALUE_MASK GENMASK_ULL(15, 8)

#define ACER_PREDATOR_V4_RETURN_STATUS_BIT_MASK GENMASK_ULL(7, 0)
#define ACER_PREDATOR_V4_SENSOR_INDEX_BIT_MASK GENMASK_ULL(15, 8)
#define ACER_PREDATOR_V4_SENSOR_READING_BIT_MASK GENMASK_ULL(23, 8)
#define ACER_PREDATOR_V4_SUPPORTED_SENSORS_BIT_MASK GENMASK_ULL(39, 24)

/* Acer ACPI method GUIDs */
#define WMID_GUID3		"61EF69EA-865C-4BC3-A502-A0DEBA0CB531"
#define WMID_GUID4		"7A4DDFE7-5B5D-40B4-8595-4408E0CC7F56"
#define WMID_GUID5		"79772EC5-04B1-4bfd-843C-61E7F77B6CC9"

/* Predator State */
#define STATE_FILE "/etc/predator_state"
#define KB_STATE_FILE "/etc/four_zone_kb_state"

/* Acer ACPI event GUIDs */
#define ACERWMID_EVENT_GUID "676AA15E-6A47-4D9F-A2CC-1E6D18D14026"

MODULE_ALIAS("wmi:6AF4F258-B401-42FD-BE91-3D4AC2D7C0D3");
MODULE_ALIAS("wmi:676AA15E-6A47-4D9F-A2CC-1E6D18D14026");

enum acer_wmi_event_ids {
    WMID_HOTKEY_EVENT = 0x1,
    WMID_ACCEL_OR_KBD_DOCK_EVENT = 0x5,
    WMID_GAMING_TURBO_KEY_EVENT = 0x7,
    WMID_AC_EVENT = 0x8,
    WMID_BATTERY_BOOST_EVENT = 0x9,
    WMID_CALIBRATION_EVENT = 0x0B,
};

enum battery_mode {
    HEALTH_MODE = 1,
    CALIBRATION_MODE = 2
};

enum acer_wmi_predator_v4_sys_info_command {
   ACER_WMID_CMD_GET_PREDATOR_V4_SUPPORTED_SENSORS = 0x0000,
   ACER_WMID_CMD_GET_PREDATOR_V4_BAT_STATUS = 0x02,
   ACER_WMID_CMD_GET_PREDATOR_V4_SENSOR_READING	= 0x0001,
   ACER_WMID_CMD_GET_PREDATOR_V4_CPU_FAN_SPEED = 0x0201,
   ACER_WMID_CMD_GET_PREDATOR_V4_GPU_FAN_SPEED = 0x0601,
};

enum acer_wmi_predator_v4_sensor_id {
   ACER_WMID_SENSOR_CPU_TEMPERATURE	= 0x01,
   ACER_WMID_SENSOR_CPU_FAN_SPEED		= 0x02,
   ACER_WMID_SENSOR_EXTERNAL_TEMPERATURE_2 = 0x03,
   ACER_WMID_SENSOR_GPU_FAN_SPEED		= 0x06,
   ACER_WMID_SENSOR_GPU_TEMPERATURE	= 0x0A,
};

enum acer_wmi_predator_v4_oc {
    ACER_WMID_OC_NORMAL			= 0x0000,
    ACER_WMID_OC_TURBO			= 0x0002,
};

enum acer_wmi_gaming_misc_setting {
	ACER_WMID_MISC_SETTING_OC_1			= 0x0005,
	ACER_WMID_MISC_SETTING_OC_2			= 0x0007,
	ACER_WMID_MISC_SETTING_SUPPORTED_PROFILES	= 0x000A,
	ACER_WMID_MISC_SETTING_PLATFORM_PROFILE		= 0x000B,
};

struct event_return_value {
    u8 function;
    u8 key_num;
    u16 device_state;
    u16 reserved1;
    u8 kbd_dock_state;
    u8 reserved2;
} __packed;

/* Interface capability flags */
#define ACER_CAP_MAILLED		BIT(0)
#define ACER_CAP_WIRELESS		BIT(1)
#define ACER_CAP_BLUETOOTH		BIT(2)
#define ACER_CAP_BRIGHTNESS		BIT(3)
#define ACER_CAP_THREEG			BIT(4)
#define ACER_CAP_SET_FUNCTION_MODE	BIT(5)
#define ACER_CAP_KBD_DOCK		BIT(6)
#define ACER_CAP_TURBO_OC		BIT(7)
#define ACER_CAP_TURBO_LED		BIT(8)
#define ACER_CAP_TURBO_FAN		BIT(9)
#define ACER_CAP_PLATFORM_PROFILE	BIT(10)
#define ACER_CAP_FAN_SPEED_READ		BIT(11)
#define ACER_CAP_PREDATOR_SENSE		BIT(12)
#define ACER_CAP_NITRO_SENSE BIT(13)
#define ACER_CAP_NITRO_SENSE_V4		BIT(14)
/* PHN16-72 back logo/lightbar LED support */
#define ACER_CAP_BACK_LOGO		BIT(15)

/* Interface type flags */
enum interface_flags {
    ACER_AMW0,
    ACER_AMW0_V2,
    ACER_WMID,
    ACER_WMID_v2,
};

static bool cycle_gaming_thermal_profile = true;
static u64 supported_sensors;

struct acer_data {
    int mailled;
    int threeg;
    int brightness;
};

struct acer_debug {
    struct dentry *root;
    u32 wmid_devices;
};

/* Each low-level interface must define at least some of the following */
struct wmi_interface {
    /* The WMI device type */
    u32 type;

    /* The capabilities this interface provides */
    u32 capability;

    /* Private data for the current interface */
    struct acer_data data;

    /* debugfs entries associated with this interface */
    struct acer_debug debug;
};

/* The static interface pointer, points to the currently detected interface */
static struct wmi_interface *interface;

struct quirk_entry {
    u8 wireless;
    u8 mailled;
    s8 brightness;
    u8 bluetooth;
    u8 turbo;
    u8 cpu_fans;
    u8 gpu_fans;
    u8 predator_v4;
    u8 nitro_v4;
    u8 nitro_sense;
    u8 four_zone_kb;
    u8 back_logo; /* back lid logo/lightbar present */
};

static struct quirk_entry *quirks;

static void __init set_quirks(void)
{
    if (quirks->mailled)
        interface->capability |= ACER_CAP_MAILLED;

    if (quirks->brightness)
        interface->capability |= ACER_CAP_BRIGHTNESS;

    if (quirks->turbo)
        interface->capability |= ACER_CAP_TURBO_OC | ACER_CAP_TURBO_LED
                     | ACER_CAP_TURBO_FAN;

    /* Some acer nitro laptops don't have features like lcd override , boot animation sound so this is used. */
    if (quirks->nitro_sense)
        interface->capability |= ACER_CAP_PLATFORM_PROFILE | ACER_CAP_FAN_SPEED_READ | ACER_CAP_NITRO_SENSE;

    if (quirks->predator_v4)
        interface->capability |= ACER_CAP_PLATFORM_PROFILE |
                     ACER_CAP_FAN_SPEED_READ | ACER_CAP_PREDATOR_SENSE;

    /* Includes all feature that predatorv4 have */
    if (quirks->nitro_v4)
        interface->capability |= ACER_CAP_PLATFORM_PROFILE |
                ACER_CAP_FAN_SPEED_READ  | ACER_CAP_NITRO_SENSE_V4;

    if (quirks->back_logo)
        interface->capability |= ACER_CAP_BACK_LOGO;
}

static struct quirk_entry quirk_acer_predator_phn16_72 = {
    .predator_v4 = 1,
    .four_zone_kb = 1,
    .back_logo = 1,
};

/* Find which quirks are needed for a particular vendor/ model pair */
static void __init find_quirks(void)
{
    quirks = &quirk_acer_predator_phn16_72;
}

static bool has_cap(u32 cap)
{
    return interface->capability & cap;
}

static struct device *platform_profile_device;
static bool platform_profile_support;

/*
 * The profile used before turbo mode. This variable is needed for
 * returning from turbo mode when the mode key is in toggle mode.
 */
static int last_non_turbo_profile = INT_MIN;

/* The most performant supported profile */
static int acer_predator_v4_max_perf;

enum acer_predator_v4_thermal_profile {
   ACER_PREDATOR_V4_THERMAL_PROFILE_QUIET		= 0x00,
   ACER_PREDATOR_V4_THERMAL_PROFILE_BALANCED	= 0x01,
   ACER_PREDATOR_V4_THERMAL_PROFILE_PERFORMANCE	= 0x04,
   ACER_PREDATOR_V4_THERMAL_PROFILE_TURBO		= 0x05,
   ACER_PREDATOR_V4_THERMAL_PROFILE_ECO		= 0x06,
};
 static struct wmi_interface wmid_v2_interface = {
     .type = ACER_WMID_v2,
 };
 
 /*
  * WMID ApgeAction interface
  */
 
 static acpi_status
 WMI_apgeaction_execute_u64(u32 method_id, u64 in, u64 *out){
     struct acpi_buffer input = { (acpi_size) sizeof(u64), (void *)(&in) };
     struct acpi_buffer result = { ACPI_ALLOCATE_BUFFER, NULL };
     union acpi_object *obj;
     u64 tmp = 0;
     acpi_status status;
     status = wmi_evaluate_method(WMID_GUID3, 0, method_id, &input, &result);
 
     if (ACPI_FAILURE(status))
         return status;
     obj = (union acpi_object *) result.pointer;
 
     if (obj) {
         if (obj->type == ACPI_TYPE_BUFFER) {
             if (obj->buffer.length == sizeof(u32))
                 tmp = *((u32 *) obj->buffer.pointer);
             else if (obj->buffer.length == sizeof(u64))
                 tmp = *((u64 *) obj->buffer.pointer);
         } else if (obj->type == ACPI_TYPE_INTEGER) {
             tmp = (u64) obj->integer.value;
         }
     }
 
     if (out)
         *out = tmp;
 
     kfree(result.pointer);
 
     return status;
 }
 /*
  * WMID Gaming interface
  */
 
 static acpi_status
 WMI_gaming_execute_u64(u32 method_id, u64 in, u64 *out)
 {
     struct acpi_buffer input = { (acpi_size) sizeof(u64), (void *)(&in) };
     struct acpi_buffer result = { ACPI_ALLOCATE_BUFFER, NULL };
     union acpi_object *obj;
     u64 tmp = 0;
     acpi_status status;
 
     status = wmi_evaluate_method(WMID_GUID4, 0, method_id, &input, &result);
 
     if (ACPI_FAILURE(status))
         return status;
     obj = (union acpi_object *) result.pointer;
 
     if (obj) {
         if (obj->type == ACPI_TYPE_BUFFER) {
             if (obj->buffer.length == sizeof(u32))
                 tmp = *((u32 *) obj->buffer.pointer);
             else if (obj->buffer.length == sizeof(u64))
                 tmp = *((u64 *) obj->buffer.pointer);
         } else if (obj->type == ACPI_TYPE_INTEGER) {
             tmp = (u64) obj->integer.value;
         }
     }
 
     if (out)
         *out = tmp;
 
     kfree(result.pointer);
 
     return status;
 }
 
 static int WMI_gaming_execute_u32_u64(u32 method_id, u32 in, u64 *out)
 {
     struct acpi_buffer result = { ACPI_ALLOCATE_BUFFER, NULL };
     struct acpi_buffer input = {
         .length = sizeof(in),
         .pointer = &in,
     };
     union acpi_object *obj;
     acpi_status status;
     int ret = 0;
 
     status = wmi_evaluate_method(WMID_GUID4, 0, method_id, &input, &result);
     if (ACPI_FAILURE(status))
         return -EIO;
 
     obj = result.pointer;
     if (obj && out) {
         switch (obj->type) {
         case ACPI_TYPE_INTEGER:
             *out = obj->integer.value;
             break;
         case ACPI_TYPE_BUFFER:
             if (obj->buffer.length < sizeof(*out))
                 ret = -ENOMSG;
             else
                 *out = get_unaligned_le64(obj->buffer.pointer);
 
             break;
         default:
             ret = -ENOMSG;
             break;
         }
     }
 
     kfree(obj);
 
     return ret;
 }

 static acpi_status WMID_gaming_set_u64(u64 value, u32 cap)
 {
     u32 method_id = 0;
 
     if (!(interface->capability & cap))
         return AE_BAD_PARAMETER;
 
     switch (cap) {
     case ACER_CAP_TURBO_LED:
         method_id = ACER_WMID_SET_GAMING_LED_METHODID;
         break;
     case ACER_CAP_TURBO_FAN:
         method_id = ACER_WMID_SET_GAMING_FAN_BEHAVIOR_METHODID;
         break;
     default:
         return AE_BAD_PARAMETER;
     }
 
     return WMI_gaming_execute_u64(method_id, value, NULL);
 }
 
 static acpi_status WMID_gaming_get_u64(u64 *value, u32 cap)
 {
     acpi_status status;
     u64 result;
     u64 input;
     u32 method_id;
 
     if (!(interface->capability & cap))
         return AE_BAD_PARAMETER;
 
     switch (cap) {
     case ACER_CAP_TURBO_LED:
         method_id = ACER_WMID_GET_GAMING_LED_METHODID;
         input = 0x1;
         break;
     default:
         return AE_BAD_PARAMETER;
     }
     status = WMI_gaming_execute_u64(method_id, input, &result);
     if (ACPI_SUCCESS(status))
         *value = (u64) result;
 
     return status;
 }
 
 static int WMID_gaming_get_sys_info(u32 command, u64 *out)
 {
     acpi_status status;
     u64 result;
 
     status = WMI_gaming_execute_u64(ACER_WMID_GET_GAMING_SYS_INFO_METHODID, command, &result);
     if (ACPI_FAILURE(status))
         return -EIO;
 
     /* The return status must be zero for the operation to have succeeded */
     if (FIELD_GET(ACER_PREDATOR_V4_RETURN_STATUS_BIT_MASK, result))
         return -EIO;
 
     *out = result;
 
     return 0;
 }

 static void WMID_gaming_set_fan_mode(u8 fan_mode)
 {
     /* fan_mode = 1 is used for auto, fan_mode = 2 used for turbo*/
     u64 gpu_fan_config1 = 0, gpu_fan_config2 = 0;
     int i;
 
     if (quirks->cpu_fans > 0)
         gpu_fan_config2 |= 1;
     for (i = 0; i < (quirks->cpu_fans + quirks->gpu_fans); ++i)
         gpu_fan_config2 |= 1 << (i + 1);
     for (i = 0; i < quirks->gpu_fans; ++i)
         gpu_fan_config2 |= 1 << (i + 3);
     if (quirks->cpu_fans > 0)
         gpu_fan_config1 |= fan_mode;
     for (i = 0; i < (quirks->cpu_fans + quirks->gpu_fans); ++i)
         gpu_fan_config1 |= fan_mode << (2 * i + 2);
     for (i = 0; i < quirks->gpu_fans; ++i)
         gpu_fan_config1 |= fan_mode << (2 * i + 6);
     WMID_gaming_set_u64(gpu_fan_config2 | gpu_fan_config1 << 16, ACER_CAP_TURBO_FAN);
 }

 static int WMID_gaming_set_misc_setting(enum acer_wmi_gaming_misc_setting setting, u8 value)
 {
     acpi_status status;
     u64 input = 0;
     u64 result;
 
     input |= FIELD_PREP(ACER_GAMING_MISC_SETTING_INDEX_MASK, setting);
     input |= FIELD_PREP(ACER_GAMING_MISC_SETTING_VALUE_MASK, value);
 
     status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_MISC_SETTING_METHODID, input, &result);
     if (ACPI_FAILURE(status))
         return -EIO;
 
     /* The return status must be zero for the operation to have succeeded */
     if (FIELD_GET(ACER_GAMING_MISC_SETTING_STATUS_MASK, result))
         return -EIO;
 
     return 0;
 }
 
 static int WMID_gaming_get_misc_setting(enum acer_wmi_gaming_misc_setting setting, u8 *value)
 {
     u64 input = 0;
     u64 result;
     int ret;
 
     input |= FIELD_PREP(ACER_GAMING_MISC_SETTING_INDEX_MASK, setting);
 
     ret = WMI_gaming_execute_u32_u64(ACER_WMID_GET_GAMING_MISC_SETTING_METHODID, input,
                      &result);
     if (ret < 0)
         return ret;
 
     /* The return status must be zero for the operation to have succeeded */
     if (FIELD_GET(ACER_GAMING_MISC_SETTING_STATUS_MASK, result))
         return -EIO;
 
     *value = FIELD_GET(ACER_GAMING_MISC_SETTING_VALUE_MASK, result);
 
     return 0;
 }

/* Fan Speed */
 static acpi_status acer_set_fan_speed(int t_cpu_fan_speed, int t_gpu_fan_speed);
 
 /*
  *  Predator series turbo button
  */
 static int acer_toggle_turbo(void)
 {
     u64 turbo_led_state;
 
     /* Get current state from turbo button */
     if (ACPI_FAILURE(WMID_gaming_get_u64(&turbo_led_state, ACER_CAP_TURBO_LED)))
         return -1;
 
     if (turbo_led_state) {
         /* Turn off turbo led */
         WMID_gaming_set_u64(0x1, ACER_CAP_TURBO_LED);
 
         /* Set FAN mode to auto */
         WMID_gaming_set_fan_mode(0x1);
 
         /* Set OC to normal */
         if (has_cap(ACER_CAP_TURBO_OC)) {
             WMID_gaming_set_misc_setting(ACER_WMID_MISC_SETTING_OC_1,
                              ACER_WMID_OC_NORMAL);
             WMID_gaming_set_misc_setting(ACER_WMID_MISC_SETTING_OC_2,
                              ACER_WMID_OC_NORMAL);
         }
     } else {
         /* Turn on turbo led */
         WMID_gaming_set_u64(0x10001, ACER_CAP_TURBO_LED);
 
         /* Set FAN mode to turbo */
         WMID_gaming_set_fan_mode(0x2);
 
         /* Set OC to turbo mode */
         if (has_cap(ACER_CAP_TURBO_OC)) {
             WMID_gaming_set_misc_setting(ACER_WMID_MISC_SETTING_OC_1,
                              ACER_WMID_OC_TURBO);
             WMID_gaming_set_misc_setting(ACER_WMID_MISC_SETTING_OC_2,
                              ACER_WMID_OC_TURBO);
         }
     }
     return turbo_led_state;
 }
 
 static int
 acer_predator_v4_platform_profile_get(struct device *dev,
                       enum platform_profile_option *profile)
 {
     u8 tp;
     int err;
 
     err = WMID_gaming_get_misc_setting(ACER_WMID_MISC_SETTING_PLATFORM_PROFILE, &tp);
     if (err)
         return err;
 
     switch (tp) {
     case ACER_PREDATOR_V4_THERMAL_PROFILE_TURBO:
         *profile = PLATFORM_PROFILE_PERFORMANCE;
         break;
     case ACER_PREDATOR_V4_THERMAL_PROFILE_PERFORMANCE:
         *profile = PLATFORM_PROFILE_BALANCED_PERFORMANCE;
         break;
     case ACER_PREDATOR_V4_THERMAL_PROFILE_BALANCED:
         *profile = PLATFORM_PROFILE_BALANCED;
         break;
     case ACER_PREDATOR_V4_THERMAL_PROFILE_QUIET:
         *profile = PLATFORM_PROFILE_QUIET;
         break;
     case ACER_PREDATOR_V4_THERMAL_PROFILE_ECO:
         *profile = PLATFORM_PROFILE_LOW_POWER;
         break;
     default:
         return -EOPNOTSUPP;
     }
 
     return 0;
 }
 
 static int
 acer_predator_v4_platform_profile_set(struct device *dev,
                       enum platform_profile_option profile)
 {
     int err,tp;
     acpi_status status;
     u64 on_AC;
 
     /* Check Power Source */
     status = WMI_gaming_execute_u64(
         ACER_WMID_GET_GAMING_SYS_INFO_METHODID,
         ACER_WMID_CMD_GET_PREDATOR_V4_BAT_STATUS, &on_AC);
 
     if (ACPI_FAILURE(status))
         return -EIO;
 
     /* Check power source */
     /* Blocking these modes since in official version this is not supported when its not plugged in AC! */
     if(!on_AC && (profile == PLATFORM_PROFILE_PERFORMANCE || profile == PLATFORM_PROFILE_BALANCED_PERFORMANCE || profile == PLATFORM_PROFILE_QUIET)){
         return -EOPNOTSUPP;
     }
 
     /* turn the fan down i mean its quiet mode | eco mode after all*/
     if(profile == PLATFORM_PROFILE_QUIET || profile == PLATFORM_PROFILE_LOW_POWER) {
         acpi_status stat = acer_set_fan_speed(0,0);
         if(ACPI_FAILURE(stat)){
             return -EIO;
         }
     }
 
     switch (profile) {
     case PLATFORM_PROFILE_PERFORMANCE:
         tp = ACER_PREDATOR_V4_THERMAL_PROFILE_TURBO;
         break;
     case PLATFORM_PROFILE_BALANCED_PERFORMANCE:
         tp = ACER_PREDATOR_V4_THERMAL_PROFILE_PERFORMANCE;
         break;
     case PLATFORM_PROFILE_BALANCED:
         tp = ACER_PREDATOR_V4_THERMAL_PROFILE_BALANCED;
         break;
     case PLATFORM_PROFILE_QUIET:
         tp = ACER_PREDATOR_V4_THERMAL_PROFILE_QUIET;
         break;
     case PLATFORM_PROFILE_LOW_POWER:
         tp = ACER_PREDATOR_V4_THERMAL_PROFILE_ECO;
         break;
     default:
         return -EOPNOTSUPP;
     }
 
     err = WMID_gaming_set_misc_setting(ACER_WMID_MISC_SETTING_PLATFORM_PROFILE, tp);
     if (err)
         return err;
 
     if (tp != acer_predator_v4_max_perf)
         last_non_turbo_profile = tp;
 
     return 0;
 }
 
 static int
 acer_predator_v4_platform_profile_probe(void *drvdata, unsigned long *choices)
 {
     unsigned long supported_profiles;
     int err;
 
     err = WMID_gaming_get_misc_setting(ACER_WMID_MISC_SETTING_SUPPORTED_PROFILES,
                        (u8 *)&supported_profiles);
     if (err)
         return err;
 
     /* Iterate through supported profiles in order of increasing performance */
     if (test_bit(ACER_PREDATOR_V4_THERMAL_PROFILE_ECO, &supported_profiles)) {
         set_bit(PLATFORM_PROFILE_LOW_POWER, choices);
         acer_predator_v4_max_perf = ACER_PREDATOR_V4_THERMAL_PROFILE_ECO;
         last_non_turbo_profile = ACER_PREDATOR_V4_THERMAL_PROFILE_ECO;
     }
 
     if (test_bit(ACER_PREDATOR_V4_THERMAL_PROFILE_QUIET, &supported_profiles)) {
         set_bit(PLATFORM_PROFILE_QUIET, choices);
         acer_predator_v4_max_perf = ACER_PREDATOR_V4_THERMAL_PROFILE_QUIET;
         last_non_turbo_profile = ACER_PREDATOR_V4_THERMAL_PROFILE_QUIET;
     }
 
     if (test_bit(ACER_PREDATOR_V4_THERMAL_PROFILE_BALANCED, &supported_profiles)) {
         set_bit(PLATFORM_PROFILE_BALANCED, choices);
         acer_predator_v4_max_perf = ACER_PREDATOR_V4_THERMAL_PROFILE_BALANCED;
         last_non_turbo_profile = ACER_PREDATOR_V4_THERMAL_PROFILE_BALANCED;
     }
 
     if (test_bit(ACER_PREDATOR_V4_THERMAL_PROFILE_PERFORMANCE, &supported_profiles)) {
         set_bit(PLATFORM_PROFILE_BALANCED_PERFORMANCE, choices);
         acer_predator_v4_max_perf = ACER_PREDATOR_V4_THERMAL_PROFILE_PERFORMANCE;
 
         /* We only use this profile as a fallback option in case no prior
          * profile is supported.
          */
         if (last_non_turbo_profile < 0)
             last_non_turbo_profile = ACER_PREDATOR_V4_THERMAL_PROFILE_PERFORMANCE;
     }
 
     if (test_bit(ACER_PREDATOR_V4_THERMAL_PROFILE_TURBO, &supported_profiles)) {
         set_bit(PLATFORM_PROFILE_PERFORMANCE, choices);
         acer_predator_v4_max_perf = ACER_PREDATOR_V4_THERMAL_PROFILE_TURBO;
 
         /* We need to handle the hypothetical case where only the turbo profile
          * is supported. In this case the turbo toggle will essentially be a
          * no-op.
          */
         if (last_non_turbo_profile < 0)
             last_non_turbo_profile = ACER_PREDATOR_V4_THERMAL_PROFILE_TURBO;
     }
 
     return 0;
 }
 
 static int acer_predator_state_update(int value);
 
 static acpi_status acer_predator_state_restore(int value);
 
 static acpi_status battery_health_set(u8 function, u8 function_status);
 
 static const struct platform_profile_ops acer_predator_v4_platform_profile_ops = {
     .probe = acer_predator_v4_platform_profile_probe,
     .profile_get = acer_predator_v4_platform_profile_get,
     .profile_set = acer_predator_v4_platform_profile_set,
 };
 
 static int acer_platform_profile_setup(struct platform_device *pdev)
 {
     const int max_retries = 10;
     int delay_ms = 100;
     if (!quirks->predator_v4 && !quirks->nitro_sense && !quirks->nitro_v4)
         return 0;
     for (int attempt = 1; attempt <= max_retries; attempt++) {
         platform_profile_device = devm_platform_profile_register(
             &pdev->dev, "acer-wmi", NULL, &acer_predator_v4_platform_profile_ops);
         if (!IS_ERR(platform_profile_device)) {
             platform_profile_support = true;
             pr_info("Platform profile registered successfully (attempt %d)\n", attempt);
             return 0;
         }
         pr_warn("Platform profile registration failed (attempt %d/%d), error: %ld\n",
                 attempt, max_retries, PTR_ERR(platform_profile_device));
         if (attempt < max_retries) {
             msleep(delay_ms);
             delay_ms = min(delay_ms * 2, 1000);
         }
     }
     return PTR_ERR(platform_profile_device);
 }
 
 static int acer_thermal_profile_change(void)
 {
     /*
      * This mode key can rotate each mode or toggle turbo mode.
      * On battery, only ECO and BALANCED mode are available.
      */
     if (quirks->predator_v4 || quirks->nitro_sense || quirks->nitro_v4) {
         u8 current_tp;
         int tp, err;
         u64 on_AC;
         acpi_status status;
         err = WMID_gaming_get_misc_setting(ACER_WMID_MISC_SETTING_PLATFORM_PROFILE, &current_tp);
         if (err)
             return err;
         /* Check power source */
         status = WMI_gaming_execute_u64(
             ACER_WMID_GET_GAMING_SYS_INFO_METHODID,
             ACER_WMID_CMD_GET_PREDATOR_V4_BAT_STATUS, &on_AC);
         
         if (ACPI_FAILURE(status))
             return -EIO;
         
         /* On AC - define next profile transitions */
         if (!on_AC) {
            if (current_tp == ACER_PREDATOR_V4_THERMAL_PROFILE_ECO)
                tp = ACER_PREDATOR_V4_THERMAL_PROFILE_BALANCED;
            else
                tp = ACER_PREDATOR_V4_THERMAL_PROFILE_ECO;
        } else {
            switch (current_tp) {
            case ACER_PREDATOR_V4_THERMAL_PROFILE_TURBO:
                tp = cycle_gaming_thermal_profile
                     ? ACER_PREDATOR_V4_THERMAL_PROFILE_QUIET
                     : last_non_turbo_profile;
                break;
            case ACER_PREDATOR_V4_THERMAL_PROFILE_PERFORMANCE:
                tp = (acer_predator_v4_max_perf == current_tp)
                     ? last_non_turbo_profile
                     : acer_predator_v4_max_perf;
                break;
            case ACER_PREDATOR_V4_THERMAL_PROFILE_BALANCED:
                tp = cycle_gaming_thermal_profile
                     ? ACER_PREDATOR_V4_THERMAL_PROFILE_PERFORMANCE
                     : acer_predator_v4_max_perf;
                break;
            case ACER_PREDATOR_V4_THERMAL_PROFILE_QUIET:
                tp = cycle_gaming_thermal_profile
                     ? ACER_PREDATOR_V4_THERMAL_PROFILE_BALANCED
                     : acer_predator_v4_max_perf;
                break;
            case ACER_PREDATOR_V4_THERMAL_PROFILE_ECO:
                tp = cycle_gaming_thermal_profile
                     ? ACER_PREDATOR_V4_THERMAL_PROFILE_QUIET
                     : acer_predator_v4_max_perf;
                break;
            default:
                return -EOPNOTSUPP;
            }
        }

         err = WMID_gaming_set_misc_setting(ACER_WMID_MISC_SETTING_PLATFORM_PROFILE, tp);
         if (err)
             return err;
 
         /* the quiter you become the more you'll be able to hear! */
         if(tp == ACER_PREDATOR_V4_THERMAL_PROFILE_QUIET || tp == ACER_PREDATOR_V4_THERMAL_PROFILE_ECO) {
             acpi_status stat = acer_set_fan_speed(0,0);
             if(ACPI_FAILURE(stat)){
                 return -EIO;
             }
         }
         /* Store non-turbo profile for turbo mode toggle*/
         if (tp != acer_predator_v4_max_perf)
             last_non_turbo_profile = tp;
 
         platform_profile_notify(platform_profile_device);
     }
 
     return 0;
 }
 
 static void acer_wmi_notify(union acpi_object *obj, void *context)
 {
     struct event_return_value return_value;

     if (!obj)
         return;
     if (obj->type != ACPI_TYPE_BUFFER) {
         pr_warn("Unknown response received %d\n", obj->type);
         return;
     }
     if (obj->buffer.length != 8) {
         pr_warn("Unknown buffer length %d\n", obj->buffer.length);
         return;
     }

     return_value = *((struct event_return_value *)obj->buffer.pointer);

     switch (return_value.function) {
     case WMID_GAMING_TURBO_KEY_EVENT:
         pr_info("pressed turbo button - %d\n", return_value.key_num);
         if (return_value.key_num == 0x4 && !has_cap(ACER_CAP_NITRO_SENSE_V4))
             acer_toggle_turbo();
         if ((return_value.key_num == 0x5 ||
              (return_value.key_num == 0x4 && has_cap(ACER_CAP_NITRO_SENSE_V4))) &&
             has_cap(ACER_CAP_PLATFORM_PROFILE))
             acer_thermal_profile_change();
         break;
     case WMID_AC_EVENT:
         if (has_cap(ACER_CAP_PREDATOR_SENSE) || has_cap(ACER_CAP_NITRO_SENSE_V4)) {
             if (return_value.key_num == 0) {
                 acer_predator_state_update(1);
                 acer_predator_state_restore(0);
             } else if (return_value.key_num == 1) {
                 acer_predator_state_update(0);
                 acer_predator_state_restore(1);
             } else {
                 pr_info("Unknown key number - %d\n", return_value.key_num);
             }
         }
         break;
     case WMID_CALIBRATION_EVENT:
         if (has_cap(ACER_CAP_PREDATOR_SENSE) || has_cap(ACER_CAP_NITRO_SENSE) || has_cap(ACER_CAP_NITRO_SENSE_V4)) {
             if (battery_health_set(CALIBRATION_MODE, return_value.key_num) != AE_OK)
                 pr_err("Error changing calibration state\n");
         }
         break;
     default:
         break;
     }
 }
 
 static int acer_wmi_hwmon_init(void);
 
 
 /*
  * USB Charging
  */
 static ssize_t predator_usb_charging_show(struct device *dev, struct device_attribute *attr,char *buf){
     acpi_status status;
     u64 result;
     status = WMI_apgeaction_execute_u64(ACER_WMID_GET_FUNCTION,0x4,&result);
     if(ACPI_FAILURE(status)){
         pr_err("Error getting usb charging status: %s\n",acpi_format_exception(status));
         return -ENODEV;
     }
     pr_info("usb charging get status: %llu\n",result);
     return sprintf(buf, "%d\n", result == 663296 ? 0 : result == 659200 ? 10 : result == 1314560 ? 20 : result == 1969920 ? 30 : -1); //-1 means unknown value
 }
 
 static ssize_t predator_usb_charging_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t count){
     acpi_status status;
     u64 result;
     u8 val;
       if (sscanf(buf, "%hhd", &val) != 1)
         return -EINVAL;
       if ((val != 0) && (val != 10) && (val != 20) && (val != 30))
         return -EINVAL;
     pr_info("usb charging set value: %d\n",val);
     status = WMI_apgeaction_execute_u64(ACER_WMID_SET_FUNCTION,val == 0 ? 663300 : val == 10 ? 659204 : val == 20 ? 1314564 : val == 30 ? 1969924 : 663300, &result); //if unkown value then turn it off.
     if(ACPI_FAILURE(status)){
         pr_err("Error setting usb charging status: %s\n",acpi_format_exception(status));
         return -ENODEV;
     }
     pr_info("usb charging set status: %llu\n",result);
     return count;
 }
 
 /*
  * Battery Limit (80%)
  * Battery Calibration
  */
 struct get_battery_health_control_status_input {
     u8 uBatteryNo;
     u8 uFunctionQuery;
     u8 uReserved[2];
 } __packed;
 
 struct get_battery_health_control_status_output {
     u8 uFunctionList;
     u8 uReturn[2];
     u8 uFunctionStatus[5];
 } __packed;
 
 struct set_battery_health_control_input {
     u8 uBatteryNo;
     u8 uFunctionMask;
     u8 uFunctionStatus;
     u8 uReservedIn[5];
 } __packed;
 
 struct set_battery_health_control_output {
     u8 uReturn;
     u8 uReservedOut;
 } __packed;
 
 
 static acpi_status battery_health_query(int mode, int *enabled){
     pr_info("battery health query: %d\n",mode);
     acpi_status status;
     union acpi_object *obj;
     struct get_battery_health_control_status_input params = {
         .uBatteryNo = 0x1,
         .uFunctionQuery = 0x1,
         .uReserved = { 0x0, 0x0 }
     };
     struct get_battery_health_control_status_output ret;
 
     struct acpi_buffer input = {
         sizeof(struct get_battery_health_control_status_input), &params
     };
 
     struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
 
     status = wmi_evaluate_method(WMID_GUID5, 0, ACER_WMID_GET_BATTERY_HEALTH_CONTROL_STATUS_METHODID, &input, &output);
     if (ACPI_FAILURE(status))
         return status;
     
     obj = output.pointer;
 
       if (!obj || obj->type != ACPI_TYPE_BUFFER || obj->buffer.length != 8) {
         pr_err("Unexpected output format getting battery health status, buffer "
                "length:%d\n",
                obj->buffer.length);
         goto failed;
       }
     
       ret = *((struct get_battery_health_control_status_output *)obj->buffer.pointer);
     
     if(mode == HEALTH_MODE){
         *enabled = ret.uFunctionStatus[0];
     } else if(mode == CALIBRATION_MODE){
         *enabled = ret.uFunctionStatus[1];
     } else {
         goto failed;
     }
 
     kfree(obj);
     return AE_OK;
 
     failed:
           kfree(obj);
           return AE_ERROR;
 }
 
 static acpi_status battery_health_set(u8 function, u8 function_status){
 
     pr_info("battery_health_set: %d | %d\n",function,function_status);
     
     acpi_status status;
     union acpi_object *obj;
     struct set_battery_health_control_input params = {
         .uBatteryNo = 0x1,
         .uFunctionMask = function,
         .uFunctionStatus = function_status,
         .uReservedIn = { 0x0, 0x0, 0x0, 0x0, 0x0 }
     };
     struct set_battery_health_control_output  ret;
 
     struct acpi_buffer input = {
         sizeof(struct set_battery_health_control_input), &params
     };
 
     struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
 
     status = wmi_evaluate_method(WMID_GUID5, 0, ACER_WMID_SET_BATTERY_HEALTH_CONTROL_METHODID, &input, &output);
     if (ACPI_FAILURE(status))
         return status;
     
     obj = output.pointer;
 
       if (!obj || obj->type != ACPI_TYPE_BUFFER || obj->buffer.length != 4) {
         pr_err("Unexpected output format getting battery health status, buffer "
                "length:%d\n",
                obj->buffer.length);
         goto failed;
       }
     
       ret = *((struct set_battery_health_control_output  *)obj->buffer.pointer);
     
     if(ret.uReturn != 0 && ret.uReservedOut != 0){
         pr_err("Failed to set battery health status\n");
         goto failed;
     }
 
     kfree(obj);
     return AE_OK;
 
     failed:
           kfree(obj);
           return AE_ERROR;
 }
 
 
 static ssize_t predator_battery_limit_show(struct device *dev,
                                            struct device_attribute *attr,
                                            char *buf) {
 
     int enabled;
     acpi_status status = battery_health_query(HEALTH_MODE, &enabled);
 
     if (ACPI_FAILURE(status))
         return -ENODEV;
 
     return sprintf(buf, "%d\n", enabled);
 }
 
 static ssize_t predator_battery_limit_store(struct device *dev,
                                             struct device_attribute *attr,
                                             const char *buf, size_t count) {
     u8 val;
       if (sscanf(buf, "%hhd", &val) != 1)
         return -EINVAL;
 
       if ((val != 0) && (val != 1))
         return -EINVAL;
       
     if (battery_health_set(HEALTH_MODE,val) != AE_OK)
         return -ENODEV;
       
     return count;
 }
 
 static ssize_t predator_battery_calibration_show(struct device *dev,
                                            struct device_attribute *attr,
                                            char *buf) {
 
     int enabled;
     acpi_status status = battery_health_query(CALIBRATION_MODE, &enabled);
 
     if (ACPI_FAILURE(status))
         return -ENODEV;
 
     return sprintf(buf, "%d\n", enabled);
 }
 
 static ssize_t preadtor_battery_calibration_store(struct device *dev,
                                             struct device_attribute *attr,
                                             const char *buf, size_t count) {
     u8 val;
       if (sscanf(buf, "%hhd", &val) != 1)
         return -EINVAL;
       
     if ((val != 0) && (val != 1))
         return -EINVAL;
       
     if (battery_health_set(CALIBRATION_MODE,val) != AE_OK)
         return -ENODEV;
       
     return count;
 }
 
 
 /*
  * FAN CONTROLS
  */
 static int cpu_fan_speed = 0;
 static int gpu_fan_speed = 0;
 
 static u64 fan_val_calc(int percentage, int fan_index) {
     return (((percentage * 25600) / 100) & 0xFF00) + fan_index;
 }
 static acpi_status acer_set_fan_speed(int t_cpu_fan_speed, int t_gpu_fan_speed){
     
     acpi_status status;
 
     if (t_cpu_fan_speed == 100 && t_gpu_fan_speed == 100) {
         pr_info("MAX FAN MODE!\n");
         status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_FAN_BEHAVIOR_METHODID, 0x820009, NULL);
         if(ACPI_FAILURE(status)){
             pr_err("Error setting fan speed status: %s\n",acpi_format_exception(status));
             return AE_ERROR;
         }
     } else if (t_cpu_fan_speed == 0 && t_gpu_fan_speed == 0) {
         pr_info("AUTO FAN MODE!\n");
         status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_FAN_BEHAVIOR_METHODID, 0x410009, NULL);
         if(ACPI_FAILURE(status)){
             pr_err("Error setting fan speed status: %s\n",acpi_format_exception(status));
             return AE_ERROR;
         }
     } else if (t_cpu_fan_speed <= 100 && t_gpu_fan_speed <= 100) {
         if (t_cpu_fan_speed == 0) {
             pr_info("CUSTOM FAN MODE (GPU)\n");
             status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_FAN_BEHAVIOR_METHODID, 0x10001, NULL);
             if(ACPI_FAILURE(status)){
                 pr_err("Error setting fan speed status: %s\n",acpi_format_exception(status));
                 return AE_ERROR;
             }
             status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_FAN_BEHAVIOR_METHODID, 0xC00008, NULL);
             if(ACPI_FAILURE(status)){
                 pr_err("Error setting fan speed status: %s\n",acpi_format_exception(status));
                 return AE_ERROR;
             }
             status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_FAN_SPEED_METHODID, fan_val_calc(t_gpu_fan_speed,4), NULL);
             if(ACPI_FAILURE(status)){
                 pr_err("Error setting fan speed status: %s\n",acpi_format_exception(status));
                 return AE_ERROR;
             }
         } else if (t_gpu_fan_speed == 0) {
             pr_info("CUSTOM FAN MODE (CPU)\n");
             status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_FAN_BEHAVIOR_METHODID, 0x400008, NULL);
             if(ACPI_FAILURE(status)){
                 pr_err("Error setting fan speed status: %s\n",acpi_format_exception(status));
                 return AE_ERROR;
             }
             status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_FAN_BEHAVIOR_METHODID, 0x30001, NULL);
             if(ACPI_FAILURE(status)){
                 pr_err("Error setting fan speed status: %s\n",acpi_format_exception(status));
                 return AE_ERROR;
             }
             status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_FAN_SPEED_METHODID, fan_val_calc(t_cpu_fan_speed,1), NULL);
             if(ACPI_FAILURE(status)){
                 pr_err("Error setting fan speed status: %s\n",acpi_format_exception(status));
                 return AE_ERROR;
             }
         } else {
             pr_info("CUSTOM FAN MODE (MIXED)!\n");
             //set gaming behvaiour mode to custom
             status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_FAN_BEHAVIOR_METHODID, 0xC30009, NULL);
             if(ACPI_FAILURE(status)){
                 pr_err("Error setting fan speed status: %s\n",acpi_format_exception(status));
                 return AE_ERROR;
             }
             //set cpu fan speed
             status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_FAN_SPEED_METHODID, fan_val_calc(t_cpu_fan_speed,1), NULL);
             if(ACPI_FAILURE(status)){
                 pr_err("Error setting fan speed status: %s\n",acpi_format_exception(status));
                 return AE_ERROR;
             }
             //set gpu fan speed
             status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_FAN_SPEED_METHODID, fan_val_calc(t_gpu_fan_speed,4), NULL);
             if(ACPI_FAILURE(status)){
                 pr_err("Error setting fan speed status: %s\n",acpi_format_exception(status));
                 return AE_ERROR;
             }
         }
     } else {
         return AE_ERROR;
     }
 
     cpu_fan_speed = t_cpu_fan_speed;
     gpu_fan_speed = t_gpu_fan_speed;
     pr_info("Fan speeds updated: CPU=%d, GPU=%d\n", cpu_fan_speed, gpu_fan_speed);
 
     return AE_OK;	
 }
 
 static ssize_t predator_fan_speed_show(struct device *dev,
                                            struct device_attribute *attr,
                                            char *buf) {
   return sprintf(buf, "%d,%d\n", cpu_fan_speed, gpu_fan_speed);                           
 }
 
 
 static ssize_t predator_fan_speed_store(struct device *dev,
                                             struct device_attribute *attr,
                                             const char *buf, size_t count) {
     int t_cpu_fan_speed, t_gpu_fan_speed;
     
     char input[9];
     char *token;
     char* input_ptr = input;
     size_t len = min(count, sizeof(input) - 1);
     strncpy(input, buf, len);
 
     if(input[len-1] == '\n'){
         input[len-1] = '\0';
     } else {
         input[len] = '\0';
     }
 
 
     token = strsep(&input_ptr, ",");
     if (!token || kstrtoint(token, 10, &t_cpu_fan_speed) || t_cpu_fan_speed < 0 || t_cpu_fan_speed > 100) {
         pr_err("Invalid CPU speed value.\n");
         return -EINVAL;
     }
 
     token = strsep(&input_ptr, ",");
     if (!token || kstrtoint(token, 10, &t_gpu_fan_speed) || t_gpu_fan_speed < 0 || t_gpu_fan_speed > 100) {
         pr_err("Invalid GPU speed value.\n");
         return -EINVAL;
     }
 
     acpi_status status = acer_set_fan_speed(t_cpu_fan_speed, t_gpu_fan_speed);
     if(ACPI_FAILURE(status)){
         return -ENODEV;
     } 
 
     return count;
 }
 /*
  * persistent predator states.
  */
 struct acer_predator_state {
     int cpu_fan_speed;
     int gpu_fan_speed;
     int thermal_profile;
 };
 
 struct power_states {
     struct acer_predator_state battery_state;
     struct acer_predator_state ac_state;
 } __attribute__((packed));
 
 static struct power_states current_states = {
     .battery_state = {0, 0, ACER_PREDATOR_V4_THERMAL_PROFILE_ECO},
     .ac_state = {0, 0, ACER_PREDATOR_V4_THERMAL_PROFILE_BALANCED}
 };
 
 static int acer_predator_state_update(int value){
     u8 current_tp;
     int tp, err;
     err = WMID_gaming_get_misc_setting(
        ACER_WMID_MISC_SETTING_PLATFORM_PROFILE, 
        &current_tp);
     if (err)
         return err;
     switch (current_tp) {
         case ACER_PREDATOR_V4_THERMAL_PROFILE_TURBO:
             tp = ACER_PREDATOR_V4_THERMAL_PROFILE_TURBO;
             break;
         case ACER_PREDATOR_V4_THERMAL_PROFILE_PERFORMANCE:
             tp = ACER_PREDATOR_V4_THERMAL_PROFILE_PERFORMANCE;
             break;
         case ACER_PREDATOR_V4_THERMAL_PROFILE_BALANCED:
             tp = ACER_PREDATOR_V4_THERMAL_PROFILE_BALANCED;
             break;
         case ACER_PREDATOR_V4_THERMAL_PROFILE_QUIET:
             tp = ACER_PREDATOR_V4_THERMAL_PROFILE_QUIET;
             break;
         case ACER_PREDATOR_V4_THERMAL_PROFILE_ECO:
             tp = ACER_PREDATOR_V4_THERMAL_PROFILE_ECO;
             break;
         default:
             return -1;
     }
     /* When AC is connected */
     if(value == 1){
         current_states.ac_state.thermal_profile = tp;
         current_states.ac_state.cpu_fan_speed = cpu_fan_speed;
         current_states.ac_state.gpu_fan_speed = gpu_fan_speed;
     /* When AC isn't connected */
     } else if(value == 0){
         current_states.battery_state.thermal_profile = tp;
         current_states.battery_state.cpu_fan_speed = cpu_fan_speed;
         current_states.battery_state.gpu_fan_speed = gpu_fan_speed;
     } else {
         pr_err("invalid value received: %d\n", value);
         return -1;
     }
     return 0;
 }
 
 static acpi_status acer_predator_state_restore(int value){
     int err = WMID_gaming_set_misc_setting(ACER_WMID_MISC_SETTING_PLATFORM_PROFILE, 
                                        value == 0 ? current_states.battery_state.thermal_profile : current_states.ac_state.thermal_profile);
     if (err)
         return err;
 
     acpi_status status = acer_set_fan_speed(value == 0 ? current_states.battery_state.cpu_fan_speed : current_states.ac_state.cpu_fan_speed, 
                                 value == 0 ? current_states.battery_state.gpu_fan_speed : current_states.ac_state.gpu_fan_speed);
     if(ACPI_FAILURE(status)){
         return AE_ERROR;
     } 
 
     return AE_OK;
 }
 
 static int acer_predator_state_load(void)
 {
     u64 on_AC;
     struct file *file;
     ssize_t len;
     acpi_status status;
 
     file = filp_open(STATE_FILE, O_RDONLY, 0);
     if (!IS_ERR(file)) {
 
         len = kernel_read(file, (char *)&current_states, sizeof(current_states), &file->f_pos);
         filp_close(file, NULL);
 
         if (len != sizeof(current_states)) {
             pr_err("Incomplete state read, using defaults\n");
         } else {
             pr_info("Thermal states loaded\n");
         }
     } else {
         pr_info("State file not found, loading defaults\n");
     }
 
     /* Always proceed to restore state based on power source */
     status = WMI_gaming_execute_u64(
         ACER_WMID_GET_GAMING_SYS_INFO_METHODID,
         ACER_WMID_CMD_GET_PREDATOR_V4_BAT_STATUS, &on_AC);
 
     if (ACPI_FAILURE(status)) {
         pr_err("Failed to query power source state\n");
         return -1;
     }
 
     /* Restore state based on power source (0 for battery, 1 for AC) */
     status = acer_predator_state_restore(on_AC == 0 ? 0 : 1);
     if (ACPI_FAILURE(status)) {
         pr_err("Failed to restore thermal state\n");
         return -1;
     }
 
     pr_info("Thermal states restored successfully\n");
     return 0;
 }
 
 
 static int acer_predator_state_save(void){
     u64 on_AC;
     acpi_status status;
     struct file *file;
     ssize_t len;
 
     status = WMI_gaming_execute_u64(
         ACER_WMID_GET_GAMING_SYS_INFO_METHODID,
         ACER_WMID_CMD_GET_PREDATOR_V4_BAT_STATUS, &on_AC);
     if (ACPI_FAILURE(status))
         return -1;
 
     /* update to the latest state based on power source */
     status = acer_predator_state_update(on_AC == 0 ? 0 : 1);
     if (ACPI_FAILURE(status)){
         return -1;
     }
 
     file = filp_open(STATE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
     if(!file) {
         pr_info("state_access - Error opening file\n");
         return -1;
     }
 
     len = kernel_write(file, (char *)&current_states, sizeof(current_states), &file->f_pos);
     if(len < 0) {
         pr_info("state_access - Error writing to file: %ld\n", len);
         filp_close(file, NULL);
     }
 
     filp_close(file, NULL);
 
     if (len != sizeof(current_states)) {
         pr_err("Failed to write complete state to file\n");
         return -1;
     }
 
     pr_info("Thermal states saved successfully\n");
     return 0;
 }
 
 /*
  *LCD OVERRIDE CONTROLS
  */
 static ssize_t predator_lcd_override_show(struct device *dev, struct device_attribute *attr,char *buf){
     acpi_status status;
     u64 result;
     status = WMI_gaming_execute_u64(ACER_WMID_GET_GAMING_PROFILE_METHODID,0x00,&result);
     if(ACPI_FAILURE(status)){
         pr_err("Error getting lcd override status: %s\n",acpi_format_exception(status));
         return -ENODEV;
     }
     pr_info("lcd override get status: %llu\n",result);
     return sprintf(buf, "%d\n", result == 0x1000001000000 ? 1 : result == 0x1000000 ? 0 : -1);
 }
 
 static ssize_t predator_lcd_override_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t count){
     acpi_status status;
     u64 result;
     u8 val;
       if (sscanf(buf, "%hhd", &val) != 1)
         return -EINVAL;
       if ((val != 0) && (val != 1))
         return -EINVAL;
     pr_info("lcd_override set value: %d\n",val);
     status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_PROFILE_METHODID,val == 1 ? 0x1000000000010 : 0x10, &result);
     if(ACPI_FAILURE(status)){
         pr_err("Error setting lcd override status: %s\n",acpi_format_exception(status));
         return -ENODEV;
     }
     pr_info("lcd override set status: %llu\n",result);
     return count;
 }
 
 /*
  * BACKLIGHT 30 SEC TIMEOUT
  */
 
 static ssize_t predator_backlight_timeout_show(struct device *dev, struct device_attribute *attr,char *buf){
     acpi_status status;
     u64 result;
     status = WMI_apgeaction_execute_u64(ACER_WMID_GET_FUNCTION,0x88401,&result);
     if(ACPI_FAILURE(status)){
         pr_err("Error getting backlight_timeout status: %s\n",acpi_format_exception(status));
         return -ENODEV;
     }
     pr_info("backlight_timeout get status: %llu\n",result);
     return sprintf(buf, "%d\n", result == 0x1E0000080000 ? 1 : result == 0x80000 ? 0 : -1);
 }
 
 static ssize_t predator_backlight_timeout_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t count){
     acpi_status status;
     u64 result;
     u8 val;
       if (sscanf(buf, "%hhd", &val) != 1)
         return -EINVAL;
       if ((val != 0) && (val != 1))
         return -EINVAL;
     pr_info("bascklight_timeout set value: %d\n",val);
     status = WMI_apgeaction_execute_u64(ACER_WMID_SET_FUNCTION,val == 1 ? 0x1E0000088402 : 0x88402, &result);
     if(ACPI_FAILURE(status)){
         pr_err("Error setting backlight_timeout status: %s\n",acpi_format_exception(status));
         return -ENODEV;
     }
     pr_info("backlight_timeout set status: %llu\n",result);
     return count;
 }
 
 /*
  * System Boot Animation & Sound 
  */
 static ssize_t predator_boot_animation_sound_show(struct device *dev, struct device_attribute *attr,char *buf){
     acpi_status status;
     u64 result;
     status = WMI_gaming_execute_u64(ACER_WMID_GET_GAMING_MISC_SETTING_METHODID,0x6,&result);
     if(ACPI_FAILURE(status)){
         pr_err("Error getting boot_animation_sound status: %s\n",acpi_format_exception(status));
         return -ENODEV;
     }
     pr_info("boot_animation_sound get status: %llu\n",result);
     return sprintf(buf, "%d\n", result == 0x100 ? 1 : result == 0 ? 0 : -1);
 }
 
 static ssize_t predator_boot_animation_sound_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t count){
     acpi_status status;
     u64 result;
     u8 val;
       if (sscanf(buf, "%hhd", &val) != 1)
         return -EINVAL;
       if ((val != 0) && (val != 1))
         return -EINVAL;
     pr_info("boot_animation_sound set value: %d\n",val);
     status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_MISC_SETTING_METHODID,val == 1 ? 0x106 : 0x6, &result);
     if(ACPI_FAILURE(status)){
         pr_err("Error setting boot_animation_sound status: %s\n",acpi_format_exception(status));
         return -ENODEV;
     }
     pr_info("boot_animation_sound set status: %llu\n",result);
     return count;
 }

/*
 * LIGHTING RESET CONTROL
 * Calls Method 2 (SetGamingLED) to attempt to un-brick/reset the lighting controller.
 */
static ssize_t predator_lighting_reset_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
    acpi_status status;
    u64 result;
    u8 val;
    
    if (sscanf(buf, "%hhd", &val) != 1)
        return -EINVAL;

    pr_info("Attempting lighting reset (Method 2) with value: %d\n", val);
    
    /* Method 2: SetGamingLED. Valid values unknown, official driver likely uses 1 to enable. */
    status = WMI_gaming_execute_u64(ACER_WMID_SET_GAMING_LED_METHODID, (u64)val, &result);
    
    if(ACPI_FAILURE(status)){
        pr_err("Error performing lighting reset: %s\n", acpi_format_exception(status));
        return -ENODEV;
    }
    
    pr_info("Lighting reset result: %llu\n", result);
    return count;
}
 
 /*
  * predator sense attributes
  */
 static struct device_attribute boot_animation_sound = __ATTR(boot_animation_sound, 0644, predator_boot_animation_sound_show, predator_boot_animation_sound_store);
static struct device_attribute lighting_reset = __ATTR(lighting_reset, 0200, NULL, predator_lighting_reset_store); /* Write-only */
 static struct device_attribute backlight_timeout = __ATTR(backlight_timeout, 0644, predator_backlight_timeout_show, predator_backlight_timeout_store);
 static struct device_attribute usb_charging = __ATTR(usb_charging, 0644, predator_usb_charging_show, predator_usb_charging_store);
 static struct device_attribute battery_calibration = __ATTR(battery_calibration, 0644, predator_battery_calibration_show, preadtor_battery_calibration_store);
 static struct device_attribute battery_limiter = __ATTR(battery_limiter, 0644, predator_battery_limit_show, predator_battery_limit_store);
 static struct device_attribute fan_speed = __ATTR(fan_speed, 0644, predator_fan_speed_show, predator_fan_speed_store);
 static struct device_attribute lcd_override = __ATTR(lcd_override, 0644, predator_lcd_override_show, predator_lcd_override_store);
 static struct attribute *predator_sense_attrs[] = {
     &lcd_override.attr,
    &lighting_reset.attr,
     &fan_speed.attr,
     &battery_limiter.attr,
     &battery_calibration.attr,
     &usb_charging.attr,
     &backlight_timeout.attr,
     &boot_animation_sound.attr,
     NULL
 };
 
 static struct attribute_group preadtor_sense_attr_group = {
     .name = "predator_sense", .attrs = predator_sense_attrs
 };
 

 

 
 /* Four Zoned Keyboard  */
 
 struct get_four_zoned_kb_output {
     u8 gmReturn;
     u8 gmOutput[15];
 } __packed;
 
 static acpi_status set_kb_status(int mode, int speed, int brightness,
                                  int direction, int red, int green, int blue){
     u64 resp = 0;
     u8 gmInput[16] = {mode, speed, brightness, 0, direction, red, green, blue, 3, 1, 0, 0, 0, 0, 0, 0};
     
     acpi_status status;
     union acpi_object *obj;
     struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
     struct acpi_buffer input = { (acpi_size)sizeof(gmInput), (void *)(gmInput) };
     
     status = wmi_evaluate_method(WMID_GUID4, 0, ACER_WMID_SET_GAMING_KB_BACKLIGHT_METHODID, &input, &output);
     if (ACPI_FAILURE(status))
         return status;
 
     obj = (union acpi_object *) output.pointer;
 
     if (obj) {
         if (obj->type == ACPI_TYPE_BUFFER) {
             if (obj->buffer.length == sizeof(u32))
                 resp = *((u32 *) obj->buffer.pointer);
             else if (obj->buffer.length == sizeof(u64))
                 resp = *((u64 *) obj->buffer.pointer);
         } else if (obj->type == ACPI_TYPE_INTEGER) {
             resp = (u64) obj->integer.value;
         }
     }
 
     if(resp != 0){
         pr_err("failed to set keyboard rgb: %llu\n",resp);
         kfree(obj);
         return AE_ERROR;
     }
 
     kfree(obj);
     return status;
 }
 
 static acpi_status get_kb_status(struct get_four_zoned_kb_output *out){
     u64 in = 1;
     acpi_status status;
     union acpi_object *obj;
     struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
     struct acpi_buffer input = { (acpi_size) sizeof(u64), (void *)(&in) };
 
     status = wmi_evaluate_method(WMID_GUID4, 0, ACER_WMID_GET_GAMING_KB_BACKLIGHT_METHODID, &input, &output);
     if (ACPI_FAILURE(status))
         return status;
     
     obj = output.pointer;
 
      if (!obj || obj->type != ACPI_TYPE_BUFFER || obj->buffer.length != 16) {
         pr_err("Unexpected output format getting kb zone status, buffer "
                "length:%d\n",
                obj->buffer.length);
         goto failed;
       }
 
       *out = *((struct get_four_zoned_kb_output  *)obj->buffer.pointer);
 
     kfree(obj);
     return AE_OK;
 
     failed:
           kfree(obj);
           return AE_ERROR;
 }

/* Back logo/lightbar (LB) unified setter/getter via WMBH (WMID_GUID4) */
static acpi_status set_logo_status(int enable, int brightness, int effect,
                                   int red, int green, int blue)
{
    /* Set logo RGB + brightness + enable using Arg1=0x0C (LBLR/LBLG/LBLB/LBLT/LBLF) */
    {
        u8 bhgk[6] = { 1 /* select LB set */, (u8)red, (u8)green, (u8)blue, (u8)brightness, (u8)enable };
        struct acpi_buffer in = { (acpi_size)sizeof(bhgk), (void *)bhgk };
        acpi_status st = wmi_evaluate_method(WMID_GUID4, 0, 12 /* 0x0C */, &in, NULL);
        if (ACPI_FAILURE(st))
            return st;
    }

    /* Also drive the LBLE gate via unified setter (0x14), which some firmware uses for power */
    {
        u8 bhlk[16] = {
            (u8)enable, /* LBLE */
            0,           /* LBLS */
            0,           /* LBBP (ignored for LB) */
            0,           /* reserved */
            0,           /* LBED (no change) */
            0, 0, 0,     /* colors ignored for LB in 0x14 */
            0,           /* LLES */
            2,           /* select LB */
            0, 0, 0, 0, 0, 0
        };
        struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
        struct acpi_buffer in = { (acpi_size)sizeof(bhlk), (void *)(bhlk) };
        acpi_status st = wmi_evaluate_method(WMID_GUID4, 0, ACER_WMID_SET_GAMING_KB_BACKLIGHT_METHODID, &in, &out);
        if (ACPI_FAILURE(st))
            return st;
        if (out.pointer) kfree(out.pointer);
    }

    return AE_OK;
}

static acpi_status get_logo_status(struct get_four_zoned_kb_output *out)
{
    /* Prefer dedicated logo color getter (Arg1=0x0D) for RGB, combine with 0x15 for brightness/enable */
    u8 req = 1; /* select LB color read */
    struct {
        u8 status;
        u8 r,g,b,t,f;
    } __packed col = {0};
    struct acpi_buffer out_col = { ACPI_ALLOCATE_BUFFER, NULL };
    struct acpi_buffer in_col = { (acpi_size)sizeof(req), (void *)&req };
    union acpi_object *obj;

    /* Get color via method id 13 (0x0D) */
    if (ACPI_FAILURE(wmi_evaluate_method(WMID_GUID4, 0, 13, &in_col, &out_col)))
        goto fallback_unified;
    obj = out_col.pointer;
    if (!obj || obj->type != ACPI_TYPE_BUFFER || obj->buffer.length < 6) {
        kfree(obj);
        goto fallback_unified;
    }
    /* At least 6 bytes guaranteed by check above */
    memcpy(&col, obj->buffer.pointer, 6);
    kfree(obj);

    /* Populate outputs using 0x0D data for RGB, brightness and enable. Other fields set to 0. */
    out->gmReturn = 0;
    out->gmOutput[0] = col.f; /* enable */
    out->gmOutput[1] = 0;     /* speed (not used for LB) */
    out->gmOutput[2] = col.t; /* brightness */
    out->gmOutput[3] = 0;
    out->gmOutput[4] = 0;     /* effect not reported here */
    out->gmOutput[5] = col.r;
    out->gmOutput[6] = col.g;
    out->gmOutput[7] = col.b;
    return AE_OK;

fallback_unified:
    {
        u64 sel = 2;
        struct acpi_buffer out_gkb = { ACPI_ALLOCATE_BUFFER, NULL };
        struct acpi_buffer in_gkb = { (acpi_size) sizeof(u64), (void *)(&sel) };
        if (ACPI_FAILURE(wmi_evaluate_method(WMID_GUID4, 0, ACER_WMID_GET_GAMING_KB_BACKLIGHT_METHODID, &in_gkb, &out_gkb)))
            return AE_ERROR;
        obj = out_gkb.pointer;
        if (!obj || obj->type != ACPI_TYPE_BUFFER || obj->buffer.length != 16) {
            kfree(obj);
            return AE_ERROR;
        }
        *out = *((struct get_four_zoned_kb_output  *)obj->buffer.pointer);
        kfree(obj);
        return AE_OK;
    }
}
 
 
 /* KB Backlight State  */;
 
 struct per_zone_color {
     u64 zone1, zone2, zone3, zone4;
     int brightness;
 } __packed;
 
 struct kb_state {
     u8 per_zone;
     u8 mode;
     u8 speed;
     u8 brightness;
     u8 direction;
     u8 red;
     u8 green;
     u8 blue;
     struct per_zone_color zones;
 } __packed;
 
 static struct kb_state current_kb_state;
 
 
 /* four zone mode */
 static ssize_t four_zoned_rgb_kb_show(struct device *dev, struct device_attribute *attr,char *buf){
     acpi_status status;
     struct get_four_zoned_kb_output output;
     status = get_kb_status(&output);
     if(ACPI_FAILURE(status)){
         pr_err("Error getting kb status: %s\n",acpi_format_exception(status));
         return -ENODEV;
     }
     return sprintf(buf, "%d,%d,%d,%d,%d,%d,%d\n",output.gmOutput[0],output.gmOutput[1],output.gmOutput[2],output.gmOutput[4],output.gmOutput[5],output.gmOutput[6],output.gmOutput[7]);
 }
 
 static ssize_t four_zoned_rgb_kb_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
     acpi_status status;
 
     int mode, speed, brightness, direction, red, green, blue;
     char input_buf[30];
     char *token;
     char *input_ptr = input_buf;
     size_t len = min(count, sizeof(input_buf) - 1);
 
     strncpy(input_buf, buf, len);
 
     if(input_buf[len-1] == '\n'){
         input_buf[len-1] = '\0';
     } else {
         input_buf[len] = '\0';
     }
 
     token = strsep(&input_ptr, ",");
     if (!token || kstrtoint(token, 10, &mode) || mode < 0 || mode > 7) {
         pr_err("Invalid mode value.\n");
         return -EINVAL;
     }
 
     token = strsep(&input_ptr, ",");
     if (!token || kstrtoint(token, 10, &speed) || speed < 0 || speed > 9) {
         pr_err("Invalid speed value.\n");
         return -EINVAL;
     }
 
     token = strsep(&input_ptr, ",");
     if (!token || kstrtoint(token, 10, &brightness) || brightness < 0 || brightness > 100) {
         pr_err("Invalid brightness value.\n");
         return -EINVAL;
     }
 
     token = strsep(&input_ptr, ",");
     if (!token || kstrtoint(token, 10, &direction) || ((direction <= 0) && (mode == 0x3 || mode == 0x4 )) || direction < 0 || direction > 2) {
         pr_err("Invalid direction value.\n");
         return -EINVAL;
     }
 
     token = strsep(&input_ptr, ",");
     if (!token || kstrtoint(token, 10, &red) || red < 0 || red > 255) {
         pr_err("Invalid red value.\n");
         return -EINVAL;
     }
 
     token = strsep(&input_ptr, ",");
     if (!token || kstrtoint(token, 10, &green) || green < 0 || green > 255) {
         pr_err("Invalid green value.\n");
         return -EINVAL;
     }
 
     token = strsep(&input_ptr, ",");
     if (!token || kstrtoint(token, 10, &blue) || blue < 0 || blue > 255) {
         pr_err("Invalid blue value.\n");
         return -EINVAL;
     }
 
     switch (mode) {
         case 0x0:  // Static mode: Ignore speed and direction
             speed = 0;
             direction = 0;
             break;
         case 0x1:  // Breathing mode: Ignore speed
             speed = 0;
             direction = 0;
             break;
         case 0x2:  // Neon mode: Ignore red, green, blue, and direction
             red = 0;
             green = 0;
             blue = 0;
             direction = 0;
             break;
         case 0x3:  // Wave mode: Ignore red, green, and blue
             red = 0;
             green = 0;
             blue = 0;
             break;
         case 0x4:  // Shifting mode: No restrictions (all values allowed)
             break;
         case 0x5:  // Zoom mode: Ignore direction
             direction = 0;
             break;
         case 0x6:  // Meteor mode: Ignore direction
             direction = 0;
             break;
         case 0x7:  // Twinkling mode: Ignore direction
             direction = 0;
             break;
         default:
             pr_err("Invalid mode value.\n");
             return -EINVAL;
     }
 
     status = set_kb_status(mode,speed,brightness,direction,red,green,blue);
     if (ACPI_FAILURE(status)) {
         pr_err("Error setting RGB KB status.\n");
         return -ENODEV;
     }
 
     /* Set per_zone to 0 */
     current_kb_state.per_zone = 0;
 
     return count;
 }
 
 
 /* Per Zone Mode */
 
 static acpi_status get_per_zone_color(struct per_zone_color *output) {
     acpi_status status;
     u64 *zones[] = { &output->zone1, &output->zone2, &output->zone3, &output->zone4 };
     u8 zone_ids[] = { 0x1, 0x2, 0x4, 0x8 };
 
     for (int i = 0; i < 4; i++) {
         status = WMI_gaming_execute_u64(ACER_WMID_GET_GAMING_RGB_KB_METHODID, zone_ids[i], zones[i]);
         if (ACPI_FAILURE(status)) {
             pr_err("Error getting kb status (zone %d): %s\n", i + 1, acpi_format_exception(status));
             return status;
         }
         *zones[i] = cpu_to_be64(*zones[i]) >> 32;
     }
 
     /* Fetching Brighness Value */
     struct get_four_zoned_kb_output out;
     status = get_kb_status(&out);
     if (ACPI_FAILURE(status)) {
         pr_err("get kb status failed!");
         return status;
     }
     output->brightness = out.gmOutput[2];
     
     return AE_OK;  
 }
 
 
 
 /*
  * Some firmwares expect the RGB per-zone setter (method id 6 under WMID_GUID4)
  * to receive an ACPI buffer consisting of 4 bytes: { zone_mask, R, G, B }.
  * Sending a packed u64 can be ignored or interpreted as zeros, turning LEDs off.
  * Use the buffer form for maximum compatibility (mirrors the working acer module).
  */
 struct ls_led_zone_set_param {
     u8 zone;
     u8 red;
     u8 green;
     u8 blue;
 } __packed;

 static acpi_status set_per_zone_color(struct per_zone_color *input) {
     acpi_status status;
     u64 zone_vals[4] = { input->zone1, input->zone2, input->zone3, input->zone4 };
     const u8 zone_ids[4] = { 0x1, 0x2, 0x4, 0x8 };

     /* Ensure keyboard is in static mode with desired brightness first */
     status = set_kb_status(0 /* static */, 0 /* speed */, input->brightness, 0 /* dir */, 0, 0, 0);
     if (ACPI_FAILURE(status)) {
         pr_err("Error setting KB status.\n");
         return -ENODEV;
     }

    /*
     * Vital Fix: The Predator Sense "Reset" function (and boot/resume) always calls
     * SetGamingLED(1) before sending per-zone colors. This "wakes up" or resets
     * the RGB controller to ensure it accepts the new color data.
     * Without this, the keyboard may become partially unresponsive or "stuck".
     */
     if (has_cap(ACER_CAP_PREDATOR_SENSE)) {
        u8 enable_cmd[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        struct acpi_buffer input_buf = { sizeof(enable_cmd), enable_cmd };
        struct acpi_buffer output_buf = { ACPI_ALLOCATE_BUFFER, NULL };

        status = wmi_evaluate_method(WMID_GUID4, 0, ACER_WMID_SET_GAMING_LED_METHODID, &input_buf, &output_buf);
        if (ACPI_FAILURE(status)) {
            pr_warn("Failed to wake up Gaming LED engine: %s\n", acpi_format_exception(status));
            /* Continue anyway, as it might just be already active or not supported on some FW */
        } else {
            kfree(output_buf.pointer);
        }
     }

     for (int i = 0; i < 4; i++) {
         u64 v = zone_vals[i] & 0xFFFFFFULL; /* RRGGBB */
        /* Method id 6 expects a u64 (8 bytes). Pad the struct to 8 bytes. */
        u64 payload = 0;
        /*
         * Construct payload: 0x00BBGGRRZZ (Little Endian in memory: ZZ RR GG BB 00 00 00 00)
         * Struct is {zone, red, green, blue} -> 4 bytes
         */
        struct ls_led_zone_set_param *p = (struct ls_led_zone_set_param *)&payload;
        p->zone = zone_ids[i];
        p->red = (u8)((v >> 16) & 0xFF);
        p->green = (u8)((v >> 8) & 0xFF);
        p->blue = (u8)(v & 0xFF);

        struct acpi_buffer in = { sizeof(u64), &payload };

        /* Method id 6 under WMID_GUID4 */
        status = wmi_evaluate_method(WMID_GUID4, 0, ACER_WMID_SET_GAMING_RGB_KB_METHODID, &in, NULL);
        if (ACPI_FAILURE(status)) {
            pr_err("Error setting KB color (zone %d): %s\n", i + 1, acpi_format_exception(status));
            return status;
        }
     }

     /* Mark state as per-zone */
     current_kb_state.per_zone = 1;

     return AE_OK;
 }
 
 static ssize_t per_zoned_rgb_kb_show(struct device *dev, struct device_attribute *attr,char *buf){
     struct per_zone_color output;
     acpi_status status;
     status = get_per_zone_color(&output);
     if(ACPI_FAILURE(status)){
         return -ENODEV;
     }
     return sprintf(buf,"%06llx,%06llx,%06llx,%06llx,%d\n",output.zone1,output.zone2,output.zone3,output.zone4,output.brightness);
 }
 
 static ssize_t per_zoned_rgb_kb_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
     int i = 0;
     size_t len;
     char *token;
     char str_buf[34];
     struct per_zone_color colors;
     char *input_ptr = str_buf;
     len = min(count, sizeof(str_buf) - 1);
     strncpy(str_buf, buf, len);
     if(str_buf[len-1] == '\n'){
         str_buf[len-1] = '\0';
     } else {
         str_buf[len] = '\0';
     }
 
     acpi_status status;
 
     /* zone1,zone2,zone3,zone4 */
     
     while ((token = strsep(&input_ptr, ",")) && i < 4) {
         if (strlen(token) != 6) {
             pr_err("Invalid rgb length: %s (%lu) (must be 3 bytes)\n", token, strlen(token));
             return -EINVAL;
         }
         if (kstrtoull(token, 16, &((u64 *)&colors)[i])) {
             pr_err("Invalid hex value: %s\n", token);
             return -EINVAL;
         }
         i++;
     }
 
     if (!token || kstrtoint(token, 10, &colors.brightness) || colors.brightness < 0 || colors.brightness > 100) {
         pr_err("Invalid brightness value.\n");
         return -EINVAL;
     }
 
     /* set per zone colors */
     status = set_per_zone_color(&colors);
     if(ACPI_FAILURE(status)){
         pr_err("Error setting RGB KB status.\n");
         return -ENODEV;
     }
     return count;
 }
 
 /* BackLight State */
 
 static int four_zone_kb_state_update(void) {
     acpi_status status;
     struct get_four_zoned_kb_output out;
 
     // Get keyboard status
     status = get_kb_status(&out);
     if (ACPI_FAILURE(status)) {
         pr_err("get kb status failed!");
         return -1;
     }
 
     current_kb_state.mode = out.gmOutput[0];
     current_kb_state.speed = out.gmOutput[1];
     current_kb_state.brightness = out.gmOutput[2];
     current_kb_state.direction = out.gmOutput[4]; 
     current_kb_state.red = out.gmOutput[5];
     current_kb_state.green = out.gmOutput[6];
     current_kb_state.blue = out.gmOutput[7];
 
     // Get per-zone color data
     status = get_per_zone_color(&current_kb_state.zones);
     if (ACPI_FAILURE(status)) {
         pr_err("get_per_zone_color failed!");
         return -1;
     }
     return 0;
 }
 
 static int four_zone_kb_state_save(void){
     struct file *file;
     ssize_t len;
     
     four_zone_kb_state_update();
 
     file = filp_open(KB_STATE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
     if(!file) {
         pr_err("kb_state_access - Error opening file\n");
         return -1;
     }
 
     len = kernel_write(file, (char *)&current_kb_state, sizeof(current_kb_state), &file->f_pos);
     if(len < 0) {
         pr_err("kb_state_access - Error writing to file: %ld\n", len);
         filp_close(file, NULL);
     }
     
     filp_close(file, NULL);
 
     if (len != sizeof(current_kb_state)) {
         pr_err("Failed to write complete state to file\n");
         return -1;
     }
 
     pr_info("kb states saved successfully\n");
     return 0;
 }
 
 
 static int four_zone_kb_state_load(void)
 {
     struct file *file;
     ssize_t len;
     acpi_status status;
 
     file = filp_open(KB_STATE_FILE, O_RDONLY, 0);
     if (!IS_ERR(file)) {
 
         len = kernel_read(file, (char *)&current_kb_state, sizeof(current_kb_state), &file->f_pos);
         filp_close(file, NULL);
 
         if (len != sizeof(current_kb_state)) {
             pr_err("Incomplete state read\n");
             return -1;
         } else {
             pr_info("KB states loaded\n");
         }
     } else {
         pr_info("KB state file not found!\n");
         return -1;
     }
 
     if(current_kb_state.per_zone){
         status = set_per_zone_color(&current_kb_state.zones);
         if(ACPI_FAILURE(status)){
             pr_err("Error setting RGB KB status.\n");
             return -1;
         }
     } else {
         status = set_kb_status(current_kb_state.mode,current_kb_state.speed,current_kb_state.brightness,current_kb_state.direction,current_kb_state.red,current_kb_state.green,current_kb_state.blue);
         if(ACPI_FAILURE(status)){
             pr_err("Error setting KB status.\n");
             return -1;
         }
     }
 
     pr_info("KB states restored successfully\n");
     return 0;
 }
 
 /* Four Zoned Keyboard Attributes */
 static struct device_attribute four_zoned_rgb_mode = __ATTR(four_zone_mode, 0644, four_zoned_rgb_kb_show, four_zoned_rgb_kb_store);
 static struct device_attribute per_zoned_rgb_mode = __ATTR(per_zone_mode, 0644, per_zoned_rgb_kb_show, per_zoned_rgb_kb_store);
 static struct attribute *four_zoned_kb_attrs[] = {
     &four_zoned_rgb_mode.attr,
     &per_zoned_rgb_mode.attr,
     NULL
 };
 
 /* Four Zoned RGB Keyboard */
 static struct attribute_group four_zoned_kb_attr_group = {
     .name = "four_zoned_kb", .attrs = four_zoned_kb_attrs
 };

/* Back logo/lightbar sysfs: expose a simple color+brightness control */
static ssize_t back_logo_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct get_four_zoned_kb_output out;
    acpi_status status = get_logo_status(&out);
    if (ACPI_FAILURE(status))
        return -ENODEV;
    /* gmOutput indices: [5]=R, [6]=G, [7]=B, [2]=brightness, [0]=enable */
    return sprintf(buf, "%02x%02x%02x,%d,%d\n",
                   out.gmOutput[5], out.gmOutput[6], out.gmOutput[7],
                   out.gmOutput[2], out.gmOutput[0]);
}

static ssize_t back_logo_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    /* Accept: RRGGBB,brightness[,enable] */
    char tmp[40];
    size_t len = min(count, sizeof(tmp) - 1);
    int brightness = -1, enable = -1;
    unsigned int r = 0, g = 0, b = 0;
    char *p, *tok;
    acpi_status status;

    strncpy(tmp, buf, len);
    if (tmp[len-1] == '\n')
        tmp[len-1] = '\0';
    else
        tmp[len] = '\0';

    p = tmp;
    tok = strsep(&p, ",");
    if (!tok || strlen(tok) != 6 ||
        sscanf(tok, "%02x%02x%02x", &r, &g, &b) != 3) {
        pr_err("Invalid color, expected RRGGBB\n");
        return -EINVAL;
    }
    tok = strsep(&p, ",");
    if (!tok || kstrtoint(tok, 10, &brightness) || brightness < 0 || brightness > 100) {
        pr_err("Invalid brightness 0-100\n");
        return -EINVAL;
    }
    if (p && *p) {
        tok = strsep(&p, ",");
        if (!tok || kstrtoint(tok, 10, &enable) || (enable != 0 && enable != 1)) {
            pr_err("Invalid enable (0/1)\n");
            return -EINVAL;
        }
    }

    if (enable < 0)
        enable = brightness > 0 ? 1 : 0;

    /* Some firmware ignores the enable flag for LB; enforce off by forcing brightness=0 */
    if (enable == 0)
        brightness = 0;

    /* effect 0 = static */
    status = set_logo_status(enable, brightness, 0, (int)r, (int)g, (int)b);
    if (ACPI_FAILURE(status))
        return -ENODEV;
    return count;
}

static struct device_attribute back_logo_attr = __ATTR(color, 0644, back_logo_show, back_logo_store);
static struct attribute *back_logo_attrs[] = {
    &back_logo_attr.attr,
    NULL
};
static const struct attribute_group back_logo_attr_group = {
    .name = "back_logo",
    .attrs = back_logo_attrs,
};
 /*
  * Platform device
  */
 
static void acer_gaming_init_lighting(void)
{
    acpi_status status;
    /* 
     * Windows WMI verification confirms proper payload size is 16 bytes.
     * 8-byte (u64) payloads are rejected with "Invalid Parameter".
     * Structure is likely u128 or specific struct with padding.
     * Value 1 starts the engine.
     */
    u8 enable_cmd[16] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    struct acpi_buffer input = { sizeof(enable_cmd), enable_cmd };
    struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };

    /*
     * The official Windows driver sends a SetGamingLED(1) command 
     * during service startup (boot) and likely on resume.
     * This appears to be required to "unbrick" or enable the RGB controller
     * if the BIOS disabled it (e.g. on AC plug event during boot).
     */
    if (has_cap(ACER_CAP_PREDATOR_SENSE)) {
        /* 
         * Try standard Method 2 (Gaming LED) with 16-byte payload 
         */
        status = wmi_evaluate_method(WMID_GUID4, 0, ACER_WMID_SET_GAMING_LED_METHODID, &input, &output);
        if (ACPI_FAILURE(status))
            pr_warn("Failed to enable Gaming LED engine (Method 2): %s\n", acpi_format_exception(status));
        else {
            pr_debug("Gaming LED engine enabled (Method 2)\n");
            kfree(output.pointer);
        }

        /* 
         * Try Method 6 (Gaming RGB KB) with 8-byte payload of '1'
         * Uncovered via WMI tracing of official driver service.
         */
        u64 magic = 1;
        struct acpi_buffer input6 = { sizeof(u64), &magic };
        struct acpi_buffer output6 = { ACPI_ALLOCATE_BUFFER, NULL };
        
        status = wmi_evaluate_method(WMID_GUID4, 0, ACER_WMID_SET_GAMING_RGB_KB_METHODID, &input6, &output6);
        if (ACPI_FAILURE(status))
             pr_warn("Failed to init Gaming RGB KB (Method 6): %s\n", acpi_format_exception(status));
        else {
             pr_info("Predator Sense: Gaming RGB KB initialized (Method 6, val=1)\n");
             kfree(output6.pointer);
        }
    }
}

static int acer_platform_probe(struct platform_device *device)
{
    int err;

    /* Initialize lighting engine to fix potential bricked state from BIOS */
    acer_gaming_init_lighting();

     if (has_cap(ACER_CAP_PLATFORM_PROFILE)) {
         err = acer_platform_profile_setup(device);
         if (err)
             return err;
     }

     if (has_cap(ACER_CAP_PREDATOR_SENSE)) {
         err = sysfs_create_group(&device->dev.kobj, &preadtor_sense_attr_group);
         if (err)
             return err;
         acer_predator_state_load();
     }

     if (quirks->four_zone_kb) {
         err = sysfs_create_group(&device->dev.kobj, &four_zoned_kb_attr_group);
         if (err)
             return err;
         four_zone_kb_state_load();
     }

     if (has_cap(ACER_CAP_BACK_LOGO)) {
         err = sysfs_create_group(&device->dev.kobj, &back_logo_attr_group);
         if (err)
             return err;
     }

     if (has_cap(ACER_CAP_FAN_SPEED_READ)) {
         err = acer_wmi_hwmon_init();
         if (err)
             return err;
     }

     return 0;
 }
 
 
 static void acer_platform_remove(struct platform_device *device)
 {
     if (has_cap(ACER_CAP_PREDATOR_SENSE)) {
         sysfs_remove_group(&device->dev.kobj, &preadtor_sense_attr_group);
         acer_predator_state_save();
     }
     if (quirks->four_zone_kb) {
         sysfs_remove_group(&device->dev.kobj, &four_zoned_kb_attr_group);
         four_zone_kb_state_save();
     }
     if (has_cap(ACER_CAP_BACK_LOGO))
         sysfs_remove_group(&device->dev.kobj, &back_logo_attr_group);
 }
 
 #ifdef CONFIG_PM_SLEEP
 static int acer_suspend(struct device *dev)
 {
     return 0;
 }
 
 static int acer_resume(struct device *dev)
 {
     /* Re-initialize lighting on resume to prevent bricked state */
     acer_gaming_init_lighting();
     return 0;
 }
 #else
 #define acer_suspend	NULL
 #define acer_resume	NULL
 #endif
 
 static SIMPLE_DEV_PM_OPS(acer_pm, acer_suspend, acer_resume);
 
 static void acer_platform_shutdown(struct platform_device *device)
 {
    (void)device;
 }
 
 static struct platform_driver acer_platform_driver = {
     .driver = {
         .name = "acer-wmi",
         .pm = &acer_pm,
     },
     .probe = acer_platform_probe,
     .remove = acer_platform_remove,
     .shutdown = acer_platform_shutdown,
 };
 
 static struct platform_device *acer_platform_device;
 
 static inline void remove_debugfs(void) {}
 
 static inline void __init create_debugfs(void) {}

 static const enum acer_wmi_predator_v4_sensor_id acer_wmi_temp_channel_to_sensor_id[] = {
    [0] = ACER_WMID_SENSOR_CPU_TEMPERATURE,
    [1] = ACER_WMID_SENSOR_GPU_TEMPERATURE,
    [2] = ACER_WMID_SENSOR_EXTERNAL_TEMPERATURE_2,
};

static const enum acer_wmi_predator_v4_sensor_id acer_wmi_fan_channel_to_sensor_id[] = {
    [0] = ACER_WMID_SENSOR_CPU_FAN_SPEED,
    [1] = ACER_WMID_SENSOR_GPU_FAN_SPEED,
};
 
 static umode_t acer_wmi_hwmon_is_visible(const void *data,
                      enum hwmon_sensor_types type, u32 attr,
                      int channel)
 {
     enum acer_wmi_predator_v4_sensor_id sensor_id;
     const u64 *supported_sensors = data;
 
     switch (type) {
     case hwmon_temp:
         sensor_id = acer_wmi_temp_channel_to_sensor_id[channel];
         break;
     case hwmon_fan:
         sensor_id = acer_wmi_fan_channel_to_sensor_id[channel];
         break;
     default:
         return 0;
     }
 
     if (*supported_sensors & BIT(sensor_id - 1))
         return 0444;
 
     return 0;
 }
 
 static int acer_wmi_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
                    u32 attr, int channel, long *val)
 {
     u64 command = ACER_WMID_CMD_GET_PREDATOR_V4_SENSOR_READING;
     u64 result;
     int ret;
 
     switch (type) {
     case hwmon_temp:
         command |= FIELD_PREP(ACER_PREDATOR_V4_SENSOR_INDEX_BIT_MASK,
                       acer_wmi_temp_channel_to_sensor_id[channel]);
 
         ret = WMID_gaming_get_sys_info(command, &result);
         if (ret < 0)
             return ret;
 
         result = FIELD_GET(ACER_PREDATOR_V4_SENSOR_READING_BIT_MASK, result);
         *val = result * MILLIDEGREE_PER_DEGREE;
         return 0;
     case hwmon_fan:
         command |= FIELD_PREP(ACER_PREDATOR_V4_SENSOR_INDEX_BIT_MASK,
                       acer_wmi_fan_channel_to_sensor_id[channel]);
 
         ret = WMID_gaming_get_sys_info(command, &result);
         if (ret < 0)
             return ret;
 
         *val = FIELD_GET(ACER_PREDATOR_V4_SENSOR_READING_BIT_MASK, result);
         return 0;
     default:
         return -EOPNOTSUPP;
     }
 } 
 
 static const struct hwmon_channel_info *const acer_wmi_hwmon_info[] = {
     HWMON_CHANNEL_INFO(temp,
                HWMON_T_INPUT,
                HWMON_T_INPUT,
                HWMON_T_INPUT
                ),
     HWMON_CHANNEL_INFO(fan,
                HWMON_F_INPUT,
                HWMON_F_INPUT
                ),
     NULL
 };
 
 static const struct hwmon_ops acer_wmi_hwmon_ops = {
     .read = acer_wmi_hwmon_read,
     .is_visible = acer_wmi_hwmon_is_visible,
 };
 
 static const struct hwmon_chip_info acer_wmi_hwmon_chip_info = {
     .ops = &acer_wmi_hwmon_ops,
     .info = acer_wmi_hwmon_info,
 };
 
 static int acer_wmi_hwmon_init(void)
 {
     struct device *dev = &acer_platform_device->dev;
     struct device *hwmon;
     u64 result;
     int ret;
 
     ret = WMID_gaming_get_sys_info(ACER_WMID_CMD_GET_PREDATOR_V4_SUPPORTED_SENSORS, &result);
     if (ret < 0)
         return ret;
 
     /* Return early if no sensors are available */
     supported_sensors = FIELD_GET(ACER_PREDATOR_V4_SUPPORTED_SENSORS_BIT_MASK, result);
     if (!supported_sensors)
         return 0;
 
     hwmon = devm_hwmon_device_register_with_info(dev, "acer",
                              &supported_sensors,
                              &acer_wmi_hwmon_chip_info,
                              NULL);
 
     if (IS_ERR(hwmon)) {
         dev_err(dev, "Could not register acer hwmon device\n");
         return PTR_ERR(hwmon);
     }
 
     return 0;
 }
 
 static int __init acer_wmi_init(void)
 {
     int err;

     pr_info("Acer Laptop ACPI-WMI Extras (PHN16-72)\n");

     find_quirks();

     /* Force modern WMID v2 interface on PHN16-72 path */
     interface = &wmid_v2_interface;
     set_quirks();

     /* Install WMI event handler directly (no input device/hotkeys) */
     if (wmi_has_guid(ACERWMID_EVENT_GUID)) {
         acpi_status st = wmi_install_notify_handler(ACERWMID_EVENT_GUID, acer_wmi_notify, NULL);
         if (ACPI_FAILURE(st)) {
             pr_err("Failed to install WMI notify handler\n");
             return -ENODEV;
         }
     }

     err = platform_driver_register(&acer_platform_driver);
     if (err) {
         pr_err("Unable to register platform driver\n");
         goto error_notifier;
     }

     acer_platform_device = platform_device_alloc("acer-wmi", PLATFORM_DEVID_NONE);
     if (!acer_platform_device) {
         err = -ENOMEM;
         goto error_driver;
     }

     err = platform_device_add(acer_platform_device);
     if (err)
         goto error_put;

     return 0;

 error_put:
     platform_device_put(acer_platform_device);
 error_driver:
     platform_driver_unregister(&acer_platform_driver);
 error_notifier:
     if (wmi_has_guid(ACERWMID_EVENT_GUID))
         wmi_remove_notify_handler(ACERWMID_EVENT_GUID);
     return err;
 }
 
 static void __exit acer_wmi_exit(void)
 {
     if (wmi_has_guid(ACERWMID_EVENT_GUID))
         wmi_remove_notify_handler(ACERWMID_EVENT_GUID);

     platform_device_unregister(acer_platform_device);
     platform_driver_unregister(&acer_platform_driver);

     pr_info("Acer Laptop WMI Extras unloaded\n");
 }
 
 module_init(acer_wmi_init);
 module_exit(acer_wmi_exit);
