#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  int i;
  struct disk_block *temp;
  lock_acquire (&cache_lock);
  for (i = 0; i < 64; i++) 
    {
      temp = cache[i];
      if (temp->dirty)
        {
          lock_acquire (&temp->block_lock);
          block_write (fs_device, temp->sector_id, temp->data);
          lock_release (&temp->block_lock);
        }
      palloc_free_page (temp);
    }
  lock_release (&cache_lock);
  free_map_close ();

}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size)
{

  block_sector_t inode_sector = 0;

  struct dir *par_dir;
  bool success;
  success = dir_resolve (name, &par_dir);

  if (!success || par_dir->inode->removed)
    return false;

  int split;
  split = last_occurrence (name, '/');
  char *last_name;
  last_name = malloc (128);
  strlcpy (last_name, name + split + 1, PGSIZE);

  success = (par_dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (par_dir, last_name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (par_dir);
  free (last_name);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  bool success;
  struct dir *par_dir;
  success = dir_resolve (name, &par_dir);

  if (!success)
    return false;

  int split;
  split = last_occurrence (name, '/');
  char *last_name;
  last_name = malloc (128);
  strlcpy (last_name, name + split + 1, PGSIZE);

  struct inode *inode;
  success = dir_lookup (par_dir, last_name, &inode);

  dir_close (par_dir);
  free (last_name);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  struct dir *par_dir;
  bool success;
  success = dir_resolve (name, &par_dir);

  if (!success)
    return false;

  int split;
  split = last_occurrence (name, '/');
  char *last_name;
  last_name = malloc (128);
  strlcpy (last_name, name + split + 1, PGSIZE);

  struct inode *inode;
  success = dir_lookup (par_dir, last_name, &inode);

  if (!success)
    return false;

  struct inode_disk *metadata;
  uint8_t buffer[BLOCK_SECTOR_SIZE];
  metadata = read_sector(inode->sector, buffer);

  struct dir *to_remove = dir_open (inode);
  if (!metadata->is_dir)
    success = dir_remove (par_dir, last_name);
  else
    {
      int count = 0;
      char *name = malloc (128);
      success = true;

      while (success)
        {
          success = dir_readdir (to_remove, name);
          if (strcmp (name, ".") != 0 && strcmp (name, "..") != 0)
            count++;
        }

      if (count > 0)
        return false;

      success = dir_remove (par_dir, last_name);
      free (name);

    }
  dir_close (to_remove);
  dir_close (par_dir);
  free (last_name);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

unsigned long long
block_reads (void)
{
  return get_read_cnt(fs_device);
}

unsigned long long
block_writes (void)
{
  return get_write_cnt(fs_device);
}
