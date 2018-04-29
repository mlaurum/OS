Design Document for Project 3: File Systems
============================================

## Group Members

* Mike Lee <mike.lee@berkeley.edu>
* Jennie Chen <jenniechen@berkeley.edu>
* Michael Dong <michaeldong@berkeley.edu>
* Nathan Fong <nfong1996@berkeley.edu>

# **PART 1:**

### Data structures and functions:

* Remove the filesys_lock (global lock) from project 2 as well as the other global locks

* Note: All locks and other things that need to be initialized will be initialized appropriately in various places.

Adding in filesys/inode.c:

```
struct disk_block {
	struct lock block_lock;
	block_sector_t sector_id;	        /* disk sector of data */
	struct inode_disk *data; 	        /* pointer to cached data */
	bool using;			        /* if data is in use - used for clock algorithm */
	bool empty; 			        /* if data is present */
	bool dirty;			        /* if data was changed */
};
```

* `struct disk_block cache[64]`  

	* Array of `disk_block` structs for to create our buffer cache

* `static int clock_hand;`  

	* Keep track of where in cache we are checking for replacement

* `static struct lock cache_lock;`

	* Lock for the cache

* Modifying the `inode` struct:  

	* Remove `struct inode_disk data`  
	* Add in `struct inode_disk *data_ptr` - pointer to the `inode_disk` struct  


### Algorithms:

* Respond to reads with cached data:
	* Acquire `cache_lock`
	* Modify `inode_read_at` - before calling `block_read`, check all 64 `disk_block` structs in `cache` to see if the desired sector is cached
	* If found, acquire the `block_lock` and release `cache_lock`
		* Then, load the block into the buffer (get rid of bounce buffer) and set `using` to true and `empty` to false, then release the `block_lock`
	* Otherwise, proceed with `block_read` as usual
		* Store newly-read block into `cache`
			* Either find an empty block or if not possible, use clock algorithm to find one to evict (see block replacement section below)
		* Release `cache_lock`

* Coalesce multiple writes (get rid of the bounce buffer!):
	* Acquire the `cache_lock`
		* Look for the appropriate sector to write to
	* If found, lock the `block_lock`, release `cache_lock`, and set the `dirty` bool, then release the `block_lock`
	* Otherwise, write the new sector to the cache block and set `dirty` bool
		* If possible, find an empty block
		* Otherwise, use clock algorithm (see block replacement section below) to find block for eviction
		* Store data to be written in this block of the `cache` and set all appropriate booleans
		* Release `cache_lock`

* Block replacement:
	* Starting from index of `clock_hand`, check each struct if it is being used
		* If no, return this block for eviction
		* If yes, clear the `using` boolean and move on to the next block, incrementing `clock_hand`
	* Continue (looping around the array if necessary) until a block is found for eviction
        * Take block chosen for eviction, acquire `block_lock` and write to disk using `block_write` if `dirty` bool is set, then release the `block_lock`

* Modify `filesys_done()`:
	* When system shuts down, evict all blocks in `cache` and write them all back to disk

* Other disk operations:
	* Anywhere the current code accesses inode->data is now outdated because we remove the `inode_disk` struct from the `inode`
	* Instead, check the inode’s `data_ptr`; if NULL, the data needs to be read in from the cache - follow above procedure for reading
	* Otherwise, just follow the pointer to the `inode_disk` struct and use that
    
### Synchronization:

* A large amount of our synchro issues are solved by having a lock on the `cache`
	* A block that is being read or written to can't be evicted because whoever is writing/reading the block will have acquired the `block_lock`, and in order to evict the block some other process needs to acquire `cache_lock` and then also the `block_lock`, so they'll have to wait until the first process is done.
	* Similarly, since the `block_lock` needs to be acquired in order to both access and evict blocks, multiple processes can't do it at the same time.
	* This also holds for multiple processes trying to load a block into the cache. The `cache_lock` is acquired before a process even checks if the block needs to be loaded and it is also acquired when actually loading a block and is only released after it is loaded; another process won't be in the situation where it decides to load the block, waits for the lock while another process loads the same block, and then loads it again, because they won't check until after the first process has loaded it. Other processes can only access the block when the `cache_lock` is released, which will be after it is fully loaded.

   
### Rationale: 

* We decided to use an array for the cache instead of the given linked list implementation because since we had an upper bound on the size of the cache, we thought that having an array would give us faster accesses without us having to deal with resizing.

* We also considered trying to implement LRU, but we decided to go with just the clock algorithm instead because we were more familiar with it. We also decided to just use a boolean in our `disk_block` structs to track various things like if the block was dirty instead of the bitmap because we thought it would be simpler to code and deal with, and we didn't want to accidentally introduce bugs while trying to deal with overhead of keeping the bitmap consistent.


# **PART 2:**

### Data structures and functions: 

* Modifying in filesys/inode.c:

```
struct inode_disk { 
	block_sector_t start;			    /* First data sector */
    off_t length;				    /* File size in bytes. */ 
    struct inode *inode;
    block_sector_t direct[122]; 		    /* 122 direct pointers */ 
    block_sector_t indirect; 		    	    /* a singly indirect pointer */ 
    block_sector_t doubly_indirect;	            /* a doubly indirect pointer */
    unsigned magic;			            /* Magic number */
}; 
```

* `struct lock freemap_lock;`
	* To use when using the freemap
	
* `bool inode_resize(struct inode_disk inode, off_t size)`
	* Use this newly added function to resize the inodes sizes after extending a file

* `off_t inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)`
	* modify this function to account for writing off the end of a file


Modifying in userprog/syscall.c:

* `int  inumber_handler(int fd)`
	* Helper function to be called from `syscall_handler` to deal with `SYS_INUMBER`

* `syscall_handler(int fd)`
    * Modify to deal with SYS_INUMBER

    
### Algorithms:

* Extending a file:
	* In `inode_write_at` we check to make sure that when we run off the end of a file we 
resize the inode. Call `inode_resize` to resize the the parameters of inode_disk. 
	* In `inode_resize` we manipulate the members `direct`, `indirect`, `doubly_indirect` and 
`length` of the `inode` struct in order to reflect the new size of the file. Files have to be less than 2^23. (Implementation heavily inspired by the discussion)
	* First, acquire the `freemap_lock`
	* Start by calculating how many blocks you'll need to allocate and set up an array of `block_sector_t`.
	* The idea behind `inode_resize` is to iterate through all available blocks and allocate 
more space for each  inode if the new size is greater than the old size and deallocate 
space for each inode if the new size is less than the old size. These increases and 
decreases in size are reflected in the `inode_disk` struct and is also limited by the max file 
size of 2^23
	* In order to avoid fragmentation when allocating a block, we will iterate through all the blocks and possibly allocate non-consecutive blocks
	* We can iterate from the last allocated block in the inode through all other blocks or we can iterate from the newest block position back to the older block position and get rid of whatever we no longer need(deallocating all these now unnecessary blocks). We can use `free_map_allocate` to allocate blocks and `free_map_release` to dealloc a block. Every time we allocate a block, we save the `block_sector_t` for the allocated space.
	* Once we've allocated enough blocks to get to the seeked position past EOF  without any errors or running out of space, we can  fill it all with zeroes. Then, we just need to allocate enough space to write the actual content we are trying to write; the writing can happen as previously implemented once space is allocated and we are positioned at the right spot
	* Once completely finished, release `freemap_lock`

* Aborting current operation:
	* If we try to allocate more space when writing actual content (and not zeroes) and fail, then we just abort everything and stop writing more content
	* If we try to allocate more space (filling in with zeroes) and run out of space, we can rollback by releasing all of the blocks that we allocated before, as all of the `block_sector_t`s are still stored in our array.

* Dealing with SYS_INUMBER syscall:
    * In `inumber_handler`, find the corresponding fd of the file (in the thread’s `files` array)
    * Get the corresponding inode from the file
    * Call `inode_get_inumber` on the inode to get the unique inode number.
    
### Synchronization:

* The main synchronization issue that could happen is if multiple processes try to access the `free_map` in order to allocate or deallocate blocks. We solve this by using a lock on the `free-map`; before allocating or deallocating anything, a process has to acquire the lock. That way, you can't have multiple processes calling `free_map_allocate` and possibly getting back the same empty block.

### Rationale: 

 * In order to handle disk space exhaustion, we chose to try to pre-calculate how many blocks we need and then allocate before actually writing anything; that way, if we run out of space, we won't have to figure out how to un-write anything. We considered doing a total rollback instead of just aborting if we'd actually written any content (that wasn't zeroes), but we figured that the actual write syscall would just return a smaller number of bytes written instead of not writing anything, so we decided to still write what we could.

# **PART 3:**

### Data structures and functions:

* Adding to thread struct in thread.h:

* `char *curr_dir;`
	* path to process’s current directory

* Modifying in userprog/syscall.c:

* `bool chdir_handler(const char *dir)`
	* Helper function to be called from `syscall_handler` to deal with `SYS_CHDIR`

* `bool mkdir_handler(const char *dir)`
	* Helper function to be called from `syscall_handler` to deal with `SYS_MKDIR`

* `bool readdir_handler(int fd, char *name)`
	* Helper function to be called from `syscall_handler` to deal with `SYS_READDIR`

* `bool isdir_handler(int fd)`
	* Helper function to be called from `syscall_handler` to deal with `SYS_ISDIR`

* `syscall_handler(int fd)`
    * Modify to deal with SYS_CHDIR, SYS_MKDIR, SYS_READDIR, SYS_ISDIR

* `int read_handler (int fd, void *buffer, unsigned size)`
	* Modify to check if `fd` corresponds to a directory using `isdir`; if so, do nothing
      
* `int write_handler (int fd, const void *buffer, unsigned size)`
	* Modify to check if `fd` corresponds to a directory using `isdir`; if so, do nothing

* `bool remove_handler(const char *file)`
    * Modify to support deleting empty directories

* `int exec_handler(const char *cmd_line)`
    * Modify so child process inherits parent’s current directory (will involve change thread creation process)

* Modifying in file.c:
	
```
struct file {
	bool is_dir;			/* always set to false */
	struct inode *inode;        /* File's inode. */
	off_t pos;                  /* Current position. */
	bool deny_write;            /* Has file_deny_write() been called? */
};
```

```
struct dir {
	bool is_dir;	/* always set to true */
	struct lock dir_lock;
	…
};
```

* Modifying in thread.h:
	* type of `struct file *files[]` should change to `void *files[]`
    
### Algorithms:

* Implementing chdir:
	* Find the global path of the directory to change to.
	* Change the `curr_dir` member of the current thread struct to the found global path.

* Implementing mkdir:
	* Ensure that we are working with absolute path
	* Check if dir path is valid; use `get_next_path` to move from root directory to subsequent directories
	* Return false if exact directory is found or if a non-terminal component is missing
	* Modify `filesys_create` to take in another argument indicating if it is creating a directory or not
	* Modify `filesys_create`; if that extra argument is true, then call `dir_create` instead of just `inode_create` to deal with extra entries; otherwise, everything else is the same
	* Call `filesys_create`  with that extra argument as true to create the directory file
	* Also, use `dir_add` to create two new files in the new directory, "." and ".."
		* For "." the `inode_sector` passed in to `dir_add` should be the sector of the new directory's inode.
		* For ".." the `inode_sector` passed in should be the same as the process's current directory's inode's sector.
	* Return true/false depending on success

* Implementing readdir:    
 	* Check that `fd` corresponds to a directory file
	* Acquire the `dir_lock` of the corresponding `dir_struct`
	* Call `dir_readdir` using the `dir` struct from the file
	* Release the `dir_lock`

* Implementing isdir:
	* Get back whatever corresponds to `fd` in the thread’s `files` array and return it’s `is_dir` bool

* Modifying previous handlers:
	* Create - check for component limits of 14 chars instead of path limit
	* Exec - have child inherit parent process’s current directory
	* Syscalls with file names: if path is relative, add in the path to the current directory to the file name to get an absolute path
	* Open - if called on a directory, call `dir_open` instead of `file_open`
	* Close - if called on a directory, also call `dir_close` on the inode of the file
	* Remove - if called on a non-root directory, check if directory has any files/subdirs - if so, do not remove
		* Otherwise, proceed as normal; also, since it has been removed, we'll set its `is_dir` bool to false since it cannot be used anymore.
		
* Open, Close, and Remove are considered directory level syscalls if called on a directory - if so, acquire the `dir_lock` before doing anything and release it once done.
		
* Modifying `lookup`:
	* We'll modify `lookup` so that it can look multiple levels down into directories
	* We'll use `get_next_path` to move from root directory to subsequent directories and just keep calling it until we get to the last directory, where we can check for the given file name.
	* If at any point one of the directories we look for isn't present or the file isn't present, we return false; otherwise, when the file is found we carry through with the original code.

### Synchronization:

* The main synchronization problem with this part is the fact that we have to serialize operations that occur on the same disk sector, the same file, or the same directory. To solve this issue, we look at making individual locks for each directory made that is locked and unlocked only by directory level syscalls. (examples of this include, remove). The synchronization issue with regards to files and sectors should be dealt with in the cache implementation. Our locks in our cache should prevent things from simultaneously accessing the same sector. We initially thought about implementing individual file locks as well, however according to piazza the accessing the same file simultaneously is undefined behavior. 
   
### Rationale: 

* We chose to create files "." and ".." so it was easier to deal with relative paths. Since we decided to just make relative paths into absolute paths by adding the current directory's path, when we do anything with paths, `lookup` from directory.c is called. Since we've modified `lookup` so that it works recursively, it should just be able to follow each part of the path with "." and ".." pointing to the correct relative directory until we reach the file. Absolute paths should still work the same as before, as now we are effectively just making everything appear to be an absolute path.

* We decided to allow a process to delete a directory even if it is the cwd of a running process, just for simplicity. However, we ensured that new files can't be created in deleted directories by setting the deleted directory's `is_dir` boolean to false. Since when we create new files, we first check if what we are adding it to is a directory, we'd never be able to add anything to these deleted directories. 

* We also decided to change the type of each thread's `files` array just so it could hold both files and directories. We thought this would be easier because sometimes we need direct access to the actual `dir` struct and not just an inode for it, so instead of trying to change a lot of other structs we just allowed both to be contained in the array. This way, we cn map both of them to a file desciptor easily, so trying to locate a file or directory works exactly the same - you just get the file descriptor and check the corresponding index in the array.

