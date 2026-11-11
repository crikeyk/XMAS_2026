#include "storage.h"

#include "disk_layout.h"
#include "flash_disk.h"
#include "log.h"
#include "seed_files.h"
#include "usb_app.h"

#include "ff.h"
#include "hardware/flash.h"

#include <stdio.h>
#include <string.h>

extern char __flash_binary_end;

static FATFS fs;
static uint8_t work[16 * 1024];

static int mount_fs(void) {
    flash_disk_flush();
    return f_mount(&fs, "0:", 1) == FR_OK;
}

static void unmount_fs(void) {
    f_mount(0, "0:", 0);
    flash_disk_flush();
}

static int file_exists(const char *name) {
    char path[40];
    snprintf(path, sizeof path, "0:%s", name);
    FILINFO info;
    return f_stat(path, &info) == FR_OK;
}

static int write_all(const char *name, const void *data, unsigned len) {
    char path[40];
    snprintf(path, sizeof path, "0:%s", name);
    FIL fil;
    if (f_open(&fil, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return 0;
    UINT put = 0;
    FRESULT fr = f_write(&fil, data, len, &put);
    f_close(&fil);
    return fr == FR_OK && put == len;
}

static int read_all(const char *name, void *buf, int cap) {
    char path[40];
    snprintf(path, sizeof path, "0:%s", name);
    FIL fil;
    if (f_open(&fil, path, FA_READ) != FR_OK) return -1;
    int total = 0;
    uint8_t *p = buf;
    while (total < cap) {
        UINT n = 0;
        if (f_read(&fil, p + total, (UINT)(cap - total), &n) != FR_OK) {
            f_close(&fil);
            return -1;
        }
        if (!n) break;
        total += (int)n;
    }
    f_close(&fil);
    return total;
}

static void seed_files(void) {
    for (unsigned i = 0; i < g_seed_count; i++)
        write_all(g_seed_files[i].name, g_seed_files[i].data, g_seed_files[i].len);
    f_setlabel("0:WAND");
}

static int our_files_present(void) {
    return file_exists("settings.txt") || file_exists("message.txt") || file_exists("image.bmp");
}

/* FatFs writes the type string as "FAT     " for both FAT12 and FAT16, so the
 * name is the keeper. The old CircuitPython volume was not called WAND. */
static int drive_ready(void) {
    char label[16];
    memset(label, 0, sizeof label);
    if (f_getlabel("0:", label, 0) != FR_OK) return 0;
    return strcmp(label, "WAND") == 0;
}

int storage_init(void) {
    uint32_t used = (uint32_t)&__flash_binary_end - XIP_BASE;
    if (used > FS_FLASH_OFFSET) {
        pov_log("POV> PROBLEM: program is %lu bytes and overlaps the drive", (unsigned long)used);
        return 0;
    }
    g_msc_busy = 1;
    int ok = mount_fs();
    if (ok && (!our_files_present() || !drive_ready())) {
        unmount_fs();
        ok = 0;
    }
    if (!ok) {
        pov_log("POV> preparing the WAND drive");
        MKFS_PARM opt;
        memset(&opt, 0, sizeof opt);
        opt.fmt = FM_FAT | FM_SFD;
        opt.n_fat = 2;
        opt.align = 8;
        opt.n_root = 512;
        opt.au_size = 512;
        FRESULT fr = f_mkfs("0:", &opt, work, sizeof work);
        if (fr != FR_OK || !mount_fs()) {
            pov_log("POV> PROBLEM: could not create the drive (%d)", (int)fr);
            g_msc_busy = 0;
            return 0;
        }
        seed_files();
    } else if (!file_exists("blank_template.bmp")) {
        for (unsigned i = 0; i < g_seed_count; i++) {
            if (!strcmp(g_seed_files[i].name, "blank_template.bmp"))
                write_all(g_seed_files[i].name, g_seed_files[i].data, g_seed_files[i].len);
        }
    }
    uint8_t kind[8];
    memset(kind, 0, sizeof kind);
    flash_disk_flush();
    flash_disk_read(0, 0x36, kind, 8);
    pov_log("POV> drive %lu KB, %.8s", (unsigned long)(FS_FLASH_SIZE / 1024), kind);
    unmount_fs();
    g_msc_busy = 0;
    return 1;
}

int storage_load(Loaded *out) {
    memset(out, 0, sizeof *out);
    g_msc_busy = 1;
    if (!mount_fs()) {
        g_msc_busy = 0;
        return 0;
    }
    int n = read_all("settings.txt", out->settings, SETTINGS_CAP - 1);
    if (n >= 0) {
        out->settings[n] = 0;
        out->settings_len = n;
        out->have_settings = 1;
    }
    n = read_all("message.txt", out->message, MESSAGE_CAP - 1);
    if (n >= 0) {
        out->message[n] = 0;
        out->message_len = n;
        out->have_message = 1;
    }
    n = read_all("image.bmp", out->bmp, BMP_CAP);
    if (n > 0) {
        out->bmp_len = n;
        out->have_bmp = 1;
        if (n == BMP_CAP) pov_log("POV> PROBLEM: image.bmp is too big; the extra is ignored");
    }
    unmount_fs();
    g_msc_busy = 0;
    return 1;
}

void storage_save_log(const char *line) {
    if (!line || !line[0]) return;
    if (usb_host_connected()) return;
    g_msc_busy = 1;
    if (!mount_fs()) {
        g_msc_busy = 0;
        return;
    }
    char old[3072];
    int n = read_all("log.txt", old, (int)sizeof old - 1);
    if (n < 0) n = 0;
    old[n] = 0;
    char *lines[32];
    int count = 0;
    char *p = old;
    while (*p && count < 31) {
        lines[count++] = p;
        char *nl = strchr(p, '\n');
        if (!nl) break;
        *nl = 0;
        p = nl + 1;
    }
    char fresh[4096];
    int used = 0;
    int start = count > 29 ? count - 29 : 0;
    for (int i = start; i < count; i++) {
        if (!lines[i][0]) continue;
        int k = snprintf(fresh + used, sizeof fresh - (size_t)used, "%s\n", lines[i]);
        if (k < 0 || used + k >= (int)sizeof fresh) break;
        used += k;
    }
    int k = snprintf(fresh + used, sizeof fresh - (size_t)used, "%s\n", line);
    if (k > 0 && used + k < (int)sizeof fresh) used += k;
    write_all("log.txt", fresh, (unsigned)used);
    unmount_fs();
    g_msc_busy = 0;
}
