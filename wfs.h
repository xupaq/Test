/*
  S.M.A.C.K - An operating system kernel
  Copyright (C) 2010,2011,2014 Mattias Holm and Kristian Rietveld
  For licensing and a full list of authors of the kernel, see the files
  COPYING and AUTHORS.
*/

#include <stdint.h>

/* We keep a copy here and not re-use the kernel header include
 * paths to save us a lot of trouble.
 */

/* 16 bytes of magic used to identify the file system */
#define WFS_MAGIC_SIZE 16

#define WFS_MAGIC0 0x00c0ffee
#define WFS_MAGIC1 0x00000000
#define WFS_MAGIC2 0xf00d1350
#define WFS_MAGIC3 0x0000beef

#define WFS_FILENAME_SIZE 58

typedef struct
{
  char filename[WFS_FILENAME_SIZE];
  uint16_t start_block;

  /* Given that the maximum size of a file is about 8Mb, we use the top
   * 4 bits of the size field for flags.
   */
  uint32_t size;
} __attribute__((__packed__)) wfs_file_entry_t;

#define WFS_BLOCK_SIZE 512 /* Assume 512 byte block size */

#define WFS_N_FILES 64 /* 4 Kb, 8 x 512 byte block */
#define WFS_N_BLOCKS 16384

#define WFS_SIZE_MASK 0x0fffffff /* Mask to extract the size */
#define WFS_SIZE_IS_DIRECTORY (1 << 31) /* Is directory flag */

#define WFS_BLOCK_FREE 0x0
#define WFS_BLOCK_EOF 0xfffe

#define WFS_ENTRIES_START WFS_MAGIC_SIZE

#define WFS_BLOCK_TABLE_START (WFS_ENTRIES_START + (WFS_N_FILES * sizeof(wfs_file_entry_t)))
#define WFS_BLOCK_TABLE_SIZE (WFS_N_BLOCKS * sizeof(uint16_t))

#define WFS_DATA_START (WFS_BLOCK_TABLE_START + WFS_BLOCK_TABLE_SIZE)


/*
 * Assorted utility functions
 */

static inline size_t
wfs_get_size(void)
{
  return WFS_MAGIC_SIZE
      + sizeof(wfs_file_entry_t) * WFS_N_FILES
      + WFS_N_BLOCKS * sizeof(uint16_t)
      + WFS_N_BLOCKS * WFS_BLOCK_SIZE;
}

static inline bool
wfs_file_entry_is_empty(const wfs_file_entry_t *entry)
{
  if (entry->filename[0] == 0 || entry->start_block == WFS_BLOCK_FREE)
    return true;

  return false;
}

static inline bool
wfs_file_entry_is_directory(const wfs_file_entry_t *entry)
{
  if ((entry->size & WFS_SIZE_IS_DIRECTORY) == WFS_SIZE_IS_DIRECTORY)
    return true;

  return false;
}

static inline uint32_t
wfs_file_entry_get_size(const wfs_file_entry_t *entry)
{
  return entry->size & WFS_SIZE_MASK;
}

static inline uint32_t
wfs_get_block_offset(int block)
{
  return WFS_DATA_START + block * WFS_BLOCK_SIZE;
}
