#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"

void syscall_init (void);

struct wrapper
  {
    bool is_dir;
    struct file *file;
    struct dir *dir;
  };

void exit_handler (int status);
int open_handler (const char *file);
int filesize_handler (int fd);
int read_handler (int fd, void *buffer, unsigned size);
int write_handler (int fd, const void *buffer, unsigned size);
void seek_handler (int fd, unsigned position);
unsigned tell_handler (int fd);
void close_handler (int fd);

void halt_handler (void);
int exec_handler (const char *cmd_line);
int wait_handler (pid_t pid);
int practice_handler (int status);
int cacheh_handler (void);
int cachem_handler (void);
unsigned long long blockr_handler (void);
unsigned long long blockw_handler (void);
void cacheclear_handler(void);

bool chdir_handler (const char *dir);
bool mkdir_handler (const char *dir);
bool readdir_handler (int fd, char *name);
bool isdir_handler (int fd);
int inumber_handler (int fd);

int last_occurrence (const char *str, char desired);
#endif /* userprog/syscall.h */
