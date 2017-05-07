/* wfsfuse -- Starting point of WFS FUSE assignment.
 *
 * Copyright (C) 2017  Leiden University, The Netherlands.
 *
 * Based on code from:
 *
 * S.M.A.C.K - An operating system kernel
 * Copyright (C) 2010,2011,2013 Mattias Holm and Kristian Rietveld
 * For licensing and a full list of authors of the kernel, see the files
 * COPYING and AUTHORS.
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include <stdbool.h>

#include "wfs.h"


/*
 * WFS image management
 */

typedef struct
{
  int fd;
  const char *filename;
} wfs_image_t;


static void
wfs_image_close(wfs_image_t *img)
{
  if (!img)
    return;

  if (img->fd >= 0)
    close(img->fd);

  free(img);
}

/* Verifies whether an opened image is really an WFS file system image */
static int
wfs_check_image(wfs_image_t *img)
{
  struct stat buf;
  uint32_t magic[WFS_MAGIC_SIZE / sizeof(uint32_t)];

  if (fstat(img->fd, &buf) < 0)
    {
      fprintf(stderr, "error: file '%s': %s\n",
              img->filename, strerror(errno));
      return -1;
    }

  /* We can't check the size of devices, otherwise check the
   * size of the image file.
   */
  if (!S_ISBLK(buf.st_mode))
    {
      if (buf.st_size < wfs_get_size())
        {
          fprintf(stderr,
                  "error: file '%s' too small to contain WFS file system\n",
                  img->filename);
          return -1;
        }
    }

  pread(img->fd, &magic, sizeof(magic), 0);

  if (magic[0] != WFS_MAGIC0 || magic[1] != WFS_MAGIC1
      || magic[2] != WFS_MAGIC2 || magic[3] != WFS_MAGIC3)
    {
      fprintf(stderr, "error: image '%s' has incorrect magic number\n",
              img->filename);
      return -1;
    }

  return 0;
}

static wfs_image_t *
wfs_image_open(const char *filename)
{
  wfs_image_t *img = malloc(sizeof(wfs_image_t));

  img->filename = filename;
  img->fd = open(img->filename, O_RDWR);
  if (img->fd < 0)
    {
      fprintf(stderr, "error: could not open file '%s': %s\n",
              img->filename, strerror(errno));
      wfs_image_close(img);
      return NULL;
    }

  if (wfs_check_image(img) < 0)
    {
      wfs_image_close(img);
      return NULL;
    }

  return img;
}

static inline wfs_image_t *
get_wfs_image(void)
{
  return (wfs_image_t *)fuse_get_context()->private_data;
}

/*
 * Low-level file system routines
 */

static inline uint16_t
wfs_block_table_read(wfs_image_t *image, uint16_t idx)
{
  uint16_t value = 0;
  uint32_t offset = WFS_BLOCK_TABLE_START + (idx * sizeof(uint16_t));

  pread(image->fd, &value, sizeof(uint16_t), offset);

  return value;
}

static uint16_t
wfs_get_block(wfs_image_t *image, uint16_t current_block)
{
  return wfs_block_table_read(image, current_block - 1);
}

/* Given an offset within a file, return the block number in which
 * this offset is located and the corresponding position within that
 * block (block_position).
 */
static uint16_t
wfs_get_current_block(wfs_image_t *image, wfs_file_entry_t *entry,
                      off_t off, uint16_t *block_position)
{
  off_t o = 0;
  uint16_t block = entry->start_block;

  if (block == WFS_BLOCK_EOF)
    return block;

  while (off >= o + WFS_BLOCK_SIZE)
    {
      block = wfs_block_table_read(image, block - 1);
      if (block == WFS_BLOCK_EOF)
        return block;

      o += WFS_BLOCK_SIZE;
    }

  if (block_position)
    *block_position = off - o;

  return block;
}

/* Read the block chain to determine the disk block that follows the
 * current block.
 */
static uint16_t
wfs_get_next_block(wfs_image_t *image, uint16_t current_block)
{
  uint16_t block = 0;

  if (current_block == WFS_BLOCK_EOF)
    return current_block;

  if (current_block - 1 >= WFS_N_BLOCKS)
    return WFS_BLOCK_EOF;

  block = wfs_block_table_read(image, current_block - 1);

  return block;
}


/*
 * Generic file entry operations
 */

typedef enum
{
  WFS_FILE_ENTRY_OP_FIND,
  WFS_FILE_ENTRY_OP_COUNT,
  WFS_FILE_ENTRY_OP_CALLBACK,
  WFS_FILE_ENTRY_OP_MKDIR,
  WFS_FILE_ENTRY_OP_RMDIR
} wfs_file_entry_op_t;

typedef void (* wfs_file_entry_op_callback_t) (wfs_file_entry_t *entry,
                                               void             *data);

/* Performs the specified file entry operation within the directory
 * specified by "parent". "parent" must be a directory. If "parent" is
 * the empty entry, the root directory is used. The use of the "entry"
 * argument depends on the selected file entry operation.
 */
static int
wfs_file_entry_operation(wfs_file_entry_t             *parent,
                         wfs_file_entry_op_t           op,
                         wfs_file_entry_t             *entry,
                         wfs_file_entry_op_callback_t  callback,
                         void                         *callback_data)
{
  int count = 0;
  int aantalfiles = 16, entrystart = WFS_ENTRIES_START;
  wfs_image_t *image = get_wfs_image();
  if (!parent)
    return -EINVAL;

  /* Can only read the root directory, so parent must be the empty entry */
  if (!wfs_file_entry_is_empty(parent)){
  	entrystart = wfs_get_block_offset(parent->start_block -1);
	}
	
  /* Refuse to perform find operation if the filename is empty. */
  if (op == WFS_FILE_ENTRY_OP_FIND && entry->filename[0] == 0)
    return -EINVAL;
	
	//printf("files: %d , entries: %d\n", aantalfiles, entrystart);
	
  for (int i = 0; i < aantalfiles; i++)
    {
      wfs_file_entry_t tmp_entry;
      pread(image->fd, &tmp_entry, sizeof(wfs_file_entry_t),
            entrystart + (i * sizeof(wfs_file_entry_t)));

      switch (op)
        {
          case WFS_FILE_ENTRY_OP_FIND:
            {
              if (wfs_file_entry_is_empty(&tmp_entry))
                continue;

              if (!strncmp(tmp_entry.filename, entry->filename,
                           WFS_FILENAME_SIZE))
                {
                  *entry = tmp_entry;
                  return 1;
                }
            }
          break;

          case WFS_FILE_ENTRY_OP_COUNT:
            if (!wfs_file_entry_is_empty(&tmp_entry))
              count++;
            break;

          case WFS_FILE_ENTRY_OP_CALLBACK:
            if (!wfs_file_entry_is_empty(&tmp_entry))
              (* callback) (&tmp_entry, callback_data);
            break;
            
          case WFS_FILE_ENTRY_OP_MKDIR:
          	
        }
    }

  return count;
}

/* Searches the file system hierarchy to find the file entry for
 * the given path. Returns true if the operation succeeded.
 */
static bool
wfs_find_entry(const char *path, wfs_file_entry_t *entry)
{
  if (strlen(path) == 0 || path[0] != '/')
    return false;

  wfs_file_entry_t current_entry = { { 0, }, };
  while (path && (path = strchr(path, '/')))
    {
      wfs_file_entry_t parent_entry = current_entry;

      /* Ignore path separator */
      while (*path == '/')
        path++;

      /* Find end of new component */
      char *end = strchr(path, '/');
      if (!end)
        {
          int len = strnlen(path, PATH_MAX);
          if (len > 0)
            end = (char *)&path[len];
          else
            {
              /* We are done: return current entry. */
              *entry = current_entry;
              return true;
            }
        }

      /* Verify length of component is not larger than maximum allowed
       * filename size.
       */
      int len = end-path+1;
      if (len >= WFS_FILENAME_SIZE - 1)
        return false;

      memset(&current_entry, 0, sizeof(wfs_file_entry_t));
      strncpy(current_entry.filename, path, len);
      current_entry.filename[len-1] = 0;

      /* Find entry for this filename in parent_entry */
      if (current_entry.filename[0] != 0)
        {
          if (wfs_file_entry_operation(&parent_entry,
                                       WFS_FILE_ENTRY_OP_FIND,
                                       &current_entry, NULL, NULL) <= 0)
            return false;
        }

      path = end;
    }

  *entry = current_entry;

  return true;
}

static inline void
drop_trailing_slashes(char *path_copy)
{
  int len = strlen(path_copy);
  while (len > 0 && path_copy[len-1] == '/')
    {
      path_copy[len-1] = 0;
      len--;
    }
}

/* Return the parent entry, for the containing directory of the file or
 * directory specified in path. Returns 0 on success, error code otherwise.
 */
static int
wfs_get_parent_entry(const char *path, wfs_file_entry_t *parent_entry)
{
  int res;
  char *path_copy = strdup(path);

  drop_trailing_slashes(path_copy);

  if (strlen(path_copy) == 0)
    {
      res = -EINVAL;
      goto out;
    }

  /* Extract parent component */
  char *sep = strrchr(path_copy, '/');
  if (!sep)
    {
      res = -EINVAL;
      goto out;
    }

  if (path_copy == sep)
    {
      /* The parent is the root directory, return an empty entry. */
      memset(parent_entry, 0, sizeof(wfs_file_entry_t));
      res = 0;
      goto out;
    }

  *sep = 0;
  char *dirname = path_copy;

  if (!wfs_find_entry(dirname, parent_entry))
    {
      res = -ENOENT;
      goto out;
    }

  /* Note that the entry may be empty in case the root directory was found. */
  if (!wfs_file_entry_is_empty(parent_entry)
      && !wfs_file_entry_is_directory(parent_entry))
    {
      /* This is really not supposed to happen. */
      res = -EIO;
      goto out;
    }

  res = 0;

out:
  free(path_copy);

  return res;
}

/* Separates the basename (the actual name of the file) from the path.
 * The return value must be freed.
 */
static char *
wfs_get_basename(const char *path)
{
  char *res = NULL;
  char *path_copy = strdup(path);

  drop_trailing_slashes(path_copy);

  if (strlen(path_copy) == 0)
    {
      res = NULL;
      goto out;
    }

  /* Find beginning of basename. */
  char *sep = strrchr(path_copy, '/');
  if (!sep)
    {
      res = NULL;
      goto out;
    }

  res = strdup(sep + 1);

out:
  free(path_copy);

  return res;
}

/*
 * Implementation of necessary FUSE operations.
 */

struct wfs_readdir_data
{
  void *buf;
  fuse_fill_dir_t filler;
};

static void
wfs_readdir_callback(wfs_file_entry_t *entry,
                     void             *data)
{
  struct wfs_readdir_data *readdir_data = (struct wfs_readdir_data *)data;

  readdir_data->filler(readdir_data->buf, entry->filename, NULL, 0);
}

static int
wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
            off_t offset, struct fuse_file_info *fi)
{
  struct wfs_readdir_data data = { buf, filler };
  wfs_file_entry_t entry = { { 0, }, };

  if (!wfs_find_entry(path, &entry))
    return -ENOENT;

  /* Note that an empty entry represents the root directory. */
  if (!wfs_file_entry_is_empty(&entry)
      && !wfs_file_entry_is_directory(&entry))
    return -ENOTDIR;

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
  wfs_file_entry_operation(&entry, WFS_FILE_ENTRY_OP_CALLBACK, NULL,
                           wfs_readdir_callback, &data);

  return 0;
}

static int
wfs_mkdir(const char *path, mode_t mode)
{
	char * name = NULL;
	wfs_file_entry_t * parent = (wfs_file_entry_t *) malloc(sizeof(wfs_file_entry_t));
	name = wfs_get_basename(path);
	int lengte = strlen(path) - strlen(name) - 1;
	char path2[lengte];
	strncpy(path2, path, lengte);
	wfs_get_parent_entry(path, parent);
  wfs_file_entry_t entry = { { 0, }, };
	wfs_file_entry_operation(parent, WFS_FILE_ENTRY_OP_MKDIR,)
	
  return -ENOSYS;
}

static int
wfs_rmdir(const char *path)
{
  return -ENOSYS;
}

static int
wfs_getattr(const char *path, struct stat *stbuf)
{
  int res = 0;

  memset(stbuf, 0, sizeof(struct stat));
  if (strcmp(path, "/") == 0)
    {
      stbuf->st_mode = S_IFDIR | 0755;
      stbuf->st_nlink = 2;
      return res;
    }

  wfs_file_entry_t entry;
  if (!wfs_find_entry(path, &entry))
    res = -ENOENT;
  else
    {
      if (wfs_file_entry_is_directory(&entry))
        {
          stbuf->st_mode = S_IFDIR | 0444;
          stbuf->st_nlink = 2;
        }
      else
        {
          stbuf->st_mode = S_IFREG | 0444;
          stbuf->st_nlink = 1;
        }
      stbuf->st_size = wfs_file_entry_get_size(&entry);
    }

  return res;
}

static int
wfs_open(const char *path, struct fuse_file_info *fi)
{
  wfs_file_entry_t entry;
  if (!wfs_find_entry(path, &entry))
    return -ENOENT;

  if (wfs_file_entry_is_directory(&entry))
    return -EISDIR;

  return 0;
}

/* To keep things simple, we will not support creation of new files, only
 * modification of existing files.
 */
static int
wfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
  return -ENOSYS;
}

static int
wfs_read(const char *path, char *buf, size_t size, off_t offset,
         struct fuse_file_info *fi)
{
  wfs_file_entry_t entry;
  if (!wfs_find_entry(path, &entry))
    return -ENOENT;

  const size_t current_size = wfs_file_entry_get_size(&entry);
  if (offset > current_size)
    return -EINVAL;

  uint16_t block_position;
  wfs_image_t *image = get_wfs_image();
  int block = wfs_get_current_block(image, &entry, offset, &block_position);

  if (block >= WFS_BLOCK_EOF)
    return -EIO;

  if (offset + size > current_size)
    size = current_size - offset;

  int read = 0;
  while (size > 0 && block > WFS_BLOCK_FREE && block <= WFS_BLOCK_EOF)
    {
      off_t block_offset;
      uint16_t transfer;

      block_offset = wfs_get_block_offset(block - 1);

      if (block_position == 0)
        transfer = WFS_BLOCK_SIZE;
      else
        transfer = WFS_BLOCK_SIZE - block_position;

      if (transfer > size)
        transfer = size;

      transfer = pread(image->fd, buf + read, transfer,
                       block_offset + block_position);

      size -= transfer;
      read += transfer;
      offset += transfer;

      if (size > 0)
        {
          block = wfs_get_next_block(image, block);
          block_position = 0;

          if (block == WFS_BLOCK_FREE || block >= WFS_BLOCK_EOF)
            return -EIO;
        }
    }

  return read;
}


static int
wfs_write(const char *path, const char *buf, size_t size, off_t offset,
          struct fuse_file_info *fi)
{
  return -ENOSYS;
}

/*
 * FUSE setup
 */

static struct fuse_operations wfs_oper =
{
  .readdir   = wfs_readdir,
  .mkdir     = wfs_mkdir,
  .rmdir     = wfs_rmdir,
  .getattr   = wfs_getattr,
  .open      = wfs_open,
  .create    = wfs_create,
  .read      = wfs_read,
  .write     = wfs_write
};

int
main(int argc, char *argv[])
{
  /* Count number of arguments without hyphens; excluding execname */
  int count = 0;
  for (int i = 1; i < argc; ++i)
    if (argv[i][0] != '-')
      count++;

  if (count != 2)
    {
      fprintf(stderr, "error: file and mountpoint arguments required.\n");
      return -1;
    }

  /* Extract filename argument; we expect this to be the
   * penultimate argument.
   */
  const char *filename = argv[argc-2];
  argv[argc-2] = argv[argc-1];
  argv[argc-1] = NULL;
  argc--;

  /* Try to open the file system */
  wfs_image_t *img = wfs_image_open(filename);
  if (!img)
    return -1;

  /* Start fuse main loop */
  int ret = fuse_main(argc, argv, &wfs_oper, img);
  wfs_image_close(img);

  return ret;
}
