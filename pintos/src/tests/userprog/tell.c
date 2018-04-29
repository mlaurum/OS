
#include <string.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  
  int handle = open("sample.txt");
  if (handle < 2)
    fail("open() returned %d", handle);
  seek(handle, 10);
  msg("10 is the proper position and was %d", tell(handle));

}
