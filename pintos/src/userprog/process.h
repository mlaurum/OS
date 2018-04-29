#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct lock open_lock;
struct lock exit_lock;
struct lock close_lock;
struct lock load_lock;
struct lock execute_lock;

struct arg_struct
  {
    void *file_name;
    bool loaded;
  };

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);



#endif /* userprog/process.h */
