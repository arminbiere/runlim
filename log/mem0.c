#include <stdlib.h>

int main()
{
  unsigned i, size = 1 << 27;
  char * a = malloc(size);
  for(i = 0; i < size; i++) a[i] = 0x55;
}
