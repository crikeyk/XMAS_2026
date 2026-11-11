#include "ff.h"
#include "diskio.h"
#include "flash_disk.h"

DSTATUS disk_initialize(BYTE pdrv) {
    (void)pdrv;
    return 0;
}

DSTATUS disk_status(BYTE pdrv) {
    (void)pdrv;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    (void)pdrv;
    for (UINT i = 0; i < count; i++) {
        if (!flash_disk_read((uint32_t)sector + i, 0, buff + (uint32_t)i * 512u, 512))
            return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    (void)pdrv;
    for (UINT i = 0; i < count; i++) {
        if (!flash_disk_write((uint32_t)sector + i, 0, buff + (uint32_t)i * 512u, 512))
            return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    (void)pdrv;
    switch (cmd) {
    case CTRL_SYNC:
        flash_disk_flush();
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = flash_disk_blocks();
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 8; /* 4 KB erase */
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
