#include <stdlib.h>
#include <stdio.h>
int main()
{
  size_t i, size;
  for (size = (1l << 30); size < (1l << 33); size *= 2) {
    char * a;
    printf ("%.1f\n", size / (double)(1<<20));
    a = malloc (size);
    for(i = 0; i < size; i++) a[i] = 42;
    free (a);
  }
  return 0;
}
