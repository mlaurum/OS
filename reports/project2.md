Final Report for Project 2: User Programs
=========================================

## Group Members

* Mike Lee <mike.lee@berkeley.edu>
* Jennie Chen <jenniechen@berkeley.edu>
* Michael Dong <michaeldong@berkeley.edu>
* Nathan Fong <nfong1996@berkeley.edu>

# Files Edited:

* userprog/process.c

* userprog/process.h

* userprog/syscall.c

* userprog/syscall.h

* userprog/exception.c

* threads/thread.c

* threads/thread.h

# **PART 1:**
    
### Final Implementation:
* Our implementation was largely the same as in our design doc. Perhaps the only major change we made was that after our design review, we decided to use `strtok_r` instead of `strtok`, which wasn't thread safe. 

# **PART 2:**
    
### Final Implementation:

**Note: If not directly mentioned as being changed or taken out, we still used what was in our design doc.**

* Changes to checking for invalid pointers (`check_pointer`):
    * Check the pointer as described in the design doc, additionally using `pagedir_get_page` to check if the page is allocated
    * For buffers, also check every byte of the buffer to make sure it's valid and doesn't spill over to invalid memory
      
* Changes to implementing syscall `exec`:  
    * Addition of several different locks to various critical section that could have concurrency issues, from creating a thread to loading an executable to opening files, etc.

* Changes to implementing syscall `wait`:
    * Additional fields for the `thread` struct:
        * `bool has_parent`
        * `int parID`
        * `struct child_process_info *cpi`
        * `int num_cpi`
        
    * Changes to implementing `process_execute`:
        * Set child's `has_parent` to true when making the `child_process_info` struct, set it's parent ID, and give the child thread a pointer to it's corresponding struct for easier access
        * Initialize the struct's `exit_status` to 300 (which is an invalid status) instead of NULL because of type issues
        * Also, lock the section where we are modifying a thread's list `child_process_structs` to avoid race conditions
        * Increment parent thread's `num_cpi`
         
    * Changes to implementing `exit_handler`:
        * Check that a thread has a parent and that it's `cpi` pointer isn't null before trying to change a struct
        * Instead of iterating through a list of structs, use the thread's direct pointer to the relevant `child_process_struct`
         
    * Changes to implementing `kill` in userprog/exception.c:
         * Instead of repeating all the code in `exit_handler`, we just call `exit_handler(-1)`
         
    * Changes to implementing `process_wait`:
         * Again, if possible find a struct by finding the child and using it's pointer instead of iterating through lists of structs
         * If forced to iterate through the list of structs, the section is locked to avoid race conditions
         * When finished getting the exit status of the child, free the struct and decrease `num_cpi` by 1

### Major Differences (with Explanations) 

* One major change is the addition of locks to many important sections. We realized that we were having a lot of concurrency issues because of race conditions
    * For example, when loading an executable files are opened and closed, so we added locks to make sure you couldn't have two threads trying to open a file at the same time
    * As another example, we also added locks around the areas where a thread's `child_process_structs` list to make sure that it couldn't be modified at the same time a thread was iterating through it
    
* Another important change we made was freeing a `child_process_struct` after the corresponding child had been waited on
    * A thread can't be waited on twice, so the parent would never have to use that struct again
    * This way, when iterating through the list of structs we won't have to check a lot of useless structs
    * Also, this frees up memory for other child threads; if we kept all the old structs, then when we had to make new child processes and allocate the memory for their structs, we'd eventually run out of space
    
* As mentioned in our design review, when check validity of buffers we just went through it byte by byte instead of doing something more complicated with checking pages
    * Although slightly less efficient, it is much more straightforward and less likely to have implementation issues due to complexity
    
* Our last major change has to do with how threads find and change their struct when exiting
    * We added some checks to make sure the thread should be finding a struct and changing it; it makes to sense for the main thread to update anything when they exit because they don't have a parent, and if the parent already exited there's no point in updating the struct either
    * We also gave the child a direct pointer so they didn't have to deal with iterating through lists, as it would take longer and would be more complex to coordinate list access between many processes.

# **PART 3:**

### Final Implementation:
**Note: If not directly mentioned as being changed or taken out, we still used what was in our design doc.**

* Changes to `thread` struct in thread.h:
    * Added `struct file *files[130]`
* Changes to syscall.c:
	* Removed `int lowest_empty` from initial design
	* Removed `struct open_files` from initial design
	* Added various sanity checks for parameters in syscalls
		* Check that the file descriptor number passed in to syscalls fell between 0 and 130
		* Added checks for valid buffers before calling any handler; if not, called exit handler with status of -1
    * Added locks for open, exit, execute, load, and close, and initialized these locks along with our `filesys_lock` in `syscall_init`
* Changes to `open_handler` in syscall.c:
	* We store f at the lowest open index in the current thread’s `struct file *files[130]`
	* We have to iterate through the files array to find the smallest empty index
        * We don't check 0 and 1 in our file descriptors array because those are reserved for `stdin` and `stdout`, respectively
        
### Major Differences (with Explanations) 
* One major change is the removal of `struct open_files` because we realized that each process has its own file descriptor table so creating a variable in syscall.c didn’t make sense. Instead, we added it to the `thread` struct in thread.c, since each process in pintos has one thread, so each thread's file descriptors would be separate. 

* Another major change is the way we handled the file descriptors list. Instead of keeping an `int lowest_empty` to keep track of the lowest available file descriptor, we simply iterated through the whole `files` array to find the lowest available file descriptor. We realized that we couldn’t keep a `lowest_empty` variable because when we close a file descriptor, we wouldn’t always be closing a file descriptor at the beginning of the list, which would cause issues with properly keeping track of which file descriptors numbers were available. This change affected the runtime complexity of finding an available file descriptor but it was the simplest and cleanest solution. 

* Finally, we added a lot of bounds checking from our initial design. For example, for syscall handlers such as `open_handler` and `read_handler`, we check that the file descriptor is valid and falls between 0 and 130. Moreover, we realized that we should rigorously check if the buffers that we passed in for each syscall were valid pointers before even calling our syscall handlers, so we could gracefully call our `exit_handler` with -1 before executing anything else.
