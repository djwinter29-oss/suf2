/*
 * SUF2 loader for RP2040 and RP2350.
 *
 * Boot: finish an interrupted install if the journal says so, otherwise
 * boot-verify the installed application and jump to it. Anything else
 * (no application, failed verification, or an update request from the
 * application) starts update mode: a USB drive that accepts .suf2 files.
 */
#include <stdio.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "boot_jump.h"
#include "flash_store.h"
#include "loader.h"
#include "suf2/suf2.h"
#include "suf2_app.h"
#include "suf2_board_config.h"
#include "suf2_keys.h"
#include "suf2_aes_keys.h"

#define APP_BASE (XIP_BASE + SUF2_APP_OFFSET)
#define FAILURE_QUIET_MS 1000u
#define REENUMERATE_MS 250u

#define MAX_BLOCKS (SUF2_SLOT_SIZE / SUF2_CHUNK_SIZE + 1u)

typedef enum {
    PHASE_WAITING,
    PHASE_RECEIVING,
    PHASE_INSTALLING,
    PHASE_FAILED,
} phase_t;

static const suf2_policy_t policy = {
    .product_id = SUF2_PRODUCT_ID_BYTES,
    .execution_base = APP_BASE,
    .max_image_size = SUF2_SLOT_SIZE,
    .require_encryption = false,
    .keys = suf2_trusted_keys,
    .key_count = SUF2_TRUSTED_KEY_COUNT,
    .decryption_keys = suf2_decryption_keys,
    .decryption_key_count = SUF2_DECRYPTION_KEY_COUNT,
};

static suf2_session_t session;
static uint8_t bitmap[(MAX_BLOCKS + 7u) / 8u];
static phase_t phase = PHASE_WAITING;
static suf2_status_t last_error = SUF2_OK;
static const char *last_error_where = "";
static uint32_t last_write_ms;
static uint8_t installed_id[SUF2_ID_SIZE];
static meta_state_t boot_meta = META_EMPTY;
static const char *entry_reason = "";

static uint32_t now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

/* Records the most recent error; it stays on STATUS.TXT until the next one. */
static void note_error(const char *where, suf2_status_t st)
{
    last_error = st;
    last_error_where = where;
}

static void fail(const char *where, suf2_status_t st)
{
    note_error(where, st);
    phase = PHASE_FAILED;
}

static void session_restart(void)
{
    suf2_session_reset(&session);
    flash_store_session_reset();
    phase = PHASE_WAITING;
}

static bool take_update_request(void)
{
    bool requested = watchdog_hw->scratch[0] == SUF2_UPDATE_REQUEST_MAGIC &&
                     watchdog_hw->scratch[1] == ~SUF2_UPDATE_REQUEST_MAGIC;
    watchdog_hw->scratch[0] = 0;
    watchdog_hw->scratch[1] = 0;
    return requested;
}

static void __attribute__((noreturn)) reboot(void)
{
    watchdog_reboot(0, 0, 0);
    for (;;) tight_loop_contents();
}

static bool try_boot_app(const uint8_t manifest[SUF2_MANIFEST_SIZE])
{
    suf2_reader_t app = flash_store_app_reader();
    suf2_status_t st = suf2_verify_boot(manifest, &policy, &app);
    if (st != SUF2_OK) {
        note_error("boot check", st);
        return false;
    }
    uint32_t vtor = APP_BASE + SUF2_APP_VECTOR_OFFSET;
    if (!boot_vector_table_ok(vtor, APP_BASE, APP_BASE + SUF2_SLOT_SIZE)) {
        note_error("boot check", SUF2_E_ARGUMENT);
        return false;
    }
    boot_jump(vtor);
}

void loader_on_sector(const uint8_t sector[512])
{
    suf2_status_t st = SUF2_OK;
    suf2_sector_result_t r = suf2_session_write(&session, sector, &st);
    if (r == SUF2_SECTOR_IGNORED) return;
    last_write_ms = now_ms();
    if (r == SUF2_SECTOR_STAGED && phase == PHASE_WAITING) phase = PHASE_RECEIVING;
    if (session.state == SUF2_STATE_FAILED && phase != PHASE_FAILED) fail("receive", session.error);
}

void loader_on_unmount(void)
{
    if (phase != PHASE_INSTALLING) session_restart();
}

static void hex(char *out, const uint8_t *in, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 15];
    }
    out[2 * n] = 0;
}

size_t loader_info_text(char *buf, size_t size)
{
    char id[2 * SUF2_ID_SIZE + 1];
    int n = snprintf(buf, size,
                     "SUF2 loader %s for %s\r\n"
                     "Product ID: ",
                     SUF2_LOADER_VERSION, SUF2_BOARD_NAME);
    hex(id, policy.product_id, SUF2_ID_SIZE);
    n += snprintf(buf + n, size - n,
                  "%s\r\n"
                  "Application base: 0x%08lx\r\n"
                  "Largest image: %lu bytes\r\n"
                  "Encryption: not supported, signed packages only\r\n"
                  "Trusted signing key IDs:\r\n",
                  id, (unsigned long)policy.execution_base, (unsigned long)policy.max_image_size);
    for (size_t i = 0; i < policy.key_count && (size_t)n < size; i++) {
        uint8_t kid[SUF2_ID_SIZE];
        if (suf2_key_id(policy.keys[i].public_key, kid) != SUF2_OK) continue;
        hex(id, kid, SUF2_ID_SIZE);
        n += snprintf(buf + n, size - n, "  %s\r\n", id);
    }
#if !SUF2_PRODUCTION
    if ((size_t)n < size) n += snprintf(buf + n, size - n, "DEVELOPMENT BUILD: not for production devices\r\n");
#endif
    return (size_t)n < size ? (size_t)n : size - 1;
}

size_t loader_status_text(char *buf, size_t size)
{
    char id[2 * SUF2_ID_SIZE + 1];
    int n;
    switch (phase) {
    case PHASE_WAITING:
        n = snprintf(buf, size, "Ready: copy a .suf2 file to this drive\r\n");
        break;
    case PHASE_RECEIVING:
        n = snprintf(buf, size, "Receiving: %lu of %lu blocks\r\n", (unsigned long)session.received,
                     (unsigned long)session.num_blocks);
        break;
    case PHASE_INSTALLING:
        n = snprintf(buf, size, "Installing\r\n");
        break;
    default:
        n = snprintf(buf, size, "Failed: copy the file again after the drive reappears\r\n");
        break;
    }
    if ((size_t)n < size && last_error != SUF2_OK)
        n += snprintf(buf + n, size - n, "Last error: %s during %s\r\n", suf2_status_name(last_error),
                      last_error_where);
    if ((size_t)n < size) n += snprintf(buf + n, size - n, "Update mode reason: %s\r\n", entry_reason);
    if ((size_t)n < size) {
        if (boot_meta == META_COMMITTED) {
            hex(id, installed_id, SUF2_ID_SIZE);
            n += snprintf(buf + n, size - n, "Installed package: %s\r\n", id);
        } else {
            n += snprintf(buf + n, size - n, "Installed package: none\r\n");
        }
    }
    return (size_t)n < size ? (size_t)n : size - 1;
}

static void led_init(void)
{
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif
}

static void led_update(uint32_t t)
{
#ifdef PICO_DEFAULT_LED_PIN
    bool on;
    switch (phase) {
    case PHASE_RECEIVING: on = (t / 64u) & 1u; break;
    case PHASE_FAILED: on = (t / 128u) % 8u < 2u; break; /* short double flash */
    default: on = (t / 512u) & 1u; break;
    }
    gpio_put(PICO_DEFAULT_LED_PIN, on);
#else
    (void)t;
#endif
}

/* After a failure, wait for the host to go quiet, then present a fresh drive. */
static void reenumerate(void)
{
    tud_disconnect();
    uint32_t until = now_ms() + REENUMERATE_MS;
    while ((int32_t)(now_ms() - until) < 0) tud_task();
    session_restart();
    tud_connect();
}

static void update_mode(void)
{
    static const suf2_session_config_t cfg = {
        .staging = &flash_staging,
        .staging_capacity = SUF2_SLOT_SIZE,
        .bitmap = bitmap,
        .bitmap_size = sizeof bitmap,
    };
    suf2_status_t st = suf2_session_init(&session, &cfg);
    if (st == SUF2_OK) st = msc_disk_init();
    if (st != SUF2_OK) {
        note_error("startup", st);
        for (;;) tight_loop_contents(); /* configuration bug: nothing safe to do */
    }
    flash_store_session_reset();
    led_init();
    tusb_init();

    for (;;) {
        tud_task();
        uint32_t t = now_ms();
        led_update(t);

        if (session.state == SUF2_STATE_COMPLETE && phase != PHASE_FAILED) {
            tud_task(); /* let the host see the last write complete */
            st = suf2_verify_candidate(&session, &policy);
            if (st != SUF2_OK) {
                fail("verification", st);
                continue;
            }
            phase = PHASE_INSTALLING;
            st = flash_store_install(session.manifest, &policy);
            if (st == SUF2_OK) reboot();
            fail("install", st);
            last_write_ms = now_ms();
        }

        /* A failed session keeps dropping blocks until it is reset. */
        if (phase == PHASE_FAILED && t - last_write_ms > FAILURE_QUIET_MS) {
            reenumerate();
        }
    }
}

int main(void)
{
    uint8_t manifest[SUF2_MANIFEST_SIZE];
    bool requested = take_update_request();

    suf2_status_t startup = flash_store_init();
    if (startup == SUF2_OK) startup = flash_store_read_meta(manifest, &boot_meta);
    if (startup != SUF2_OK) {
        note_error("storage startup", startup);
        if (startup == SUF2_E_ARGUMENT) {
            for (;;) tight_loop_contents();
        }
        entry_reason = "installed metadata could not be read";
        update_mode();
        return 0;
    }
    if (boot_meta == META_PENDING) {
        /* Power was lost mid-install. Finish it if staging still verifies. */
        suf2_status_t st = flash_store_install(manifest, &policy);
        if (st == SUF2_OK) reboot();
        note_error("resume install", st);
        entry_reason = "an interrupted install could not be finished";
    } else if (boot_meta == META_COMMITTED) {
        suf2_package_id(manifest, installed_id);
        if (requested) {
            entry_reason = "requested by the application";
        } else if (!try_boot_app(manifest)) {
            entry_reason = "the installed application failed verification";
        }
    } else {
        entry_reason = "no application installed";
    }

    update_mode();
    return 0;
}
