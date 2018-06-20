#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
int main () {
  char cmd[80];
  sprintf (cmd, "head -4 /proc/%d/stat|cut -d ' ' -f 1-6", getpid ());
  int res = system (cmd);
  res &= system ("./c.sh &");
  res &= system ("./c.sh &");
  while (1);
  return res;
}
