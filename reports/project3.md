Final Report for Project 3: File System
=======================================

## Group Members

* Mike Lee <mike.lee@berkeley.edu>
* Jennie Chen <jenniechen@berkeley.edu>
* Michael Dong <michaeldong@berkeley.edu>
* Nathan Fong <nfong1996@berkeley.edu>

# Files Edited:

* devices/block.c
* devices/block.h
* filesys/directory.c
* filesys/directory.h
* filesys/file.c
* filesys/file.h
* filesys/filesys.c
* filesys/filesys.h
* filesys/inode.c
* filesys/inode.h
* threads/thread.c
* threads/thread.h
* userprog/process.c
* userprog/syscall.c
* userprog/syscall.h

# **PART 1:**
    
### Final Implementation:

**Note: If not directly mentioned as being changed or taken out, we still used what was in our design doc.**

* We kept the general idea of the implementation for part 1 the same.
* However, we did change the way we implemented certain functions and also the specifics of what goes inside the structs
* We also made a couple of major differences in how we approached the structs
    * Got rid of the struct `inode_disk` from `struct disk_block`
        * Changed it to an char array and just copied data in and out from it
    * Decided not to add in struct `inode_disk *data_ptr`
    * Instead we decided that `block_sector_t sector` was enough for our implementation
* Moved multiple struct definitions to the header files in order to keep consistent
* After our design review, we also moved the `bool is_dir` from the `inode` struct to the `inode_disk` struct

### Major Differences (with Explanations) 
* Struct definitions
    * We decided to get rid of the `struct inode_disk` from the struct disk_block because of numerous reasons
        * We were informed that the `disk_block` should be able to take any type of data not just an `inode_disk`
        * We also were given the size that the data could reach
        * Because of this we decided to use a char array of size 512
    * Getting rid of inode_disk *data_ptr
        * We decide to get rid of this pointer all together because we were able to achieve the same result with the information from `block_sector_t sector`
        * We realized that blocks and sectors were the same in pintos which simplified the structs a bit.
        
# **PART 2:**
    
### Final Implementation:

**Note: If not directly mentioned as being changed or taken out, we still used what was in our design doc.**

* We used the same general approach as detailed in the design doc, as both our final solution and design were heavily inspired by the discussion worksheet.
* We made minor changes to `struct inode_disk` and added a new struct for indirect blocks.
* We also made additional changes to functions like `inode_write_at` to allow it to be more flexible with both parts 1 and 2.

### Major Differences (with Explanations) 

* In `struct inode_disk` we made the following changes that were different from our design doc:
    * Removed `block_sector_t start`
    * Changed having separate arrays for direct, indirect, and doubly_indirect pointers and instead created one array of `block_sector_t pointers[125]`
    * Because of this change, we created a new `struct indirect_block` for indirect and doubly indirect pointers
    
* In `inode_write_at` we modified the function to call a more generalized function `write_sector` which allowed our `allocate_block` function to call it as well. 
    * `void write_sector(block_sector_t sector, void* buffer, off_t offset, size_t size)` writes `size` bytes of `buffer` into `sector` starting at `offset`. This lets both the cache code in `inode_write_at` as well as our inode resizing code in `change_block_count` and `allocate_block` to call it.
    
* Also of note is the way we implemented `inode_resize`. We added several new helper functions `calculate_index`, `allocate_block`, and `change_block_count` to deal with the complexity involved with indirect and doubly indirect pointers.
    * `inode_resize` first makes a call to the helper `bool change_block_count(struct inode_disk *id, block_sector_t block, bool add)`, which is responsible for adding or removing blocks from the inode_disk at the given block_sector_t depending on the value of the boolean value `add`.
        * `change_block_count` then utilizes the helpers `allocate_block` and `calculate_index`
        * `change_block_count` is where most of the logic is; depending on the value of the bool, we allocate or deallocate blocks and do the corresponding reads and writes.
    * `bool calculate_index(block_sector_t block_num, int *indices, int *num_indices)` is our simple helper function to calculate a list of indices for the block number in the inode. We return `true` upon success and `false` otherwise. The calculated indices are stored in `int *indices` and the calculated number of indices are stored in `int *num_indices`.
    * `int allocate_block()` is a helper function that returns the block_sector_t of the new block allocated, or -1 upon failure. We fill the new sector with zeroes if successful. We call this function if the `bool add` in `change_block_count` is set to `true`.
    
    
# **PART 3:**

### Final Implementation:
**Note: If not directly mentioned as being changed or taken out, we still used what was in our design doc.**

* We decided to have a thread's `cwd` be a `struct dir` instead of just the path
* Instead of using the given `get_next_path` code, we ended up using `strtok_r` inside of a helper method we made `dir_resolve` that was used to get parent directories for all paths.
* Instead of storing an `is_dir` bool inside a `struct file` or a `struct dir`, they are all stored in the `struct inode_disk` so we just lookup that.
* In trying to solve our other issues, we never actually made the `dir_lock` structs; this was a mistake that we should have remedied but didn't have the time to.

### Major Differences (with Explanations) 

* After discussion with Michael in our design review, we realized it was probably better to have a proccess have access to it's exact `struct dir` for a current working directory instead of just the path. This way, it doesn't have to resolve the path every single time and open up another `struct dir`, so this way should be much faster and easier since everything dealing with files can just use the already-open struct.

* To facilitate all of the path resolving we had to do, we created a helper method `dir_resolve` that would take in a path and find the parent directory of that path. That way, if we were trying to add new files we had the directory we wanted to add them to, while if we needed that exact path we only had to do one manual lookup. Originally we had it resolve the entire path, but then we realized we had issues with trying to backtrack.

* We realized that for a `struct dir` or `struct file` to have an `is_dir` bool, we needed something lower down that knew because of the way we were writing our code. We took a suggestion from Michael and instead moved down the boolean to the `struct inode_disk` for every file and directory. Because every `struct file` and `struct dir` has an inode, we could always use it to access the `inode_disk` to check this boolean.

* A more minor change, but instead of dealing with casting every time we wanted to put a file or directory in our array for file descriptors, we just created a `wrapper` struct to deal with it.
