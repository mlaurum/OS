#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

static char buf[100];

void
test_main (void)
{ 
  int fd;
  cacheclear();

  CHECK ((fd = open ("cache-test.txt")) > 1, "open \"cache-test.txt\"");
  CHECK (read (fd, &buf, 100) > 0, "read \"cache-test.txt\"");
  close(fd);
  int initial_hits = cacheh();

  CHECK ((fd = open ("cache-test.txt")) > 1, "second open \"cache-test.txt\"");
  CHECK (read (fd, &buf, 100) > 0, "second read \"cache-test.txt\"");
  close(fd);
  int new_hits = cacheh();
  msg("New hits should be greater than initial hits: %d, expected 1", new_hits > initial_hits);
}
