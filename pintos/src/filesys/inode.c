#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/palloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

void write_sector(block_sector_t sector, void* buffer, off_t offset, size_t size);
bool calculate_index(block_sector_t block_num, int *indices, int *num_indices);

bool change_block_count(struct inode_disk *id, block_sector_t block, bool add);
bool inode_resize(struct inode_disk *id, size_t size);

struct indirect_block
  {
    block_sector_t pointers[NUM_BLOCK_POINTERS];
  };

void
cache_init (void)
{
  int i;
  for (i = 0; i < 64; i++) 
    {
      struct disk_block *block = palloc_get_page(0);
      lock_init (&block->block_lock);
      memset (block->data, 0, BLOCK_SECTOR_SIZE);
      block->sector_id = -1;
      block->empty = true;
      block->using = false;
      block->dirty = false;
      cache[i] = block;
    }

  clock_hand = 0;
  cache_hits = 0;
  cache_misses = 0;
  lock_init (&cache_lock);
}

void
cache_clear (void)
{
  int i;
  lock_acquire (&cache_lock);
  for (i = 0; i < 64; i++) 
    {
      struct disk_block *block = cache[i];
      lock_acquire (&block->block_lock);
      block->sector_id = -1;
      block->empty = true;
      block->using = false;
      block->dirty = false;
      lock_release (&block->block_lock);
    }

  cache_hits = 0;
  cache_misses = 0;
  lock_release (&cache_lock);
}


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);

  struct inode_disk *id;
  uint8_t buffer[BLOCK_SECTOR_SIZE];
  id = read_sector(inode -> sector, buffer);

  int result = 0;

  size_t block = pos / BLOCK_SECTOR_SIZE;
  int indices[3];
  int num_indices;
  calculate_index(block, indices, &num_indices);
  struct indirect_block *cur = malloc(sizeof(struct indirect_block));

  result = id -> pointers[indices[0]];
  if (num_indices > 1) {
    if (result == 0)
      goto done;
    read_sector(result, cur->pointers);
    result = cur->pointers[indices[1]];
  }
  if (num_indices > 2) {
    if (result == 0)
      goto done;
    read_sector(result, cur->pointers);
  }

done:
  free(cur);
  if (result != 0)
    return result;
  return -1;
}

/* Takes in a sector number and writes content from sector into the buffer.
 * Looks in the cache first and updates cache with clock algorithm on miss.
 * Buffer size must fit an entire sector. */
void *
read_sector (block_sector_t sector, void *buffer)
{
  int i;
  struct disk_block *block = NULL;
  lock_acquire (&cache_lock);

  for (i = 0; i < 64; i++)
    {
      if (cache[i]->sector_id == sector) 
        {
          block = cache[i];
          break;
        }
    }

  if (block != NULL)
    {
      lock_acquire (&block->block_lock);
      cache_hits++;
      lock_release (&cache_lock);
      block->using = true;
      memcpy (buffer, block->data, BLOCK_SECTOR_SIZE);
      lock_release (&block->block_lock);
    }
  else 
    {
      struct disk_block *to_replace = clock_algorithm ();
      cache_misses++;
      lock_acquire (&to_replace->block_lock);
      to_replace->sector_id = sector;
      to_replace->using = true;
      to_replace->empty = false;
      to_replace->dirty = false;
      block_read (fs_device, sector, to_replace->data);
      memcpy (buffer, to_replace->data, BLOCK_SECTOR_SIZE);
      lock_release (&to_replace->block_lock);
      lock_release (&cache_lock);
    }
  return buffer;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  lock_init (&freemap_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = 0;
      disk_inode->is_dir = is_dir;
      disk_inode->magic = INODE_MAGIC;
      if (inode_resize (disk_inode, length))
        {
          write_sector (sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
          success = true;
        }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->dw_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {

          struct inode_disk *data;
          uint8_t buffer[BLOCK_SECTOR_SIZE];
          data = read_sector (inode->sector, buffer);
          
          free_map_release (inode->sector, 1);
          int cur_dealloc;
          int num_blocks = bytes_to_sectors (data->length);
          for (cur_dealloc = 0; cur_dealloc < num_blocks; cur_dealloc++)
            change_block_count (data, cur_dealloc, false);
        }
      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      int i;
      struct disk_block *block = NULL;
      lock_acquire (&cache_lock);

      for (i = 0; i < 64; i++)
        {
          if (cache[i]->sector_id == sector_idx) 
            {
              block = cache[i];
              break;
            }
        }

      if (block != NULL)
        {
          lock_acquire (&block->block_lock);
          cache_hits++;
          lock_release (&cache_lock);
          block->using = true;
          memcpy (buffer + bytes_read, block->data + sector_ofs, chunk_size);
          lock_release (&block->block_lock);
        }
      else 
        {
          struct disk_block *to_replace = clock_algorithm ();
          lock_acquire (&to_replace->block_lock);
          to_replace->sector_id = sector_idx;
          to_replace->using = true;
          to_replace->empty = false;
          to_replace->dirty = false;
          cache_misses++;
          block_read (fs_device, sector_idx, to_replace->data);
          memcpy (buffer + bytes_read, to_replace->data + sector_ofs, chunk_size);
          lock_release (&to_replace->block_lock);
          lock_release (&cache_lock);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  return bytes_read;
}

/* Gives a list of indices for the block number in the inode. There is one index for a 
direct pointer and an additional index for each additional indirect layer. */
bool calculate_index (block_sector_t block_num, int *indices, int *num_indices) 
{
  if (block_num < NUM_DIRECT_POINTERS) 
    {
      indices[0] = block_num;
      *num_indices = 1;
      return true;
    }
  block_num -= NUM_DIRECT_POINTERS;
  if (block_num < NUM_BLOCK_POINTERS) 
    {
      indices[0] = NUM_DIRECT_POINTERS;
      indices[1] = block_num;
      *num_indices = 2;
      return true;
    }
  block_num -= NUM_BLOCK_POINTERS;
  if (block_num < NUM_BLOCK_POINTERS * NUM_BLOCK_POINTERS) 
    {
      indices[0] = NUM_DIRECT_POINTERS + 1;
      indices[1] = block_num / NUM_BLOCK_POINTERS;
      indices[2] = block_num % NUM_BLOCK_POINTERS;
      *num_indices = 3;
      return true;
    }
  return false; 
}

int 
allocate_block () 
{
  uint8_t zeros[BLOCK_SECTOR_SIZE];
  memset (zeros, 0, sizeof (zeros));
  block_sector_t new_block; 
  lock_acquire (&freemap_lock);
  bool success = free_map_allocate (1, &new_block);
  lock_release (&freemap_lock);
  if (!success)
    return -1;
  write_sector (new_block, zeros, 0, BLOCK_SECTOR_SIZE); 
  return new_block;
}

/* Adds or removes blocks from the inode_disk at the given block_sector_t. */
bool 
change_block_count (struct inode_disk *id, block_sector_t block, bool add) 
{
  int indices[3];
  int num_indices;
  
  struct indirect_block *cur = malloc(sizeof(struct indirect_block));
  calculate_index(block, indices, &num_indices);

  if (add) 
    {
      if (id->pointers[indices[0]] == 0) 
        {
          int new_block = allocate_block ();
          if (new_block < 0) 
            {
              free (cur);
              return false;
            }
          id->pointers[indices[0]] = new_block;
        }
      if (num_indices > 1) 
        {
          block_sector_t indirect_block = id->pointers[indices[0]];
          read_sector (indirect_block, cur->pointers);
          if (cur -> pointers[indices[1]] == 0) 
            {
              int new_block = allocate_block ();
              if (new_block < 0)
                {
                  free (cur);
                  return false;
                }
              cur->pointers[indices[1]] = new_block;
              write_sector (indirect_block, cur->pointers, 0, BLOCK_SECTOR_SIZE);
            }
        }
      if (num_indices > 2) 
        {
          block_sector_t indirect_block = id -> pointers[indices[0]];
          read_sector (indirect_block, cur -> pointers);
          indirect_block = cur -> pointers[indices[1]];
          read_sector (indirect_block, cur -> pointers);
          if (cur -> pointers[indices[2]] == 0) 
            {
              int new_block = allocate_block ();
              if (new_block < 0) 
                {
                  free (cur);
                  return false;
                }
              cur->pointers[indices[1]] = new_block;
              write_sector (indirect_block, cur->pointers, 0, BLOCK_SECTOR_SIZE);
            }
        }
    } 
  else 
    {
      if (num_indices == 1) 
        {
          lock_acquire (&freemap_lock);
          free_map_release (id->pointers[indices[0]], 1);
          lock_release (&freemap_lock);
          id->pointers[indices[0]] = 0;
        }
      if (num_indices == 2) 
        {
          block_sector_t indirect_block = id->pointers[indices[0]];
          read_sector (indirect_block, cur->pointers);
          lock_acquire (&freemap_lock);
          free_map_release (cur->pointers[indices[1]], 1);
          lock_release (&freemap_lock);
          cur->pointers[indices[1]] = 0;
          write_sector (indirect_block, cur->pointers, 0, BLOCK_SECTOR_SIZE);
        }
      if (num_indices == 3) 
        {
          block_sector_t indirect_block = id->pointers[indices[0]];
          read_sector (indirect_block, cur->pointers);
          indirect_block = cur->pointers[indices[1]];
          read_sector (indirect_block, cur->pointers);
          lock_acquire (&freemap_lock);
          free_map_release (cur->pointers[indices[2]], 1);
          lock_release (&freemap_lock);
          cur->pointers[indices[2]] = 0;
          write_sector (indirect_block, cur->pointers, 0, BLOCK_SECTOR_SIZE);
        }
      }
  free(cur);
  return true;
}

bool 
inode_resize(struct inode_disk *id, size_t size) 
{
  size_t num_blocks = bytes_to_sectors (id -> length);
  size_t num_new_blocks = bytes_to_sectors (size);
  block_sector_t cur;
  block_sector_t cur_dealloc;
  if (num_blocks < num_new_blocks) 
    {
      for (cur = num_blocks; cur < num_new_blocks; cur++) 
        {
          if (!change_block_count (id, cur, true)) 
            {
              for (cur_dealloc = num_blocks + 1; cur_dealloc < cur; cur_dealloc++)
                change_block_count (id, cur_dealloc, false);
              return false;
            }
        }
    }
  else if (num_blocks > num_new_blocks) 
    {
      for (cur_dealloc = num_new_blocks + 1; cur_dealloc <= num_blocks; cur_dealloc++)
        change_block_count (id, cur_dealloc, false);
    }
  id->length = size;
  return true;
}

/* Takes a sector number and writes size bytes of the buffer into the sector starting at the offset..
 * Writes the sector into the write-back buffer cache and writes to disk when evicted from the cache. */
void
write_sector(block_sector_t sector, void* buffer, off_t offset, size_t size)
{
    int i;
    struct disk_block *block = NULL;
    lock_acquire (&cache_lock);
    
    for (i = 0; i < 64; i++)
      {
        if (cache[i]->sector_id == sector) 
          {
            block = cache[i];
            break;
          }
      }

    if (block != NULL)
      {
        lock_acquire (&block->block_lock);
        cache_hits++;
        lock_release (&cache_lock);
        block->using = true;
        block->dirty = true;
        memcpy (block->data + offset, buffer, size);
        lock_release (&block->block_lock);
      }
    else 
      {
        struct disk_block *to_replace = clock_algorithm ();
        lock_acquire (&to_replace->block_lock);
        to_replace->sector_id = sector;
        memcpy (to_replace->data + offset, buffer, size);
        to_replace->using = true;
        to_replace->empty = false;
        to_replace->dirty = true;
        cache_misses++;
        lock_release (&to_replace->block_lock);
        lock_release (&cache_lock);
      }
}


/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  if (inode_length (inode) < offset + size) 
    {
      struct inode_disk id;
      read_sector (inode->sector, &id);
      inode_resize (&id, offset + size);
      write_sector (inode->sector, &id, 0, BLOCK_SECTOR_SIZE);
    }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < sector_left ? size : sector_left;
      if (chunk_size <= 0)
        break;

      write_sector(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Explain what this does */
struct disk_block *
clock_algorithm (void)
{
  int found = false;
  struct disk_block *temp;

  while (!found)
    {
      temp = cache[clock_hand];
      lock_acquire (&temp->block_lock);
      clock_hand = (clock_hand + 1) % 64;
      if (temp->empty || !temp->using)
        found = true;
      else
        temp->using = false;
      lock_release (&temp->block_lock);
    }

  if (temp->dirty)
    {
      lock_acquire (&temp->block_lock);
      block_write (fs_device, temp->sector_id, temp->data);
      lock_release (&temp->block_lock);
    }

  return temp;

}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  lock_acquire (&inode->dw_lock);
  inode->deny_write_cnt++;
  lock_release (&inode->dw_lock);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_acquire (&inode->dw_lock);
  inode->deny_write_cnt--;
  lock_release (&inode->dw_lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk *data;
  uint8_t buffer[BLOCK_SECTOR_SIZE];
  data = read_sector (inode -> sector, buffer);
  return data->length;
}
