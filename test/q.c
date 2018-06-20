#include <stdlib.h>
#include <stdio.h>
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
  if (!fork ()) { res &= print (); while (1); }
  (void) wait (&res);
  return res;
}
