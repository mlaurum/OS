#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"

#define NUM_DIRECT_POINTERS 123
#define NUM_BLOCK_POINTERS 128
struct bitmap;
int cache_hits;
int cache_misses;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    bool is_dir;
    block_sector_t pointers[NUM_DIRECT_POINTERS + 2];
    unsigned magic;                     /* Magic number. */
  };

struct disk_block
  {
    struct lock block_lock;
    block_sector_t sector_id;
    char data[512];
    bool using;
    bool empty;
    bool dirty;
  };

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock dw_lock;
  };

void cache_init (void);
void cache_clear (void);
struct disk_block *clock_algorithm (void);
struct disk_block *cache[64];
int clock_hand;
struct lock cache_lock;
struct lock freemap_lock;

void *read_sector(block_sector_t sector, void *buffer);

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
