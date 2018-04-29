#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  char message[16];
  int handle = open("sample.txt");
  if (handle < 2)
    fail("open() returned %d", handle);
  seek(handle, 100000);
  int size = read(handle, message, 1);
  msg("should be zero and was %d", size);
}
