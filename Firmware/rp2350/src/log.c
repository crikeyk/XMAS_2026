#include "log.h"
#include "usb_app.h"

#include "tusb.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Boot lines are kept until a PC opens the serial port, then sent.
 * Once they have all gone out, the buffer is reused for later lines. */
static char boot[8192];
static int boot_len;
static int boot_sent;

void pov_log_flush(void) {
    if (!tud_mounted()) return;
    while (boot_sent < boot_len) {
        uint32_t room = tud_cdc_write_available();
        if (!room) return;
        uint32_t n = (uint32_t)(boot_len - boot_sent);
        if (n > room) n = room;
        uint32_t w = tud_cdc_write(boot + boot_sent, n);
        boot_sent += (int)w;
        if (!w) return;
        tud_cdc_write_flush();
    }
}

void pov_log(const char *fmt, ...) {
    char line[240];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof line) n = (int)sizeof line - 1;
    if (n + 2 < (int)sizeof line) {
        line[n++] = '\r';
        line[n++] = '\n';
        line[n] = 0;
    }
    if (boot_sent >= boot_len) {
        boot_sent = 0;
        boot_len = 0;
    }
    if (boot_len + n < (int)sizeof boot) {
        memcpy(boot + boot_len, line, (size_t)n);
        boot_len += n;
    }
    pov_log_flush();
}
