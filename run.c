#include <assert.h>
#include <ctype.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>

/*------------------------------------------------------------------------*/

enum Status
{
  OK,
  OUT_OF_MEMORY,
  OUT_OF_TIME,
  SEGMENTATION_FAULT,
  BUS_ERROR,
  OTHER_SIGNAL,
  FORK_FAILED,
  INTERNAL_ERROR,
  EXEC_FAILED
};

typedef enum Status Status;

/*------------------------------------------------------------------------*/

static void
usage (void)
{
  printf ("usage: run [option ...] program [arg ...]\n"
	  "\n"
	  "  where option is from the following list:\n"
	  "\n"
	  "    -h                       print this command line summary\n"
	  "    --help\n"
	  "\n"
	  "    -v                       print version number\n"
	  "    --version\n"
	  "\n"
	  "    -o <file>                overwrite or create <file> for logging\n"
	  "    --output-file=<file>\n"
	  "\n"
	  "    -m <number>              set space limit to <number> MB\n"
	  "    --space-limit=<number>\n"
	  "\n"
	  "    -t <number>              set time limit to <number> seconds\n"
	  "    --time-limit=<number>\n"
	  "\n"
	  "The program is the name of an executable followed by its arguments.\n");
}

/*------------------------------------------------------------------------*/

static int
isposnumber (const char *str)
{
  const char *p;
  int res;

  if (*str)
    {
      for (res = 1, p = str; res && *p; p++)
	res = isdigit ((int) *p);
    }
  else
    res = 0;

  return res;
}

/*------------------------------------------------------------------------*/

static unsigned
parse_number_argument (int *i, int argc, char **argv)
{
  unsigned res;

  if (argv[*i][2])
    {
      if (isposnumber (argv[*i] + 2))
	res = (unsigned) atoi (argv[*i] + 2);
      else
	goto ARGUMENT_IS_MISSING;
    }
  else if (*i + 1 < argc && isposnumber (argv[*i + 1]))
    {
      res = (unsigned) atoi (argv[*i + 1]);
      *i += 1;
    }
  else
    {
    ARGUMENT_IS_MISSING:

      fprintf (stderr,
	       "*** run: number argument for '-%c' is missing\n",
	       argv[*i][1]);
      exit (1);

      res = 0;
    }

  return res;
}

/*------------------------------------------------------------------------*/

static void
print_long_command_line_option (FILE * file, char *str)
{
  const char *p;

  for (p = str; *p && *p != ' '; p++)
    fputc (*p, file);
}

/*------------------------------------------------------------------------*/

static unsigned
parse_number_rhs (char *str)
{
  unsigned res;
  char *p;

  p = strchr (str, '=');
  assert (p);

  if (!p[1])
    {
      fputs ("*** run: argument to ", stderr);
      print_long_command_line_option (stderr, str);
      fputs (" is missing\n", stderr);
      exit (1);

      res = 0;
    }

  if (!isposnumber (p + 1))
    {
      fputs ("*** run: argument to ", stderr);
      print_long_command_line_option (stderr, str);
      fputs (" is not a positive number\n", stderr);
      exit (1);

      res = 0;
    }

  res = (unsigned) atoi (p + 1);

  return res;
}

/*------------------------------------------------------------------------*/

static FILE *
open_log (const char *name, const char *option)
{
  FILE *res;

  if (!name || !name[0])
    {
      fprintf (stderr, "*** run: argument to '%s' is missing\n", option);
      exit (1);
    }

  res = fopen (name, "w");
  if (!res)
    {
      fprintf (stderr, "*** run: could not write to '%s'\n", name);
      exit (1);
    }

  return res;
}

/*------------------------------------------------------------------------*/

static unsigned
get_physical_mb ()
{
  unsigned res;
  long tmp;

  tmp = sysconf (_SC_PAGE_SIZE) * sysconf (_SC_PHYS_PAGES);
  tmp >>= 20;
  res = (unsigned) tmp;

  return res;
}

/*------------------------------------------------------------------------*/

static int caught_usr1_signal = 0;

/*------------------------------------------------------------------------*/

static void
handler (int s)
{
  if (s == SIGUSR1)
    caught_usr1_signal = 1;
}

/*------------------------------------------------------------------------*/

int
main (int argc, char **argv)
{
  int i, j, res, child, status, s, ok;
  unsigned max_seconds, max_mb;
  struct rusage u;
  struct rlimit l;
  const char *p;
  FILE *log;
  time_t t;

  ok = OK;			/* status of the run */
  log = 0;			/* log file */
  s = 0;			/* signal caught */
  max_seconds = 60 * 60 * 24;	/* one day as default time limit */
  max_mb = get_physical_mb ();	/* physical memory size as space limit */

  for (i = 1; i < argc; i++)
    {
      if (argv[i][0] == '-')
	{
	  if (argv[i][1] == 't')
	    {
	      max_seconds = parse_number_argument (&i, argc, argv);
	    }
	  else if (strstr (argv[i], "--time-limit=") == argv[i])
	    {
	      max_seconds = parse_number_rhs (argv[i]);
	    }
	  else if (argv[i][1] == 'm')
	    {
	      max_mb = parse_number_argument (&i, argc, argv);
	    }
	  else if (strstr (argv[i], "--space-limit=") == argv[i])
	    {
	      max_mb = parse_number_rhs (argv[i]);
	    }
	  else
	    if (strcmp (argv[i], "-v") == 0
		|| strcmp (argv[i], "--version") == 0)
	    {
	      printf ("%s\n", VERSION);
	      exit (0);
	    }
	  else
	    if (strcmp (argv[i], "-h") == 0
		|| strcmp (argv[i], "--help") == 0)
	    {
	      usage ();
	      exit (0);
	    }
	  else if (argv[i][1] == 'o')
	    {
	      if (argv[i][2])
		log = open_log (argv[i] + 2, "-o");
	      else
		log = open_log (i + 1 >= argc ? 0 : argv[++i], "-o");
	    }
	  else if (strstr (argv[i], "--output-file=") == argv[i])
	    {
	      p = strchr (argv[i], '=');
	      assert (p);
	      log = open_log (p + 1, "--output-file");
	    }
	  else
	    {
	      fprintf (stderr,
		       "*** run: unknown command line option '%s' (try '-h')\n",
		       argv[i]);
	      exit (1);
	    }
	}
      else
	break;
    }

  if (i >= argc)
    {
      fprintf (stderr, "*** run: no program specified (try '-h')\n");
      exit (1);
    }

  if (!log)
    log = stderr;

  fprintf (log, "[run] time limit:\t%u seconds\n", max_seconds);
  fprintf (log, "[run] space limit:\t%u MB\n", max_mb);
  for (j = i; j < argc; j++)
    fprintf (log, "[run] argv[%d]:\t\t%s\n", j - i, argv[j]);
  t = time (0);
  fprintf (log, "[run] start:\t\t%s", ctime (&t));
  fflush (log);

  signal (SIGUSR1, handler);

  if ((child = fork ()) != 0)
    {
      if (child < 0)
	{
	  ok = FORK_FAILED;
	  res = 1;
	}
      else
	{
	  status = 0;
	  fprintf (log, "[run] child pid:\t%d\n", (int) child);
	  fflush (stderr);

	  (void) wait (&status);

	  if (WIFEXITED (status))
	    res = WEXITSTATUS (status);
	  else if (WIFSIGNALED (status))
	    {
	      switch ((s = WTERMSIG (status)))
		{
		case SIGXFSZ:
		  ok = OUT_OF_MEMORY;
		  break;
		case SIGXCPU:
		  ok = OUT_OF_TIME;
		  break;
		case SIGSEGV:
		  ok = SEGMENTATION_FAULT;
		  break;
		case SIGBUS:
		  ok = BUS_ERROR;
		  break;
		default:
		  ok = OTHER_SIGNAL;
		  break;
		}

	      res = 1;
	    }
	  else
	    {
	      ok = INTERNAL_ERROR;
	      res = 1;
	    }
	}
    }
  else
    {
      l.rlim_cur = l.rlim_max = max_seconds;
      setrlimit (RLIMIT_CPU, &l);
      l.rlim_cur = l.rlim_max = max_mb << 20;
      setrlimit (RLIMIT_DATA, &l);
      execvp (argv[i], argv + i);
      kill (getppid (), SIGUSR1);
      exit (1);
    }

  if (caught_usr1_signal)
    ok = EXEC_FAILED;

  t = time (0);
  fprintf (log, "\n[run] end:\t\t%s", ctime (&t));
  fprintf (log, "[run] status:\t\t");
  switch (ok)
    {
    case OK:
      fputs ("ok", log);
      break;
    case OUT_OF_TIME:
      fputs ("out of time", log);
      break;
    case OUT_OF_MEMORY:
      fputs ("out of memory", log);
      break;
    case SEGMENTATION_FAULT:
      fputs ("segmentation fault", log);
      break;
    case BUS_ERROR:
      fputs ("bus error", log);
      break;
    case FORK_FAILED:
      fputs ("fork failed", log);
      break;
    case INTERNAL_ERROR:
      fputs ("internal error", log);
      break;
    case EXEC_FAILED:
      fputs ("execvp failed", log);
      break;
    default:
      fprintf (log, "signal(%d)\n", s);
      break;
    }
  fputc ('\n', log);
  fprintf (log, "[run] result:\t\t%d\n", res);
  fflush (log);

  if (getrusage (RUSAGE_CHILDREN, &u) == 0)
    {
      fprintf (log, "[run] time:\t\t%.1f seconds\n",
#         ifdef INCLUDE_SYSTEM_TIME
	       ((double) u.ru_stime.tv_sec) +
	       10e-7 * ((double) u.ru_stime.tv_usec) +
#         endif
	       ((double) u.ru_utime.tv_sec) +
	       10e-7 * ((double) u.ru_utime.tv_usec));

      fprintf (log, "[run] space:\t\t%.1f MB\n",
	       u.ru_maxrss / 1024.0 / 1024.0);
    }
  else
    fprintf (log, "[run] could not get resource usage\n");
  fflush (log);

  exit (res);
  return res;
}
