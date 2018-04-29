Final Report for Project 1: Threads
===================================

## Group Members

* Mike Lee <mike.lee@berkeley.edu>
* Jennie Chen <jenniechen@berkeley.edu>
* Michael Dong <michaeldong@berkeley.edu>
* Nathan Fong <nfong1996@berkeley.edu>

# Files Edited:
* timer.c
* synch.c
* synch.h
* thread.c
* thread.h

# **PART 1:**
### Initial Design - Data Structures:

* To add as global variable in timer.c:
    
    `static struct list sleeping_threads;`
    
    * List of sleeping threads, kept in sorted order by when to wake up
    
* To add in thread struct in thread.h:
    
    `int wake_value;`
    
    * Tick number at which thread should be woken up; `NULL` if not sleeping
    
* To add as global variable in thread.c:

    `static struct lock sleep_lock;`
    
    * Lock to use when modifying `sleeping_threads`
    
### Final Implementation:

* Added in thread struct in thread.h:

    `int wake_value;`
    
* Added to timer.c:
    
    `static struct list sleeping_threads;`
    
    `bool 
    compare_wake_times (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);`

* Modified:
    
    `void
    timer_init (void)`
        
    `void 
     timer_sleep (int64_t ticks)`
     
    `static void
    timer_interrupt (struct intr_frame *args UNUSED)`
    
    `static void 
    init_thread (struct thread *t, const char *name, int priority, int nice, fixed_point_t rcpu)`

       
### Implemented algorithms:

* The algorithm was implemented largely as originally planned. We made a few additional tweaks, such as the use of a comparator to insert into `sleeping_threads` in order. This addition made waking up threads significantly easier, as you could just start at the beginning of the list and stop as soon as you found any thread that had a later `wake_time`. The added functions also helped with code visibility and organization.

### Major differences and explanation of fixes: 

* The main idea that we implemented in each of the function mentioned above was largely the same. The one major difference would be that instead of creating and acquiring a lock on `sleeping_threads`, we simply disabled interupts before inserting the thread. 

 * This was due mainly to discussion with Michael during our design review; we had originally thought about interrupts but thought that the docstring of the `timer_sleep` function meant that wasn't allowed. Michael cleared this up and told us it was fine. 
    
  * We also discussed how using locks could go wrong, as if the thread holding the lock was interrupted, the interrupt handler would run, and since it would need to access the list `sleeping_threads` it would also need the lock, which could lead to a lot of issues. 
    
* In addition, we blocked the thread inside of the critical zone instead of outside it. Apart from this and the added comparator for the ordered list, the implmentation is largely the same.


# **PART 2:**

### Initial Design - Data Structures:

* To add in thread struct in thread.h:

    `int base_priority;`
        
    * Keep track of thread's base priority
    
    `static struct list held_locks;`
    
    * List of all locks that this thread is currently holding
    
### Final Implementation:

* Added in thread struct in thread.h:

    `int base_priority;`
    
    `static struct list held_locks;`
    
    `struct lock *waiting_lock;`
    
    * Lock that the thread is waiting for; NULL if not applicable
    
    `struct list_elem lock_elem;`
    
    * List_elem to use inside a lock's list of waiting threads
    
    `struct list_elem sleep_elem;`
    
    * Modified from the original `elem`; used in `ready_list` and `sleeping_threads` lists of threads
    
* Added in lock struct in synch.h:

    `struct list_elem elem;`
    
    * List_elem to be used inside a thread's list of `held_locks`
    
    `int max_priority;`
    
    * Highest priority of all waiting threads for the lock

* Changes in thread.c:

    * Added:
    
        `struct thread * 
        find_max_priority_waiting (struct list *lst)`
        
        * Finds the thread in a list of threads with the highest priority using `lock_elem`. Used to choose next thread to
        acquire a lock or semaphore.

        `struct thread * 
        find_max_priority_sleep (struct list *lst)`
        
        *  Finds the thread in a list of threads with the highest priority using `sleep_elem`. Used to choose next thread to run.
        Implements round-robin scheduling.
                
        `void 
        thread_check_yield (void) `
        
        * Checks if thread should yield to some thread with higher priority. Yields thread if necessary.
        
    * Modified:
        
         `void 
         thread_init (void)`
         
         `static void 
         init_thread (struct thread *t, const char *name, int priority, int nice, fixed_point_t rcpu)`
         
         `tid_t
         thread_create (const char *name, int priority, thread_func *function, void *aux)`
         
         * Added check to see if current running thread should yield to thread just created.
         
         `void 
         thread_unblock (struct thread *t)`
         
         `void 
         thread_yield (void)`
         
         `static struct thread * 
         next_thread_to_run (void)`
         
         * Changed to choose next thread to run according to highest priority thread in `ready_list`.

         `void 
         thread_set_priority (int new_priority)`
         
         * Changes a thread's base priority. Involves a check of the thread's held locks to see if this change actually affects it's priority or if a waiting thread's priority donation takes precedence. Afterwards, checks if running thread should yield the CPU.

* Changes in synch.c:

    * Added:
    
        `void 
        lock_compute_priority (struct lock *lock)`
        
        * Recursively deals with priority donation chains. For the holder of a lock, sets its priority based on the lock's `max_priority` and the thread's own priority. If the lock holder is waiting for a lock, then this is recursively called on that lock to carry through any possible changes due to the thread's priority change.

        `bool 
        cmp_cond_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) `
        
        * Comparator function; used with `list_sort` in order to correctly sort the condition variables. 

    * Modified:

        `void 
        sema_down (struct semaphore *sema)` 
        
        * Implemented priority donation; when a thread is added to the waiting list of threads, the priority of the lock holder is recomputed and then `lock_compute_priority` is called recursively to carry through priority donation chains.

        `void 
        sema_up (struct semaphore *sema)` 
        
        * Used `find_max_priority_waiting` to choose the waiting thread with the highest priority to receive the lock next. Added a check to see if current thread should yield the CPU.
        
        `void
        lock_init (struct lock *lock)`
        
        `void 
        lock_acquire (struct lock *lock)`
        
        * Set a thread's `waiting_lock` value accordingly. Add the lock to the thread's list of `held_locks` and update the thread's priority.

        `bool 
        lock_release (struct lock *lock)` 
        
        * In addition to just releasing the lock, recalculate the thread's priority using its `base_priority` and whatever locks it may still hold. Added a check to see if current thread should yield the CPU.

        `void 
        cond_signal (struct condition *cond, struct lock *lock UNUSED)`
        
        * Used `cmp_cond_priority` to sort the condition variable's list of waiters by priority so that the one with highest priority will be woken up first.

### Implemented algorithms:

* Choosing next thread to run:
    * Use `find_max_priority_sleep` to find the ready thread with highest priority to run.

* Acquiring a lock (modifiying `lock_acquire`):
    * Disable interrupts
    * Set the current thread's `waiting_lock` value
    * Otherwise, call `sema_down` on the lock's semaphore
        * If necessary, thread will put itself on list of waiting threads in `lock->semaphore` and donate its priority to the lock's holder
    * When the thread receives the lock, reset its `waiting_lock` value to NULL.
    * Add the lock to the thread's list of `held_locks`
    * Thread recomputes its `priority`
    * Re-enable interrupts

* Releasing a lock (modifying in `lock_release`):
    * Disable interrupts
    * Thread releases the lock by setting the lock's holder to NULL and calling `sema_up`
        * `sema_up` will give the lock to the waiting thread with highest priority and then check if current thread needs to yield
    * Thread recomputes its `priority`
    * Thread checks if it needs to yield the CPU after its priority has been recalculated
    * Enable interrupts

* Computing effective priority of a thread:
    * A thread's priority starts at its `base_priority`
    * For each lock that a thread currently holds, look at its `max_priority` value
        * If it is larger than the thread's current priority, update the thread's current priority
        
* Priority scheduling for semaphores and locks (changes in `sema_up`):
    * Semaphores and locks will wake up waiting threads in order of `priority`.

* Priority scheduling for condition variables:
    * Similar to above; a condition variable's list of waiting threads is sorted by `priority` of the threads.
    
### Major differences and explanation of fixes: 

* Dealing with priority donation in general:

    * We initially had this idea of just iterating through a semaphore's list of waiting threads in order to figure out priority donation and how much the lock holder's priority should change by. 
    
    * When coding this implementation, we realized that due to a lot of required list operations it was very easy to be careless and mess up pointers, leading to a lot of kernel panics. 
    
    * We decided to instead give each lock a `max_priority` value, minimizing the amount of list operations we had to do and also making it more efficient for a thread to compute it's priority, as instead of iterating through many lists it just had to look at the value for each lock it held. 


* Dealing with priority donation chains:

    * We realized during our design doc review with Michael that although we had talked about priority donation chains and knew that they needed to happen, we hadn't actually accounted for it in our document. 
    
    * To fix this, we gave each thread a `waiting_lock` value; we used this in our implementation to follow the chain of priority donations. 
    
    * For example, if a chain of locks looked like A <- B <- C where the holder of B was waiting on lock A and the holder of C was waiting on lock B, then if a thread with a new lock D decided to wait for lock C, then we could follow the `waiting_lock` attribute of the threads from the holder of C to the holder of B to the holder of A and update all of their priorities in turn.

* Working with `list_elem`s:

    * We had the idea of giving each thread a list of `held_locks`, but we hadn't realized that this meant locks would need an `elem` in order to be in that list. In our final implementation, all locks now have a `list_elem` for this purpose.
    
    * We also realized that threads were being placed both on sleeping/ready lists of threads, but also on waiting lists for locks. To separate these two processes and avoid risks of overusing the `list_elem` and possibly creating pointer issues, we created two separate ones for each purpose.


# **PART 3:**
### Initial Design - Data Structures:

* To add in thread struct in thread.h:

    `fixed_point_t nice;`
        
    * Keep track of thread's nice value
    
    `fixed_point_t recent_cpu`
    
    * Keep track of thread's recent CPU time
    
    `int last_run`
    
    * Tick number at which thread was last running
    
* To add as global variable in thread.c:

    `fixed_point_t load_avg`
    
    * Keep track of system's load average
    
### Final Implementation:

* Added to thread struct in thread.h:
    
    `int nice;`
      
    `fixed_point_t recent_cpu`
    
    `int last_run`
    
* Added as global variable in thread.c:
   
    `fixed_point_t load_avg`

* Added and implemented in thread.c:
    
    `void mlfqs_priority(struct thread *t, void *aux UNUSED) `

    `void update_load_avg(struct thread *t) `
    
    `void update_recent_cpu(struct thread *t, void *aux UNUSED) `
    
    `void thread_set_nice (int new_nice)`

    `int thread_get_recent_cpu (void)`

    `int thread_get_load_avg (void)`

    `int thread_get_nice (void)`
    
* Modified:

    `void thread_init (void)`

    `void thread_tick (void)`

    `static void init_thread (struct thread *t, const char *name, int priority, int nice, fixed_point_t rcpu)`
        
### Implemented algorithms:

* Thread calculations are made in `thread_tick` largely according to our original design. We recalculate thread priorities every time slice, and we recalculate the system `load_avg` and all thread `recent_cpu` values every second. We also disabled priority donations by only allowing them to occur if `thread_mlfqs` was `false`; that is, that the OS wasn't running in MLFQS mode.

* One minor thing that we did change from the initial plans were additions of helper functions that made the code significantly more manageable and easy to read; `mlfqs_priority`, `update_load_avg`, and `update_recent_cpu` were all added in order to help recompute the priority as needed.

* We also made the decision to only recalculate `priority` for the running thread at every time slice instead of every single thread. This was to be more efficient, as only the running thread could have a changed `recent_cpu` value that could affect it's priority calculation (assuming we hadn't done a mass recalculation at the beginning of the second).

### Major differences and explanation of fixes: 

* There are no major differences from our initial design spec. High level ideas and implementations are pretty much the same. 

* We had a few minor type changes from the planned variables (`nice` as an int).

* Some minor changes include the addition of helper functions, which were used in order to perform mathematic operations of fixed point numbers cleanly. Some tricky things to account for were the round-robin scheduling with each thread's `last_run` value, which interfered with a few functions when we were implementing our formulas and priority updates.



