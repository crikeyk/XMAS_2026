#include "usb_app.h"
#include "flash_disk.h"

#include "bsp/board_api.h"
#include "pico/unique_id.h"
#include "tusb.h"

#include <string.h>

volatile int g_msc_busy;

#define USB_VID 0x2E8A
#define USB_PID 0x0191

tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0109,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

enum { ITF_CDC = 0, ITF_CDC_DATA, ITF_MSC, ITF_TOTAL };

#define EP_CDC_NOTIF 0x81
#define EP_CDC_OUT   0x02
#define EP_CDC_IN    0x82
#define EP_MSC_OUT   0x03
#define EP_MSC_IN    0x83

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)

uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CONFIG_TOTAL_LEN, 0x80, 100),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 4, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
    TUD_MSC_DESCRIPTOR(ITF_MSC, 5, EP_MSC_OUT, EP_MSC_IN, 64),
};

#if TUD_OPT_HIGH_SPEED
uint8_t const desc_hs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CONFIG_TOTAL_LEN, 0x80, 100),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 4, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 512),
    TUD_MSC_DESCRIPTOR(ITF_MSC, 5, EP_MSC_OUT, EP_MSC_IN, 512),
};
uint8_t desc_other_speed_config[CONFIG_TOTAL_LEN];
tusb_desc_device_qualifier_t const desc_device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 1,
};
uint8_t const *tud_descriptor_device_qualifier_cb(void) {
    return (uint8_t const *)&desc_device_qualifier;
}
uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index) {
    (void)index;
    memcpy(desc_other_speed_config,
           (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_fs_configuration : desc_hs_configuration,
           CONFIG_TOTAL_LEN);
    desc_other_speed_config[1] = TUSB_DESC_OTHER_SPEED_CONFIG;
    return desc_other_speed_config;
}
#endif

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
#if TUD_OPT_HIGH_SPEED
    return (tud_speed_get() == TUSB_SPEED_HIGH) ? desc_hs_configuration : desc_fs_configuration;
#else
    return desc_fs_configuration;
#endif
}

enum { STR_LANG = 0, STR_MANUF, STR_PRODUCT, STR_SERIAL, STR_CDC, STR_MSC };

static char const *const string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "PoV Wand",
    "PoV Wand",
    NULL,
    "PoV serial",
    "WAND",
};

static uint16_t desc_str[33];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;
    if (index == STR_LANG) {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else if (index == STR_SERIAL) {
        char id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
        pico_get_unique_board_id_string(id, sizeof id);
        chr_count = strlen(id);
        if (chr_count > 32) chr_count = 32;
        for (size_t i = 0; i < chr_count; i++) desc_str[1 + i] = id[i];
    } else {
        if (index >= sizeof string_desc_arr / sizeof string_desc_arr[0]) return NULL;
        const char *s = string_desc_arr[index];
        if (!s) return NULL;
        chr_count = strlen(s);
        if (chr_count > 32) chr_count = 32;
        for (size_t i = 0; i < chr_count; i++) desc_str[1 + i] = s[i];
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    memset(vendor_id, ' ', 8);
    memset(product_id, ' ', 16);
    memset(product_rev, ' ', 4);
    memcpy(vendor_id, "PoV", 3);
    memcpy(product_id, "WAND", 4);
    memcpy(product_rev, "1.0", 3);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (g_msc_busy) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x04, 0x00);
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = flash_disk_blocks();
    *block_size = 512;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)lun;
    (void)power_condition;
    (void)start;
    (void)load_eject;
    return true;
}

bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    (void)lun;
    if (!flash_disk_read(lba, offset, buffer, bufsize)) return -1;
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    (void)lun;
    if (g_msc_busy) return -1;
    if (!flash_disk_write(lba, offset, buffer, bufsize)) return -1;
    flash_disk_note_host_write();
    return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
    (void)buffer;
    (void)bufsize;
    (void)scsi_cmd;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}

void usb_init(void) {
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);
}

void usb_task(void) {
    tud_task();
    if (tud_cdc_available()) {
        uint8_t dump[64];
        tud_cdc_read(dump, sizeof dump);
    }
}

int usb_host_connected(void) {
    /* This USB port has no VBUS pin. Unplug is reported as SUSPEND, and
     * tud_mounted() stays true, so the wand would never power down. */
    return (tud_mounted() && !tud_suspended()) ? 1 : 0;
}

void usb_disconnect(void) { tud_disconnect(); }
