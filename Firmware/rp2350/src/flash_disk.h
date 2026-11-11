#pragma once
#include <stdint.h>

int flash_disk_read(uint32_t lba, uint32_t offset, void *buf, uint32_t len);
int flash_disk_write(uint32_t lba, uint32_t offset, const void *buf, uint32_t len);
void flash_disk_flush(void);
void flash_disk_note_host_write(void);
/* 1 when the PC has written and then been quiet for ms. Flushes first. */
int flash_disk_host_idle(uint32_t ms);
uint32_t flash_disk_blocks(void);
