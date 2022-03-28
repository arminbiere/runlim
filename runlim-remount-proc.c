#include <sys/mount.h>

int
main (void)
{
  return mount ("proc", "/proc", "proc", 0, 0);
}
