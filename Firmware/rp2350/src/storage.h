#pragma once
#include <stdint.h>

#define SETTINGS_CAP 8192
#define MESSAGE_CAP 4096
#define BMP_CAP 65536

typedef struct Loaded {
    char settings[SETTINGS_CAP];
    char message[MESSAGE_CAP];
    uint8_t bmp[BMP_CAP];
    int settings_len;
    int message_len;
    int bmp_len;
    int have_settings;
    int have_message;
    int have_bmp;
} Loaded;

int storage_init(void);
int storage_load(Loaded *out);
void storage_save_log(const char *line);
