#pragma once

/* Pico 2 is 4 MB. The program lives at the start. The WAND drive is the
 * rest, so a later UF2 update does not wipe message.txt. 3 MB is FAT16
 * with 512-byte clusters, and leaves room for the files Windows adds. */
#define FS_FLASH_OFFSET (1024u * 1024u)
#define FS_FLASH_SIZE   (3u * 1024u * 1024u)
#define FS_BLOCK        512u
