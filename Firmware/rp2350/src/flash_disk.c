#include "flash_disk.h"

#include "disk_layout.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <string.h>

static uint8_t cache[4096] __attribute__((aligned(256)));
static uint32_t cache_block = 0xFFFFFFFFu;
static int cache_dirty;
static int host_dirty;
static uint64_t last_host_write_us;

static void __not_in_flash_func(program_block)(uint32_t offset, const uint8_t *src) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(offset, 4096);
    flash_range_program(offset, src, 4096);
    restore_interrupts(ints);
}

void flash_disk_flush(void) {
    if (!cache_dirty || cache_block == 0xFFFFFFFFu) return;
    program_block(FS_FLASH_OFFSET + cache_block, cache);
    cache_dirty = 0;
}

static int load_block(uint32_t block) {
    if (block >= FS_FLASH_SIZE) return 0;
    if (cache_block == block) return 1;
    flash_disk_flush();
    memcpy(cache, (const void *)(XIP_BASE + FS_FLASH_OFFSET + block), 4096);
    cache_block = block;
    return 1;
}

static int xfer(uint32_t lba, uint32_t offset, uint8_t *buf, uint32_t len, int write) {
    if (len == 0) return 1;
    uint64_t start = (uint64_t)lba * FS_BLOCK + offset;
    if (start + len > FS_FLASH_SIZE) return 0;
    uint32_t pos = (uint32_t)start;
    while (len) {
        uint32_t block = pos & ~4095u;
        uint32_t within = pos & 4095u;
        uint32_t n = 4096u - within;
        if (n > len) n = len;
        if (!load_block(block)) return 0;
        if (write) {
            memcpy(cache + within, buf, n);
            cache_dirty = 1;
        } else {
            memcpy(buf, cache + within, n);
        }
        pos += n;
        buf += n;
        len -= n;
    }
    return 1;
}

int flash_disk_read(uint32_t lba, uint32_t offset, void *buf, uint32_t len) {
    return xfer(lba, offset, buf, len, 0);
}

int flash_disk_write(uint32_t lba, uint32_t offset, const void *buf, uint32_t len) {
    return xfer(lba, offset, (uint8_t *)buf, len, 1);
}

void flash_disk_note_host_write(void) {
    last_host_write_us = time_us_64();
    host_dirty = 1;
}

int flash_disk_host_idle(uint32_t ms) {
    if (!host_dirty) return 0;
    if (time_us_64() - last_host_write_us < (uint64_t)ms * 1000ull) return 0;
    flash_disk_flush();
    host_dirty = 0;
    return 1;
}

uint32_t flash_disk_blocks(void) { return FS_FLASH_SIZE / FS_BLOCK; }
