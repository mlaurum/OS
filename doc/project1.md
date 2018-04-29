Design Document for Project 1: Threads
======================================

## Group Members

* Mike Lee <mike.lee@berkeley.edu>
* Jennie Chen <jenniechen@berkeley.edu>
* Michael Dong <michaeldong@berkeley.edu>
* Nathan Fong <nfong1996@berkeley.edu>

# **PART 1:**
### Data structures and functions:

* To add as global variable in timer.c:
    
    `static struct list sleeping_threads;`
    
    * List of sleeping threads, kept in sorted order by when to wake up
    
* To add in thread struct in thread.h:
    
    `int wake_value;`
    
    * Tick number at which thread should be woken up; `NULL` if not sleeping
    
* To add as global variable in thread.c:

    `static struct lock sleep_lock;`
    
    * Lock to use when modifying `sleeping_threads`

### Algorithms:

* Implementation of `timer_sleep`:
    * Check for zero/negative argument; if so, return immediately
    * Thread sets its own `wake_value` (global value `ticks` + time to sleep)
    * Thread acquires `sleep_lock`
    * Thread adds itself to `sleeping_threads`
    * Thread releases `sleep_lock`
    * Thread blocks itself
    
* Implementation of `timer_interrupt`:
    * Disable interrupts
    * Check thread at front of `sleeping_threads` (and make sure it's actually sleeping)
    * If it is not sleeping, move on to the next thread
    * Otherwise, if global value `ticks` is greater than or equal to that thread's `wake_value`, remove thread from `sleeping_threads` and unblock it.
    * Repeat until front thread's `wake_value` is greater than `ticks` or there are no more sleeping threads
    * Increment `ticks`, call `thread_tick` as usual (current code before modification) 
    * Enable interrupts
    
* Edit `thread_init` to initalize `sleep_lock` and edit `init_thread` to intialize `wake_value` to `NULL`
    
### Synchronization:

* Multiple threads may try to manipulate `sleeping_threads` at the same time
    * We prevent this by making sure that a thread has to lock `sleeping_threads` with `sleep_lock` before manipulating the list. They only release `sleep_lock` once they're finished, so another thread can't start manipulating the list until they're done.

* A thread may get interrupted after it adds itself to a list of sleeping threads but before it actually puts itself to sleep
    * We handle this by having the interrupt handler check if a thread is asleep before it unblocks it. If the thread is awake, the interrupt handler doesn't do anything to it. Then if it ever goes to sleep after that interrupt, the entry will still be on the list so that the interrupt handler can wake it up. This guarantees that any thread that goes to sleep will be woken up, but not before its intended wake-up time.
    
* The interrupt handler can't acquire locks, but it might be interrupted while it is accessing `sleeping_threads`
    * We address this by disabling interrupts when `timer_interrupt` begins and then re-enabling them when it is done going through and possibly changing `sleeping_threads`.
    
* Threads may be deallocated before we try to unblock them
    * This can't happen because we only try to unblock threads in `sleeping_threads` that are asleep. If a thread is asleep, it can't exit and deallocate itself, so we are guaranteed to only unblock threads that still are allocated.
   
### Rationale: 

* We considered using a condition variable to block the thread and then wake it up when the time is up. However, `cond_signal` cannot be called inside an interrupt handler because the handler can't acquire locks, so we decided to not use condition variables.

* This part should not take a significant amount of time to code up. The most time consuming portion of this part would be reading through the code and implementing the linked list in such a way that it doesn't cause synchronization issues, which we take care of with a lock. The rest of the changes in this part largely revolves around traversing and manipulating this linked list.

# **PART 2:**
### Data structures and functions:

* To add in thread struct in thread.h:

    `int base_priority;`
        
    * Keep track of thread's base priority
    
    `static struct list held_locks;`
    
    * List of all locks that this thread is currently holding
    
### Algorithms:

* Choosing next thread to run:
    * Edit `next_thread_to_run()` to just go through the entire `ready_list`, keeping track of which thread has the highest priority that it's seen so far. Once the entire list has been examined, return the thread with the highest priority

* Acquiring a lock (modifiying `lock_acquire`):
    * Disable interrupts
    * Check that lock exists
    * Check that thread doesn't already have the lock
    * Otherwise, try to acquire it (using `sema_down`)
        * Thread will put itself on list of waiting threads in `lock->semaphore`
    * Owner of that lock recomputes and sets its effective priority (`priority`)
    * Enable interrupts

* Releasing a lock (edits in `lock_release`):
    * Disable interrupts
    * Current (owner) thread releases the lock
    * Current thread recomputes and sets its effective priority (`priority`)
    * Current thread checks the thread at the front of `ready_list`; if their `priority` is higher than current thread's `priority`, current thread yields the CPU
    * Enable interrupts

* Computing effective priority:
    * For each lock that a thread currently holds, it looks at `priority` of each waiting thread for that lock
    * Thread's `priority` = `max(base_priority, [priorities of all waiting threads])`

* Priority scheduling for semaphores and locks (changes in `sema_up`):
    * Look through the semaphore's (and `lock->semaphore`'s) list of waiting threads to find the one with highest `priority`
    * Then use the found thread with highest priority as the next owner of the lock

* Priority scheduling for condition variables:
    * Similar to above; look through the condition variable's list of waiting threads to find the one with highest priority (in `cond_signal`)
    * Then `cond_signal` will call `sema_up` which can work as above

* Changing thread's priority:
    * Thread computes effective priority (see above)
    * Thread calls `thread_set_priority` to set its own priority to the computed priority
    
* Edit `init_thread` to initalize `base_priority` to priority input and `held_locks` to an empty list struct

### Synchronization:

* The process of acquiring or releasing locks could be interrupted and cause information to be inconsistent (i.e. between a thread's list of locks it owns and a lock's list of threads that are waiting)
    * We address this by disabling interrupts inside `lock_acquire` and `lock_release` so that this cannot happen. Also, locks are based on semaphores, and `sema_up` and `sema_down` also have interrupts disabled for actual priority scheduling and list manipulations, which prevents some synchronization issues.

* Other issues we thought about:
    * There are no shared resources that could have potential concurrent accesses except the locks, and all relevant methods that interact with locks (and semaphores, and condition variables) have interrupts disabled. Of course, it wouldn't make sense to use a lock to protect locks from concurrent accesses (you'd need another lock for that lock and so on) so this should be fine for preventing synchronization issues.
    * The only pointers to threads that we access are in `ready_list` and inside the locks. A thread can't exit and deallocate itself before it finishes with the lock, and if it exits it removes itself from `ready_list`, so we know that all pointers to threads we use will have threads that are still allocated at the end.
    
### Rationale: 

* We considered actually sorting `ready_list` every time we needed to find the next thread to run so that we could just pop off the front of the list. However, we realized that this really isn't faster than just looking through the entire list, and having list manipulations could actually result in synchronization issues since they are not thread-safe, so we decided it would be better to just traverse the entire list and check all the threads (which would actually be faster with a runtime of `O(n)` instead of `O(n * log n)` for sorting.

* We also considered sorting the list of waiting threads inside semaphores every time a thread's priority was changed to ensure that priority scheduling worked, but then we realized this wasn't necessary. We only care that we find the thread with the highest priority as the owner, so we can just look through the whole list which will take `O(n)` time with `n` threads waiting. Sorting would take `O(n * log n)` time and would involve re-sorting when priorities change, whereas in the linear traversal approach it doesn't matter if a thread's priority changes. The code will be slightly longer to write, but should still be pretty clear and won't be a major issue.

* This part should take a little longer to code up than the previous part. The most time consuming portion of this part would be implementing and debugging the priority donations. 


# **PART 3:**
### Data structures and functions:

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
    
### Algorithms:

* Functions to implement:
    * `int thread_get_load_avg(void)`
        * Return `load_avg` * 100, rounded
    * `int thread_get_recent_cpu(void)`
        * Return `recent_cpu` * 100 for running thread
    * `int thread_get_nice(void)`
        * Return `nice` for running thread
    * `void thread_set_nice(int new_nice)`
        * Set `nice` value of running thread to `new_nice`
        * Recalculate `priority` for running thread
        * Check against priority of whichever ready thread would run next; if it is now lower than the priority of next thread, yield the CPU
        
* Choosing next thread (edits made in `next_thread_to_run`)
    * Add in condition to only do below if MLFQS flag is enabled; otherwise, proceed as usual (given code)
    * Go through all threads in `ready_list`, keep tracking of the one with the highest `priority` that you've seen so far
    * In case of a tie of effective priorities, choose the one with the earliest `last_run` (our implementation of round robin selection)
    * After going through all threads on list, run whichever thread was found

* Disabling priority donation:
    * In `lock_acquire` and `lock_release`, disable recalculation of priorities for owner thread based on priority donators when the MLFQS flag is enabled
    * In `thread_set_priority`, ignore all input and return immediately if MLFQS flag is enabled

* Recalculations (all changes in `thread_tick`):
    * At every call to `thread_tick`, add one to `recent_cpu` for running thread
    * Update `last_run` value for running thread
    * If `ticks` is divisible by 4, recalculate all thread `priorities`
        * Use formula `priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)`
        * Restrict priorities between PRI_MIN and PRI_MAX
    * If `timer_ticks() % TIMER_FREQ == 0` (once per second):
        * Recalculate system `load_avg` with below formula
            * `load_avg = (59/60) * load_avg + (1/60) * ready_threads`
            * `ready_threads = length(ready_list) + 1`
        * Recalculate `recent_cpu` value for all threads (below formula)
            * `recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice`
    
* Other functions to edit:
    * In `thread_create`, if MLFQS flag is enabled ignore the priority argument and just calculate `priority` based on our formulas
    * In `thread_init`, initialize system `load_avg` to 0 and ensure that `initial_thread` starts with a `nice` value of 0
    * In `init_thread`, initialize `recent_cpu` to 0, add a `nice` argument to set the `nice` value, and initialize `last_run` to current `ticks` value
        
### Synchronization:

There should be no race conditions in this part. All recalculations are done inside `thread_tick`, which can't be interrupted because we called it from `timer_interrupt` which disables interrupts and only re-enables them after `thread_tick` has finished. The only shared data being accessed is `ready_list`, but no changes are made to it; we are only looking at the values of the linked list. There isn't really any shared data as the interrupt handler through `thread_ticks` is the only one who would change all of the relevant values for the MLFQS.

### Rationale: 

* We considered actually using 64 different lists as priority queues instead of one long one; however, we realized it would be difficult to figure out how much space to allocate for each queue, so either we would have extra space allocated and wasted or we'd have to check capacity and reallocate, both of which are not ideal. Since adding each thread to a priority queue one by one would take `O(n)` time anyway with `n` threads, along with possible extra time for reallocating memory and other checks, we decided to just do one long list that we could just look through (which would also take `O(n)` time, although we'd have to do it a little more often).

* Related to the above approach, we also considered implementing the round-robin rule for picking threads by having the 64 lists and then adding threads to the back of the list, so that when we pop off the front of the list we would be getting the least-recently used thread. However, we realized this had the potential for a lot of synchronization issues with list operations and since thread priorities can be updated and they can go to different queues, the front thread wouldn't necessarily be the least recently used queue depending on implementation. We decided that just going through all threads would be simpler to code and understand and likely cause less issues.

* This part should be relatively short. The vast majority of this part is by doing Mathmatics and most of these calculations and changes take place in a single function. 




