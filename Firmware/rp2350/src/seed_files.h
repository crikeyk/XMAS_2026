#pragma once
#include <stdint.h>

typedef struct {
    const char *name;
    const uint8_t *data;
    unsigned len;
} SeedFile;

extern const SeedFile g_seed_files[];
extern const unsigned g_seed_count;
