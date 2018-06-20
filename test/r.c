#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
int print () {
  char cmd[80];
  sprintf (cmd, "head -4 /proc/%d/stat|cut -d ' ' -f 1-6", getpid ());
  return system (cmd);
}
int main () {
  int res = print ();
  FILE * file = popen ("./c.sh", "r");
  int ch, first = 0;
  while ((ch = getc (file)) != EOF) {
    if (!first) first = ch;
    if (first != 'y') fputc (ch, stdout);
    if (ch == '\n') first = 0;
  }
  pclose (file);
  return res;
}
