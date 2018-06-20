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
char * line;
int size, num;
int main () {
  int res = print ();
  FILE * file =
    popen ("head -4 /proc/$$/stat|cut -d ' ' -f 1-6; exec yes", "r");
  line = malloc (size = 128);
  int ch;
  while ((ch = getc (file)) != EOF) {
    if (size == num) line = realloc (line, size *= 2);
    if (ch == '\n') {
      line[num++] = 0;
      if (strcmp (line, "y")) printf ("%s\n", line), fflush (stdout);
      num = 0;
    } else line[num++] = ch;
  }
  pclose (file);
  free (line);
  return res;
}
