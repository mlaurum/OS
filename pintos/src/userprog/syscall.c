#include "userprog/syscall.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <string.h>
#include "userprog/pagedir.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"

static void syscall_handler (struct intr_frame *);

bool create_handler (const char *file, unsigned initial_size);
bool remove_handler (const char *file); 

static void check_pointer (const void *vaddr, int buffer_size);
static void check_buffer (const void *vaddr, int buffer_size);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&open_lock);
  lock_init (&exit_lock);
  lock_init (&execute_lock);
  lock_init (&load_lock);
  lock_init (&close_lock);
  cache_init ();
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);
  check_pointer (args, -1);

  if (args[0] == SYS_EXIT) 
    {
      if (!is_user_vaddr (args[1]))
        exit_handler (-1);
      f->eax = args[1];
      exit_handler (args[1]);
    }
  if (args[0] == SYS_PRACTICE)
    f->eax = practice_handler (args[1]);
  if (args[0] == SYS_WRITE) 
    {
      check_pointer (args[2], args[3]);
    	f->eax = write_handler (args[1], args[2], args[3]);
    }
  if (args[0] == SYS_CREATE) 
    {
      check_pointer (args[1], args[2]);
      f->eax = create_handler (args[1], args[2]);
    }
  if (args[0] == SYS_REMOVE)
    {
      if (!is_user_vaddr (args[1]))
        exit_handler (-1);
      f->eax = remove_handler (args[1]);
    }
  if (args[0] == SYS_OPEN) 
    {
      if (!is_user_vaddr (args[1]))
        exit_handler (-1);
      f->eax = open_handler (args[1]);
    }
  if (args[0] == SYS_FILESIZE)
    f->eax = filesize_handler (args[1]);
  if (args[0] == SYS_READ) 
    {
      check_pointer (args[2], args[3]);
      f->eax = read_handler (args[1], args[2], args[3]);
    } 
  if (args[0] == SYS_SEEK)
    seek_handler (args[1], args[2]);
  if (args[0] == SYS_TELL)
    f->eax = tell_handler (args[1]);
  if (args[0] == SYS_CLOSE)
    close_handler (args[1]);
  if (args[0] == SYS_HALT) 
    halt_handler ();
  if (args[0] == SYS_EXEC) 
    f->eax = exec_handler (args[1]);
  if (args[0] == SYS_WAIT) 
    f->eax = wait_handler (args[1]);
  if (args[0] == SYS_PRACTICE) 
    f->eax = practice_handler (args[1]);
  if (args[0] == SYS_CACHEH)
    f->eax = cacheh_handler ();
  if (args[0] == SYS_CACHEM)
    f->eax = cachem_handler ();
  if (args[0] == SYS_BLOCKR)
    f->eax = blockr_handler ();
  if (args[0] == SYS_BLOCKW)
    f->eax = blockw_handler ();
  if (args[0] == SYS_CACHECLEAR)
    cacheclear_handler ();
  if (args[0] == SYS_CHDIR)
    f->eax = chdir_handler (args[1]);
  if (args[0] == SYS_MKDIR)
    f->eax = mkdir_handler (args[1]);
  if (args[0] == SYS_READDIR)
    f->eax = readdir_handler (args[1], args[2]);
  if (args[0] == SYS_ISDIR)
    f->eax = isdir_handler (args[1]);
  if (args[0] == SYS_INUMBER)
    f->eax = inumber_handler (args[1]);
}

void
exit_handler (int status)
{
  
  printf("%s: exit(%d)\n", &thread_current ()->name, status);

  struct thread *cur = thread_current ();
  lock_acquire (&exit_lock);
  if (cur->has_parent && cur->cpi != NULL)
    {
    	struct thread *t = thread_find (cur->parID);
      struct child_process_info *cpi = cur->cpi;
    	cpi->exit_status = status;
    	sema_up (&t->wait_sema);

    }
  lock_release (&exit_lock);
  thread_exit ();
}

void
halt_handler (void)
{
  shutdown_power_off ();
}

int
exec_handler (const char *cmd_line)
{
	check_pointer (cmd_line, -1);
	return process_execute (cmd_line);
}

int
wait_handler (pid_t pid)
{
  return process_wait (pid);
}

int
practice_handler (int status)
{
	return status + 1;
}


bool
create_handler (const char *file, unsigned initial_size) 
{
  if (file == NULL)
    exit_handler (-1);

  check_pointer (file, -1);
  if (strcmp (file, "") == 0)
    exit_handler(-1);

  bool success = filesys_create (file, initial_size);
  return success;
}

bool
remove_handler (const char *file) 
{
  check_pointer (file, -1);
  bool success = filesys_remove (file);
  return success;
}

int 
open_handler (const char *file) 
{
  check_pointer (file, -1);
  if (strcmp (file, "") == 0)
    return -1;

  struct wrapper *wrapper;

  if (strcmp (file, "/") != 0)
    {
      struct dir *par_dir;
      bool success;
      success = dir_resolve (file, &par_dir);

      if (!success || par_dir->inode->removed)
        return -1;

      int split;
      split = last_occurrence (file, '/');
      char *last_name;
      last_name = malloc (128);
      strlcpy (last_name, file + split + 1, PGSIZE);

      struct inode *new_i;
      success = dir_lookup (par_dir, last_name, &new_i);

      if (!success)
        return -1;

      struct inode_disk *metadata;
      uint8_t buffer[BLOCK_SECTOR_SIZE];
      metadata = read_sector (new_i->sector, buffer);

      lock_acquire (&open_lock);
      struct file *new_file = NULL;
      struct dir *new_dir = NULL;

      if (metadata->is_dir)
        new_dir = dir_open (new_i);
      else
        new_file = file_open (new_i);

      lock_release (&open_lock);

      if (new_file == NULL && new_dir == NULL)
        return -1;

      wrapper = palloc_get_page (0);
      wrapper->is_dir = metadata->is_dir;
      wrapper->dir = new_dir;
      wrapper->file = new_file;

      free (last_name);
    }
  else
    {
      wrapper = palloc_get_page (0);
      wrapper->is_dir = true;
      wrapper->dir = dir_open_root ();
      wrapper->file = NULL;
    } 

  int i;
  for (i = 2; i < 130; i++) 
    {
      if (thread_current ()->files[i] == NULL) 
        {
          thread_current ()->files[i] = wrapper;
          return i;
        }
    }
  return -1;
}

int filesize_handler (int fd) 
{
  if (fd < 0 || fd >= 130 || fd == 1)
    return -1;

  struct wrapper *w;
  w = thread_current ()->files[fd];
  if (w == NULL || w->is_dir)
    return -1;

  struct file *f = w->file;

  int size = file_length (f);
  return size;
}

int read_handler (int fd, void *buffer, unsigned size) 
{
  check_pointer (buffer, -1);
  if (fd < 0 || fd >= 130 || fd == 1)
    return -1;

  struct wrapper *w;
  w = thread_current ()->files[fd];
  if (w == NULL || w->is_dir)
    return -1;

  struct file *f = w->file;

  int read = file_read (f, buffer, size);
  return read;
}

int write_handler (int fd, const void *buffer, unsigned size) 
{
  check_pointer (buffer, -1);
  if (fd < 0 || fd >= 130 || fd == 0)
    return -1;
	else if (fd == 1) 
    {
  		putbuf (buffer, size);
  		return size;
  	}

  struct wrapper *w;
  w = thread_current ()->files[fd];
  if (w == NULL || w->is_dir)
    return -1;

  struct file *f = w->file;

  int written = file_write (f, buffer, size);
  return written;
}

void seek_handler (int fd, unsigned position) 
{
  if (fd <= 0 || fd >= 130)
    return;

  struct wrapper *w;
  w = thread_current ()->files[fd];
  if (w == NULL || w->is_dir)
    return;

  struct file *f = w->file;

  file_seek (f, position);
}

unsigned tell_handler(int fd) 
{
  if (fd <= 0 || fd >= 130) 
    return -1;

  struct wrapper *w;
  w = thread_current ()->files[fd];
  if (w == NULL || w->is_dir)
    return -1;

  struct file *f = w->file;

  unsigned pos = file_tell (f);
  return pos;
}

void close_handler(int fd) 
{
  if (fd <= 0 || fd >= 130 || fd == 1)
    return;

  struct wrapper *w;
  w = thread_current ()->files[fd];
  if (w == NULL)
    return;
  if (w->is_dir)
    {
      struct dir *d = w->dir;

      lock_acquire (&close_lock);
      dir_close (d);
      lock_release (&close_lock);
    }
  else 
    {
      struct file *f = w->file;

      lock_acquire (&close_lock);
      file_close (f);
      lock_release (&close_lock);
    }
  thread_current ()->files[fd] = NULL;

}

int
cacheh_handler (void)
{
  return cache_hits;
}

int
cachem_handler (void)
{
  return cache_misses;
}

unsigned long long
blockr_handler (void)
{
  return (int) block_reads();
}

unsigned long long
blockw_handler (void)
{
  return (int) block_writes();
}

void
cacheclear_handler (void)
{
  cache_clear ();
}

bool 
chdir_handler (const char *dir)
{
  check_pointer (dir, -1);

  if (strcmp(dir, "") == 0)
    return false;
  struct dir *par_dir;
  bool success;
  success = dir_resolve (dir, &par_dir);

  if (!success)
    return false;

  int split;
  split = last_occurrence (dir, '/');
  char *last_name;
  last_name = malloc (128);
  strlcpy (last_name, dir + split + 1, PGSIZE);

  struct inode *dir_i;
  success = dir_lookup (par_dir, last_name, &dir_i);

  if (!success)
    return false;

  dir_close (thread_current ()->cwd);
  thread_current ()->cwd = dir_open (dir_i);
  dir_close (par_dir);
  free (last_name);
  return true;
}

bool 
mkdir_handler (const char *dir)
{
  check_pointer (dir, -1);
  if (strcmp(dir, "") == 0)
  {
    return false;
  }

  struct dir *par_dir;
  bool success;
  success = dir_resolve (dir, &par_dir);

  if (!success || par_dir->inode->removed)
    return false;

  int split;
  split = last_occurrence (dir, '/');
  char *last_name;
  last_name = malloc (128);
  strlcpy (last_name, dir + split + 1, PGSIZE);

  struct inode *dir_i;
  success = dir_lookup (par_dir, last_name, &dir_i);

  if (success)
    return false;

  block_sector_t new_sector;
  success = free_map_allocate (1, &new_sector);
  if (!success)
    return false;
  dir_create (new_sector, 16);
  success = dir_add (par_dir, last_name, new_sector);

  if (!success)
    return false;

  struct inode *new_inode;
  struct dir *new_dir;
  success = dir_lookup (par_dir, last_name, &new_inode);

  if (!success)
    return false;
  
  new_dir = dir_open (new_inode);

  if (new_dir == NULL)
    return false;

  dir_add (new_dir, ".", new_sector);
  dir_add (new_dir, "..", par_dir->inode->sector);

  dir_close (par_dir);
  dir_close (new_dir);
  free (last_name);

  return true;
}

bool 
readdir_handler (int fd, char name[NAME_MAX + 1])
{
  check_pointer (name, -1);

  struct wrapper *wrapper;
  wrapper = thread_current ()->files[fd];
  if (!wrapper->is_dir)
    return false;
  struct dir *dir = wrapper->dir;
  bool success;
  success = dir_readdir (dir, name);
  while (success && 
            (strcmp(name, ".") == 0 || strcmp(name, "..") == 0))
      success = dir_readdir (dir, name);
  return success;
}

bool 
isdir_handler (int fd)
{
  struct wrapper *wrapper;
  wrapper = thread_current ()->files[fd];
  return wrapper->is_dir;
}

int 
inumber_handler (int fd)
{
  struct wrapper *wrapper;
  wrapper = thread_current ()->files[fd];
  if (wrapper->is_dir)
    return inode_get_inumber (wrapper->dir->inode);
  else
    return inode_get_inumber (wrapper->file->inode); 

}

static void
check_pointer (const void *vaddr, int buffer_size)
{
  if (vaddr == NULL || !is_user_vaddr (vaddr) 
                    || pagedir_get_page (thread_current ()->pagedir, vaddr) == NULL)
      exit_handler (-1);
  else if (buffer_size != -1)
      check_buffer (vaddr, buffer_size);
}

static void
check_buffer (const void *vaddr, int buffer_size)
{
  int i;
  for (i = 0; i < buffer_size; i++)
    {
      check_pointer (vaddr, -1);
      vaddr++;
    }
}

int 
last_occurrence (const char *str, char desired)
{
  int index = -1;
  int i;

  for (i = 0; i < strlen (str); i++)
    {
      if (str[i] == desired)
        index = i;
    }
  return index;
}