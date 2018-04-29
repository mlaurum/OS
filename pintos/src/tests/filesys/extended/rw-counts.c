#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include "tests/filesys/extended/sample.inc"
static char buf[100];
void
test_main (void)
{ 
  int fd;
  cacheclear();
  CHECK ((fd = open ("empty-file.txt")) > 1, "open \"empty-file.txt\"");
  int initial = blockr();
  write (fd, sample, 10000);
  close(fd);

  int reads = blockr();
  msg("There should only be one inode metadata read: %d, expected 1", reads - initial);

}
