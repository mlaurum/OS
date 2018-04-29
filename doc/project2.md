Design Document for Project 2: User Programs
============================================

## Group Members

* Mike Lee <mike.lee@berkeley.edu>
* Jennie Chen <jenniechen@berkeley.edu>
* Michael Dong <michaeldong@berkeley.edu>
* Nathan Fong <nfong1996@berkeley.edu>

# **PART 1:**
### Data structures and functions:

No new data structures or functions will be added.

We will modify the following functions in userprog/process.c:

* `tid_t process_execute (const char *file_name)`
    * Creating thread with correct program name
   
* `bool load (const char *file_name, void (**eip) (void), void **esp)`
    * Dealing with program name as above, providing arguments to `setup_stack`
    
* `static bool setup_stack(void **esp, char* args)`
    * Adding parameter to take in program arguments
    * Placing arguments, argument addresses, etc. on the stack
    
### Algorithms:

* In `process_execute`:
    * Copy the input argument string (already in the skeleton code)
    * Use `strtok` to get out the program file name from the string of all arguments (everything before first space)
    * Use that name as the first argument to `thread_create`, keeping everything else the same
* In `load`:
    * Make a copy of `file_name` to pass into `setup_stack` since `strtok` is destructive
    * As above, get the first argument (the program name) and use to open/otherwise deal with executable file
    * Call `setup_stack` with additional parameter of copied `file_name`
* In `setup_stack`:
    * Get and install page in user virtual memory (from skeleton code)
    * Using `strtok` to iterate through the argument string
        * For each token, push the argument onto the stack and keep track of the addresses of each one
    * Pad stack to round it down to nearest multiple of 4 bytes for word-aligned accesses
    * Add a null pointer sentinel to the stack
    * Then, push the address of each argument in reverse order (last argument goes on stack first so first argument is the lowest in stack) - these are elements of `argv`
    * Push `argv` (pointer to address of first argument) and `argc`
    * Push fake return address
    
### Synchronization:

   * There are no real synchronization errors. Each thread would only put arguments and other data on its own stack, so we'd never run into an issue where threads are messing with each others' stacks and causing unexpected behavior.
   * There are no shared resources between threads.
   
### Rationale: 

   * We considered adding a whole new parameter to `load` in order to pass along the program arguments; however, we realized that we could just pass it along as written and then just use `strtok` to get out the filename which is guaranteed to be the first argument. This way, we can keep function calls shorter which could help with readability.
   
   * Overall, this section will have very little to code. The main change involved is the process of pushing things onto the stack, but it will the same pattern of updating pointers, just repeated multiple times.
   
   
# **PART 2:**
### Data structures and functions:

**Data Structures:**

* Adding in `thread.h`:
   
     ```
      struct child_process_info {
         pid_t pid;                          /* Thread identifier */
         int exit_status;                    /* Exit status of child thread; NULL if thread has not exited */
         boolean waited_on;                  /* Identifies whether parent has waited on this child thread before */
      }  
     ```

* Adding in `thread` struct in threads/thread.h:
      
   * `struct list_elem parent_elem`
      * `list_elem` of the parent of this thread
   
   * `struct list child_process_structs`
      * List of `child_process_info` structs for each child thread
      
   * `struct semaphore wait_sema`
      * Semaphore for parent to use when waiting for a child process
      
   * `struct semaphore load_sema`
      * Semaphore to use to make sure executable is loaded (or fails loading) before returning from `process_execute`
      
* Modifying userprog/process.c:

   * Remove `static struct semaphore temporary` and all references to it
   
   * Adding in:
   
      ```
      struct arg_struct {
         void *file_name;                    /* Original argument passed in to `process_execute` */
         boolean *loaded;                    /* Boolean pointer to modify so we know if process was loaded */
      }
      ```
      
**Functions:**

* Modifying `init_thread`:
   
   * Initialize all of the new members of the `thread` struct listed above (with semaphores initialized to 0)
   
* Adding in userprog/syscall.c:

   * `void exit_handler (int status)`
      * Moving into a helper function for consistency (will do the same thing)
   
   * `void halt_handler (void)`
      * Helper function to be called from `syscall_handler` to deal with `SYS_HALT`
   
   * `pid_t exec_handler (const char *cmd_line)`
      * Helper function to be called from `syscall_handler` to deal with `SYS_EXEC`

   * `int wait_handler (pid_t pid)`
      * Helper function to be called from `syscall_handler` to deal with `SYS_WAIT`

   * `int practice_handler (int i)`
      * Helper function to be called from `syscall_handler` to deal with `SYS_PRACTICE`
      
   * `void check_pointer (const void *vaddr)`
      * Checks for validity of input pointer

### Algorithms:

* Checking for invalid pointers (`check_pointer`):
   * Use functions from vaddr.c to determine whether pointer is valid
      * Check for `NULL` pointer
      * Use `is_user_vaddr` to check if pointer has a valid user virtual address
      * Check that pointer is above the unmapped memory region (0x8048000)
      * Check that no part of the pointer will spill over to invalid memory (ex: 2 bytes valid, 2 bytes invalid)
   * If pointer is not valid, exit the user process
      
* Change to `syscall_handler`:
   * Check if stack pointer `f->esp` is valid before doing anything
   * For each syscall, find the relevant arguments in the stack, and call the appropriate handler below

* Implementing syscall `halt` (changes in new `halt_handler`):
   * Use `shutdown_power_off()` from src/devices/shutdown.c to halt the system. 

* Implementing syscall `exec`:   

   * Modify `exec_handler`:
      * Check that input pointer `cmd_line` is valid
      * Pass `cmd_line` into `process_execute` in userprog/process.c
   * Modify `process_execute`:
      * Create an `arg_struct` out of passed in argument `file_name` and a new boolean pointer
      * Pass in this struct into `start_process`
      * If it creates a thread successfully to run the program with `thread_create`, we call `sema_down` on the `load_sema` of that new thread
      * If either the thread was created unsuccessfully or we check the boolean pointer and the executable wasn't loaded, we return -1
      * Otherwise we return the `tid` of the created process that just loaded the executable
   * Modify `start_process` in userprog/process.c:
      * Have this function take in a struct instead of just `void *file_name_`
      * Extract `file_name` and `loaded` out of the struct for use in the rest of the function
      * After calling `load`, we know whether or not the load was successful
      * Update the value of `loaded` to reflect this
      * Call `sema_up` on the `load_sema` of the running thread at the end of the function to say that the `load` function finished
   * `process_execute` will return the `tid_t` of the thread running the program, or -1 if the thread cannot be created or the program wasn't loaded
      * We will use a one-to-one mapping of `tid_t` to `pid_t` as there will never be one process with the same `pid_t` run on multiple threads
      * Therefore, `exec_handler` can just return the result of `process_execute`

* Implementing syscall `wait`:
     * `wait_handler` will just call `process_wait`
     
     * Modifications to `process_execute`:
         * After creating a child process, if the thread was created successfully, create a `child_process_info` struct for that thread
         * `waited_on` should be initialized to False, `exit_status` to `NULL`, and `pid` to the thread's `tid`
         
     * Modifications to `exit_handler`:
         * When a process is exiting, access the parent thread using `parent_elem`
         * Find the `child_process_info` struct corresponding to itself and change the `exit_status` in the struct to match the one passed in to the handler
            * This can happen in `process_exit`, which gets called
         * Also make sure to up the parent's `wait_sema` so it knows the process has exited
         
     * Modifications to `kill` in userprog/exception.c:
         * import userprog/process.h
         * If kernel kills the process, do the same as above except change the `exit_status` to -1
         * Also, up the parent's `wait_sema` so the parent thread knows the process was killed
         * Then continue as usual
         
     * Modifications to `process_wait`:
         * Check if process is a child (if it has a `child_process_info` struct)
            * if so, return -1
         * Check if that process has already been waited on (look for `child_process_info` struct and check `waited_on`)
            * if so, return -1
         * While the `exit_status` value for that child is NULL:
            * call `sema_down` on its `wait_sema`
            * when some child process exits or dies, it will call `sema_up` on the semaphore
            * because of the while loop, the parent process will check if that child process was the one it was waiting for; if not, it will go back to sleep
         * Once we have the `exit_status` for that child, parent process can change `waited_on` to True and then return the `exit_status`.

* Implementing syscall `practice` (changes in new `practice_handler`):
   * `practice_handler` will return `i + 1` (`i` is the input, found by `syscall_handler` in the stack as `args[1]`)

### Synchronization:
   
   * The main synchronization issues in this part are related to the inherent behavior of some of the syscalls such as `wait` and `exec`.
   
   * `wait`:
      * The main synchronization issue was making sure that the parent process would wait until the specific child process it was waiting for finished.
      * For this to work, we had to account for the fact that the child process could exit either before the parent decided to wait or after.
      * Therefore, we gave every thread a semaphore `wait_sema` that its children could use to let the parent know that it had exited.
      * The parent would check if the child process had already exited (if it had an `exit_status` in the struct for child process info).
      * Otherwise, the parent would both wait for the child to `sema_up` to alert it to wake up, and also check when it woke up to see if it was the specific child it was waiting for, as other child threads could also exit.
      * This way, the parent will always know the exit status of its child (even though it might have to wait), and it will never wake up and stop waiting for an incorrect child.
   
   * `exec`:
      * The main synchronization issue here was ensuring that the parent process doesn't return from `exec` before finding out if the child process could properly load the executable.
      * Although `process_execute` calls `thread_create` and tells it to execute `start_process`, there was no guarantee that this would happen before `process_execute` returned.
      * To deal with this, we created a semaphore for each process thread; `process_execute` calls `sema_down` on this `load_sema`, and since `sema_up` is only called on this semaphore and after `load` returns in `start_process`, then we know that by the time `process_execute` returns to `exec` and allows that to return, the child thread has had a chance to try and load the executable.
      * We also needed to know whether or not loading actually worked, so instead of just passing in one string to `start_process`, we also gave it a boolean pointer so that it could change this to reflect the possible success of `load` (since `start_process` could just exit the thread and remove all state pertaining to it, but this pointer can still be accessed by the parent thread)
      * `process_execute` can then access this boolean and figure out what to give back to `exec` (in terms of whether or not loading the executable was successful)
      
   * Other potential problems:
      * We are using the same `parent_elem` for multiple children
         * This is fine because the child never actually changes or affects `parent_elem` in any way
      * Multiple threads have access to one parent thread's `child_process_structs` list
         * This is okay because changes to each part of the list is limited to only one thread (or two, but never at the same time)
         * Only the parent thread who the list belongs to will ever add structs to the list
         * Each struct will only ever be modified by the corresponding child and the parent, but the parent would only ever modify it (by modifying `waited_on`) after the child thread has exited, so they cannot possibly both modify it at the same time
    
### Rationale: 

   * We decided to completely get rid of the `temporary` semaphore because every single time a process was executed, it would re-initialize the `temporary` semaphore, making it pretty useless for all other threads who were using it. It was much better to have every parent thread have its own semaphore because no one other than the parent and its children would use it, so you'd never run into issues like unrelated threads affecting it and messing up the waiting process.
   
   * We also decided to use semaphores instead of just having the parent process wait until the `exit_status` it was looking at wasn't `NULL` because we wanted to avoid busy waiting as much as possible; this implementation makes it possible for the parent thread to go to sleep for a while, because the child threads will wake it up when they exit.
   
   * We choose to compartmentalize the syscalls into their own handlers to improve readability and also make it easier to change smaller pieces of the `syscall_handler`. We felt that being able to have each handler take care of the complicated stuff and then having the `syscall_handler` do some checks and then just call the handler made it easier to focus on each piece separately; one piece being ensuring that we call the appropriate handler, and the other piece being implementing that specific handler correctly. This is the same reason why we created a helper function to check if pointers are valid so that we won't have to repeat the code many times.
   
   * On a related note to above, we are currently considering whether or not we can just check all pointers in `syscall_handler`; this would make it so that we won't have to repeat the check in each separate handler.
   
   * This part will be much trickier and definitely take longer than part 1, as we will have to be very careful with the various semaphores and synchronization issues to ensure that we don't get any unexpected behavior. We will also have to be very careful with the various structs we are defining and making sure that they are updated appropriately.
  
  
# **PART 3:**
### Data structures and functions:

**Data Structures:**

* Adding to thread.c:

   ```
   struct fd_saver {
      int fd;                                         /* A fd for an opened file */
      struct list_elem fd_elem;                       /* list_elem so this struct can be stored in a list */
   }
   ```
   
* Adding to `thread` struct:
   
   * `struct file *file`
      * Allows thread to allow write to file when done running it
      * Will need to modify `init_thread` to initialize it and `load` to update this pointer when file struct is opened
   
   * `struct list opened_fds`
      * Keep track of what file descriptors have been opened with this process; a list of `fd_saver` structs

* Adding in userprog/syscall.c:

   * `static struct lock filesys_lock`
      * Global lock on filesystem operations for thread safety
      
   * `static struct lock fd_lock`
      * Lock on the array of file structs
      
   * `struct open_files[128]`
      * Array of file structs; index + 2 = the file descriptor (avoid 0 and 1)
   
   * `int lowest_empty`
      * index of lowest spot in `open_files` still empty 

**Functions:**

* Adding in userprog/syscall.c:

   * `bool create_handler (const char *file, unsigned initial_size)`
      * Helper function to be called from `syscall_handler` to deal with `SYS_CREATE`
   
   * `bool remove_handler (const char *file)`
      * Helper function to be called from `syscall_handler` to deal with `SYS_REMOVE`
   
   * `int open_handler (const char *file)`
      * Helper function to be called from `syscall_handler` to deal with `SYS_OPEN`

   * `int filesize_handler (int fd)`
      * Helper function to be called from `syscall_handler` to deal with `SYS_FILESIZE`

   * `int read_handler (int fd, void *buffer, unsigned size)`
      * Helper function to be called from `syscall_handler` to deal with `SYS_READ`
      
   * `int write_handler (int fd, const void *buffer, unsigned size)`
      * Helper function to be called from `syscall_handler` to deal with `SYS_WRITE`
   
   * `void seek_handler (int fd, unsigned position)`
      * Helper function to be called from `syscall_handler` to deal with `SYS_SEEK`

   * `unsigned tell_handler (int fd)`
      * Helper function to be called from `syscall_handler` to deal with `SYS_TELL`

   * `void close_handler (int fd)`
      * Helper function to be called from `syscall_handler` to deal with `SYS_CLOSE`
    
### Algorithms:

* Modifying `syscall_init`:
   * Initialize `open_files` to start as all `NULL` values
   * Initialize a file system with `filesys_init`
   
* In `syscall_handler` (not a new change, done in part 2 - just a reminder):
   * Check if stack pointer `f->esp` is valid before doing anything
   * For each syscall, find the relevant arguments in the stack, and call the appropriate handler
   
* Implementing syscall `create`:
   * Check that `file` is a valid pointer
   * Check that `file` is not longer than 14 characters
   * Acquire `filesys_lock`
   * Call `filesys_create` with corresponding arguments passed into `create_handler`
   * Release `filesys_lock`
   * Return result of calling `filesys_create`

* Implementing syscall `remove`:
   * Check that `file` is a valid pointer
   * Acquire `filesys_lock`
   * Call `filesys_remove` with corresponding arguments passed into `remove_handler`
   * Release `filesys_lock`
   * Return result of calling `filesys_remove`

* Implementing syscall `open`:
   * Check that `file` is a valid pointer
   * Acquire `filesys_lock`
   * Call `filesys_open` with corresponding arguments passed into `open_handler`
   * Release `filesys_lock`
   * If `filesys_open` returns NULL, return -1; otherwise, let `f` be the file struct returned by `filesys_open`
   * Acquire `fd_lock`
   * Store `f` at the index of `open_files` referred to be `lowest_empty`
   * Then, increment `lowest_empty` and check the array until you find an empty spot (value is `NULL` in the array)
   * Release `fd_lock`
   * Create an `fd_saver` struct with the index where you stored `f` + 2 and store it in the current thread's `opened_fds` list
   * Return the index where you stored `f` + 2

* Implementing syscall `filesize`:
   * Find the file struct `f` at index `fd - 2` of the array `open_files`
   * Acquire `filesys_lock`
   * Call `file_length(f)`
   * Release `filesys_lock`
   * Return the result of calling `file_length`

* Implementing syscall `read`:
   * Check that `buffer` is a valid pointer
   * Check that `fd` is not invalid (1 or not referring to an opened file)
   * If `fd` is not 0:
      * Find the file struct `f` at index `fd - 2` of the array `open_files`
      * Acquire `filesys_lock`
      * Call `file_read(f, buffer, size)`
      * Release `filesys_lock`
      * Return the result of calling `file_read`
   * Otherwise:
      * Initialize the `buffer` of input.c
      * Call `input_getc` from src/devices/input.c in a while loop `size` times
      * Then copy the keys from the `buffer` in input.c to the `buffer` that was passed in
      * Return `size`

* Implementing syscall `write`:
   * Check that `buffer` is a valid pointer
   * Check that `fd` is not invalid (0 or not referring to an opened file)
   * If `fd` is not 1:
      * Find the file struct `f` at index `fd - 2` of the array `open_files`
      * Acquire `filesys_lock`
      * Call `file_write(f, buffer, size)`
      * Release `filesys_lock`
      * Return the result of calling `file_write`
   * Otherwise:
      * Create a counter `c` and initialize to 0
      * Create an empty buffer `b` of size 512 bytes
      * Use strlcpy to copy the first section of `buffer` into `b`; strlcpy will tell us that it wrote `n` bytes
      * Increase `c` by `n` and call `putbuf(b)` (from lib/kernel/console.c)
      * Move the pointer for `buffer` `n` bytes forward (`n` bytes into `buffer`) and repeat above process until there is no more to write to console
      * Return `c`

* Implementing syscall `seek`:
   * Check that `fd` is not invalid (0 or not referring to an opened file)
   * Find the file struct `f` at index `fd - 2` of the array `open_files`
   * Acquire `filesys_lock`
   * Call `file_seek(f, position)`
   * Release `filesys_lock`
   
* Implementing syscall `tell`:
   * Check that `fd` is not invalid (0 or not referring to an opened file)
   * Find the file struct `f` at index `fd - 2` of the array `open_files`
   * Acquire `filesys_lock`
   * Call `file_tell(f)`
   * Release `filesys_lock`
   * Return result of calling `file_tell`
   
* Implementing syscall `close`:
   * Check that `fd` is not invalid (0 or not referring to an opened file)
   * Find the file struct `f` at index `fd - 2` of the array `open_files`
   * Acquire `fd_lock`
   * Set the value of `open_files` at index `fd - 2` to `NULL`
   * If that index `fd - 2` is less than `lowest_empty`, set `lowest_empty` to `fd - 2`
   * Release `fd_lock`
   * Acquire `filesys_lock`
   * Call `file_close(f)`
   * Release `filesys_lock`
      
* Modify `load`:
   * Remove the call to `file_close` at the end of the function
   * Call `file_deny_write` on the executable to ensure that no one can modify it while it is running
   
* Modify `process_exit`:
   * Call `file_allow_write` on the `file` belonging to the thread running this file
   * When exiting, go through each struct in `opened_fds`; for each one, call `close_handler` on the `fd` in the struct to close it
   
* Follow above procedure as well when killing a process in exception.c
   
### Synchronization:

   * The main synchronization issue we are concerned with is the possibility of multiple threads trying to use file system operations at once. To deal with this, we use `filesys_lock` any time any file system operation is called and only release the lock when the operation is done.
  
   * We also ensured that a running executable can't be modified by calling `file_deny_write` when a thread runs the file and only calling `file_allow_write` again when the thread is about to exit. This way, the executable file can only be modified when every single thread running it has exited or is about to exit.
  
   * Other shared state includes the array `open_files` (and also the int `lowest_empty`). There was the possibility that multiple threads could try to add a file struct to the array and once and possibly end up with the same file descriptor for separate file structs. To fix this, we used `fd_lock`; anytime the array (and thus the int) would be changed, the thread has to acquire the lock first, and it only releases the lock when it is done making changes. This way, the above issue can never happen.
  
   * We will never run into an issue where a thread's `opened_fds` list has synchronization issues because that thread is the only one who will ever modify that list.

### Rationale: 

   * This part will mostly take longer not because of difficulty, but because there are many different syscalls to account for. Most of the difficult work is already taken care of with the given file system and its operations.
   
   * We initially had several different ideas for implementing file descriptors. We first considered using a linked list of file structs, but then we realized that this was slow to iterate through to find a certain `fd` and would also require modification of the struct to include a `fd` and a `list_elem`, which was not recommended. We then considered using an array because of faster accessing of file structs (with constant-time lookup corresponding to indices) but we had originally thought of having to reallocate the array every time we added a struct, which we thought would be very inefficient. Then we realized that we could assume that there would be at most 128 open files at a time, so we decided to start out by initializing an array of size 128. This way, we might have a little bit of wasted space, but accesses are fast and we won't have to be slowed down by reallocation.
   
   * Since every thread has to close it's opened `fd`s upon exiting, we decided that it was best to make a separate struct to store the `fd` that was opened; this way, we could also include a `list_elem` and use the given list implementation. We considered having each thread keep an array of its opened `fd`s, but we thought that we didn't want to deal with reallocation if a thread kept opening files, but we also didn't want to deal with a lot of wasted space for every single thread (because 128 total opened files doesn't tell us anything about the distribution of open files among different threads).


