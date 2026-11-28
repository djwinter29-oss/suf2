#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

#include "suf2_board_config.h"

enum { ITF_NUM_MSC, ITF_NUM_TOTAL };
enum { STR_LANGID, STR_MANUFACTURER, STR_PRODUCT, STR_SERIAL, STR_MSC };

#define EP_MSC_OUT 0x01
#define EP_MSC_IN 0x81
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = SUF2_USB_VID,
    .idProduct = SUF2_USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STR_MANUFACTURER,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, STR_MSC, EP_MSC_OUT, EP_MSC_IN, 64),
};

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return configuration;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    static uint16_t desc[33];
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    const char *str;
    size_t len;

    switch (index) {
    case STR_LANGID:
        desc[1] = 0x0409;
        len = 1;
        desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
        return desc;
    case STR_MANUFACTURER: str = SUF2_USB_MANUFACTURER; break;
    case STR_PRODUCT: str = SUF2_USB_PRODUCT; break;
    case STR_SERIAL:
        pico_get_unique_board_id_string(serial, sizeof serial);
        str = serial;
        break;
    case STR_MSC: str = "SUF2 update drive"; break;
    default: return NULL;
    }
    len = strlen(str);
    if (len > 32) len = 32;
    for (size_t i = 0; i < len; i++) desc[1 + i] = (uint8_t)str[i];
    desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return desc;
}
