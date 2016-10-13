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
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <asm/param.h>
#include <dirent.h>
#include <fcntl.h>

/*------------------------------------------------------------------------*/

#define SAMPLE_RATE 100000	/* in milliseconds */
#define REPORT_RATE 100		/* in terms of sampling */
#define COMM_LEN 16

/*------------------------------------------------------------------------*/
#define Proc_BASE    "/proc"
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

/*------------------------------------------------------------------------*/

typedef struct Proc
{
  pid_t pid, ppid;
  int highlight;
  long unsigned ujiffies, sjiffies;
  long unsigned vsize;
  long rsize;
  struct Child *children;
  struct Proc *parent;
  struct Proc *next;
}
Proc;

typedef struct Child
{
  Proc *child;
  struct Child *next;
}
Child;

static Proc *proc_list = NULL;

typedef enum Status Status;

/*------------------------------------------------------------------------*/

#define USAGE \
"usage: runlim [option ...] program [arg ...]\n" \
"\n" \
"  where option is from the following list:\n" \
"\n" \
"    -h                         print this command line summary\n" \
"    --help\n" \
"\n" \
"    --version                  print version number\n" \
"\n" \
"    -o <file>                  overwrite or create <file> for logging\n" \
"    --output-file=<file>\n" \
"\n" \
"    --space-limit=<number>     set space limit to <number> MB\n" \
"    -s <number>\n"\
"\n" \
"    --time-limit=<number>      set time limit to <number> seconds\n" \
"    -t <number>\n"\
"\n" \
"    --real-time-limit=<number> set real time limit to <number> seconds\n" \
"    -r <number>\n"\
"\n" \
"    -k|--kill                  propagate signals\n" \
"\n" \
"The program is the name of an executable followed by its arguments.\n"

/*------------------------------------------------------------------------*/

static void
usage (void)
{
  printf (USAGE);
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
	       "*** runlim: number argument for '-%c' is missing\n",
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
      fputs ("*** runlim: argument to ", stderr);
      print_long_command_line_option (stderr, str);
      fputs (" is missing\n", stderr);
      exit (1);

      res = 0;
    }

  if (!isposnumber (p + 1))
    {
      fputs ("*** runlim: argument to ", stderr);
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
      fprintf (stderr, "*** runlim: argument to '%s' is missing\n", option);
      exit (1);
    }

  res = fopen (name, "w");
  if (!res)
    {
      fprintf (stderr, "*** runlim: could not write to '%s'\n", name);
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

static FILE *log = 0;
static int child_pid = -1;
static int parent_pid = -1;
static int num_samples_since_last_report = 0;
static unsigned num_samples = 0;
static double max_mb = 0;
static double max_seconds = 0;
static int propagate_signals = 0;
static int children = 0;

/*------------------------------------------------------------------------*/

#define PID_POS 0
#define PPID_POS 3
#define STIME_POS 13
#define UTIME_POS 14
#define VSIZE_POS 22
#define RSIZE_POS 23

/*------------------------------------------------------------------------*/

static unsigned start_time, time_limit, real_time_limit, space_limit;

/*------------------------------------------------------------------------*/

static Proc *
new_proc (pid_t pid, pid_t ppid, long unsigned sjiffies, 
	  long unsigned ujiffies, unsigned long vsize, long rsize)
{
  Proc *new;

  if (!(new = malloc (sizeof (Proc))))
    {
      perror ("malloc");
      exit (1);
    }
  new->pid = pid;
  new->ppid = ppid;
  new->sjiffies = sjiffies;
  new->ujiffies = ujiffies;
  new->vsize = vsize;
  new->rsize = rsize;
  new->highlight = 0;
  new->children = NULL;
  new->parent = NULL;
  new->next = proc_list;
  return proc_list = new;
}

/*------------------------------------------------------------------------*/

static void
add_child (Proc * parent, Proc * child)
{
  Child *new, **walk;

  if (!(new = malloc (sizeof (Child))))
    {
      perror ("malloc");
      exit (1);
    }
  new->child = child;
  for (walk = &parent->children; *walk; walk = &(*walk)->next)
    if ((*walk)->child->pid > child->pid)
      break;

  new->next = *walk;
  *walk = new;
}

/*------------------------------------------------------------------------*/

static Proc *
find_proc (pid_t pid)
{
  Proc *p;

  for (p = proc_list; p; p = p->next)
    if (p->pid == pid)
      break;

  return p;
}

/*------------------------------------------------------------------------*/

static void
delete_proc (void) 
{
  Child * c, * nextc;
  Proc * p, * nextp;

  for (p = proc_list; p; p = nextp)
    {
      nextp = p->next;
      for (c = p->children; c; c = nextc) 
	{
	  nextc = c->next;
          free (c);
	}
      free (p);
    }
  proc_list = 0;
}

/*------------------------------------------------------------------------*/

static void
add_proc (pid_t pid, pid_t ppid, long unsigned sjiffies, 
	  long unsigned ujiffies, unsigned long vsize, long rsize)
{
  Proc *this, *parent;

  if (!(this = find_proc (pid)))
    this = new_proc (pid, ppid, sjiffies, ujiffies, vsize, rsize);
  else
    {
      /* was already there, update sjiffies, ujiffies, vsize, rsize */
      this->sjiffies = sjiffies;
      this->ujiffies = ujiffies;
      this->vsize = vsize;
      this->rsize = rsize;
      if (this->ppid != ppid)
	{
	  /* parent added before with ppid 0 */
	  this->ppid = ppid;
	}
      return;
    }

  if (pid == ppid)
    ppid = 0;

  if (!(parent = find_proc (ppid)))
    parent = new_proc (ppid, 0, 0, 0, 0, 0);

  add_child (parent, this);
  this->parent = parent;
}

/*------------------------------------------------------------------------*/
/* Uses a similar method as 'procps' for finding the process name in the
 * /proc filesystem. My thanks to Albert and procps authors.
 */
static void
read_proc ()
{
  int i, ch, tmp, size, pos, empty;
  long unsigned ujiffies, sjiffies;
  char *path, *buffer, *token;
  unsigned long vsize;
  struct dirent *de;
  pid_t pid, ppid;
  FILE *file;
  long rsize;
  DIR *dir;

  buffer = malloc (size = 100);

  if (!(dir = opendir (Proc_BASE)))
    {
      perror (Proc_BASE);
      exit (1);
    }

  empty = 1;
SKIP:
  while ((de = readdir (dir)) != NULL)
    {
      empty = 0;
      if ((pid = (pid_t) atoi (de->d_name)) == 0) continue;
      if (!(path = malloc (strlen (Proc_BASE) + strlen (de->d_name) + 10)))
	continue;
      sprintf (path, "%s/%d/stat", Proc_BASE, pid);
      file = fopen (path, "r");
      free (path);
      if (!file) continue;
      pos = 0;
      
      while ((ch = getc (file)) != EOF)
	{
	  if (pos >= size - 1)
	    buffer = realloc (buffer, size *= 2);
	  
	  buffer[pos++] = ch;
	}
      
      fclose (file);
      
      ujiffies = sjiffies = -1;
      vsize = 0;
      rsize = 0;
      
      token = strtok (buffer, " ");
      i = 0;
      
      while (token)
	{
	  switch (i++)
	    {
	    case VSIZE_POS:
	      if (sscanf (token, "%lu", &vsize) != 1) goto SKIP;
	      break;
	    case RSIZE_POS:
	       if (sscanf (token, "%ld", &rsize) != 1) goto SKIP;
	       break;
	    case PID_POS:
	      if (atoi (token) != pid) goto SKIP;
	      break;
	    case PPID_POS:
	      if (sscanf (token, "%d", &ppid) != 1) goto SKIP;
	      break;
	    case STIME_POS:
	      if (sscanf (token, "%d", &tmp) != 1) goto SKIP;
	      sjiffies = tmp;
	      assert (sjiffies >= 0);
	      break;
	    case UTIME_POS:
	      if (sscanf (token, "%d", &tmp) != 1) goto SKIP;
	      ujiffies = tmp;
	      assert (usage >= 0);
	      break;
	    default:
	      break;
	    }
	  
	  token = strtok (0, " ");
	}
      if (ujiffies < 0 || sjiffies < 0) goto SKIP;
      add_proc (pid, ppid, ujiffies, sjiffies, vsize, rsize);
    }
  
  (void) closedir (dir);
  free (buffer);
  if (empty)
    {
      fprintf (stderr, "%s is empty (not mounted ?)\n", Proc_BASE) ;
      exit (1);
    }
}

/*------------------------------------------------------------------------*/

static void
sample_children (Child *cptr, double *time_ptr, double *mb_ptr)
{
  while (cptr)
    {
      children++;

#ifdef DEBUGSAMPLE
      fprintf(log, "ujiffies %lu\n", cptr->child->ujiffies); 
      fprintf(log, "sjiffies %lu\n", cptr->child->sjiffies); 
      fprintf(log, "result %f\n",
              (cptr->child->ujiffies + cptr->child->sjiffies) / (double)HZ);
      fprintf(log, "timeptr %f\n", *time_ptr);
      fprintf(log, "pid: %d vsize %lu\n",
              cptr->child->pid, cptr->child->vsize);
#endif
      *time_ptr +=
        (cptr->child->ujiffies + cptr->child->sjiffies) / (double) HZ;

      *mb_ptr += cptr->child->rsize / (double) (1 << 8);
#ifdef DEBUGSAMPLE
      fprintf(log, "timeptr(new) %f\n", *time_ptr);
#endif
      if (cptr->child->children)
	sample_children (cptr->child->children, time_ptr, mb_ptr);

      cptr = cptr->next;
    }
  return;
}

/*------------------------------------------------------------------------*/
/* should trace the memory and jiffies consumption recursively
 * 1. calls read_proc
 * 2. calls find_proc with pid
 * 3. goes through all the children
 */
static int
sample_recursive (double *time_ptr, double *mb_ptr)
{
  Proc *pr;

  read_proc ();
  pr = find_proc (child_pid);

  if (!pr)
    return 0;

  *time_ptr = (pr->ujiffies + pr->sjiffies) / (double) HZ;
  *mb_ptr = pr->rsize / (double) (1 << 8);

  if (pr->children)
    sample_children (pr->children, time_ptr, mb_ptr);

  delete_proc ();

  return 1;
}

/*------------------------------------------------------------------------*/

struct itimerval timer, old_timer;
static int caught_out_of_memory;
static int caught_out_of_time;

/*------------------------------------------------------------------------*/

static void
really_kill_child (void)
{
  usleep (10000);
  kill (child_pid, SIGTERM);
  usleep (5000);
  kill (child_pid, SIGTERM);
  usleep (2000);
  kill (child_pid, SIGTERM);
  usleep (1000);
  kill (child_pid, SIGKILL);
  usleep (1000);
  kill (child_pid, SIGKILL);
  usleep (1000);
  kill (child_pid, SIGKILL);
}

/*------------------------------------------------------------------------*/

static double
wall_clock_time (void)
{
  double res = -1;
  struct timeval tv;
  if (!gettimeofday (&tv, 0))
    {
      res = 1e-6 * tv.tv_usec;
      res += tv.tv_sec;
    }
  return res;
}

/*------------------------------------------------------------------------*/

static double
real_time (void) 
{
  double res;
  if (start_time < 0) return -1;
  res = wall_clock_time() - start_time;
  return res;
}

/*------------------------------------------------------------------------*/

static void
report (double time, double mb)
{
  double real = real_time ();
  fprintf (log, 
     "[runlim] sample:\t\t%.1f time, %.1f real, %.1f MB\n", 
     time, real, mb);
  fflush (log);
}

/*------------------------------------------------------------------------*/

static void
sampler (int s)
{
  double mb, time;
  int res;

  assert (s == SIGALRM);
  num_samples++;

  res = sample_recursive (&time, &mb);

  if (res)
    { 
      if (mb > max_mb)
	max_mb = mb;

      if (time > max_seconds)
	max_seconds = time;
    }

  if (++num_samples_since_last_report >= REPORT_RATE)
    {
      num_samples_since_last_report = 0;
      if (res)
	report (time, mb);
    }

  if (res)
    {
      if (time > time_limit || real_time () > real_time_limit)
	{
	  caught_out_of_time = 1;
	  really_kill_child ();
	}
      else if (mb > space_limit)
	{
	  caught_out_of_memory = 1;
	  really_kill_child ();
	}
    }
}

/*------------------------------------------------------------------------*/

static int caught_usr1_signal = 0;

/*------------------------------------------------------------------------*/

static void
sig_usr1_handler (int s)
{
  assert (s == SIGUSR1);
  caught_usr1_signal = 1;
}

/*------------------------------------------------------------------------*/

int
main (int argc, char **argv)
{
  int i, j, res, status, s, ok;
  struct rlimit l;
  const char *p;
  double real;
  time_t t;

  ok = OK;				/* status of the runlim */
  s = 0;				/* signal caught */
  time_limit = 60 * 60 * 24 * 3600;	/* one year */
  real_time_limit = time_limit;
  space_limit = get_physical_mb ();	/* physical memory size */

  for (i = 1; i < argc; i++)
    {
      if (argv[i][0] == '-')
	{
	  if (argv[i][1] == 't')
	    {
	      time_limit = parse_number_argument (&i, argc, argv);
	    }
	  else if (strstr (argv[i], "--time-limit=") == argv[i])
	    {
	      time_limit = parse_number_rhs (argv[i]);
	    }
	  else if (argv[i][1] == 'r')
	    {
	      real_time_limit = parse_number_argument (&i, argc, argv);
	    }
	  else if (strstr (argv[i], "--real-time-limit=") == argv[i])
	    {
	      real_time_limit = parse_number_rhs (argv[i]);
	    }
	  else if (argv[i][1] == 's')
	    {
	      space_limit = parse_number_argument (&i, argc, argv);
	    }
	  else if (strstr (argv[i], "--space-limit=") == argv[i])
	    {
	      space_limit = parse_number_rhs (argv[i]);
	    }
	  else if (strcmp (argv[i], "-v") == 0 ||
	           strcmp (argv[i], "--version") == 0)
	    {
	      printf ("%s\n", VERSION);
	      exit (0);
	    }
	  else if (strcmp (argv[i], "-k") == 0 ||
	           strcmp (argv[i], "--kill") == 0)
	    {
	      propagate_signals = 1;
	    }
	  else if (strcmp (argv[i], "-h") == 0 ||
	           strcmp (argv[i], "--help") == 0)
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
	      fprintf (stderr, "*** runlim: invalid option '%s' (try '-h')\n",
		       argv[i]);
	      exit (1);
	    }
	}
      else
	break;
    }

  if (i >= argc)
    {
      fprintf (stderr, "*** runlim: no program specified (try '-h')\n");
      exit (1);
    }

  if (!log)
    log = stderr;

  fprintf (log, "[runlim] version:\t\t%s\n", VERSION);
  fprintf (log, "[runlim] time limit:\t\t%u seconds\n", time_limit);
  fprintf (log, "[runlim] real time limit:\t%u seconds\n", real_time_limit);
  fprintf (log, "[runlim] space limit:\t\t%u MB\n", space_limit);
  for (j = i; j < argc; j++)
    fprintf (log, "[runlim] argv[%d]:\t\t%s\n", j - i, argv[j]);
  t = time (0);
  fprintf (log, "[runlim] start:\t\t\t%s", ctime (&t));
  fflush (log);

  signal (SIGUSR1, sig_usr1_handler);
  parent_pid = getpid ();

  start_time = wall_clock_time();
  if ((child_pid = fork ()) != 0)
    {
      if (child_pid < 0)
	{
	  ok = FORK_FAILED;
	  res = 1;
	}
      else
	{
	  status = 0;
	  fprintf (log, "[runlim] main pid:\t\t%d\n", (int) child_pid);
	  fflush (log);

	  assert (SAMPLE_RATE < 1000000);
	  timer.it_interval.tv_sec = 0;
	  timer.it_interval.tv_usec = SAMPLE_RATE;
	  timer.it_value = timer.it_interval;
	  signal (SIGALRM, sampler);
	  setitimer (ITIMER_REAL, &timer, &old_timer);

	  (void) wait (&status);

	  setitimer (ITIMER_REAL, &old_timer, &timer);

	  if (WIFEXITED (status))
	    res = WEXITSTATUS (status);
	  else if (WIFSIGNALED (status))
	    {
	      s = WTERMSIG (status);
	      res = 128 + s;
	      switch (s)
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
      unsigned hard_time_limit;
      if (time_limit < real_time_limit) {
	hard_time_limit = time_limit;
	hard_time_limit = (hard_time_limit * 101 + 99) / 100;	// + 1%
	l.rlim_cur = l.rlim_max = hard_time_limit;
	setrlimit (RLIMIT_CPU, &l);
	l.rlim_cur = l.rlim_max = space_limit;
	setrlimit (RLIMIT_AS, &l);
      }
      execvp (argv[i], argv + i);
      kill (getppid (), SIGUSR1);
      exit (1);
    }

  real = real_time ();

  if (caught_usr1_signal)
    ok = EXEC_FAILED;
  else if (caught_out_of_memory)
    ok = OUT_OF_MEMORY;
  else if (caught_out_of_time)
    ok = OUT_OF_TIME;

  t = time (0);
  fprintf (log, "[runlim] end:\t\t\t%s", ctime (&t));
  fprintf (log, "[runlim] status:\t\t");

  if (max_seconds >= time_limit || real_time () >= real_time_limit)
    goto FORCE_OUT_OF_TIME_ENTRY;

  switch (ok)
    {
    case OK:
      fputs ("ok", log);
      res = 0;
      break;
    case OUT_OF_TIME:
FORCE_OUT_OF_TIME_ENTRY:
      fputs ("out of time", log);
      res = 2;
      break;
    case OUT_OF_MEMORY:
      fputs ("out of memory", log);
      res = 3;
      break;
    case SEGMENTATION_FAULT:
      fputs ("segmentation fault", log);
      res = 4;
      break;
    case BUS_ERROR:
      fputs ("bus error", log);
      res = 5;
      break;
    case FORK_FAILED:
      fputs ("fork failed", log);
      res = 6;
      break;
    case INTERNAL_ERROR:
      fputs ("internal error", log);
      res = 7;
      break;
    case EXEC_FAILED:
      fputs ("execvp failed", log);
      res = 1;
      break;
    default:
      fprintf (log, "signal(%d)", s);
      res = 11;
      break;
    }
  fputc ('\n', log);
  fprintf (log, "[runlim] result:\t\t%d\n", res);
  fflush (log);

  fprintf (log, "[runlim] children:\t\t%d\n", children);
  fprintf (log, "[runlim] real:\t\t\t%.2f seconds\n", real);
  fprintf (log, "[runlim] time:\t\t\t%.2f seconds\n", max_seconds);
  fprintf (log, "[runlim] space:\t\t\t%.1f MB\n", max_mb);
  fprintf (log, "[runlim] samples:\t\t%u\n", num_samples);

  fflush (log);

  if (propagate_signals)
    {
      switch (ok)
	{
	case OK:
	case OUT_OF_TIME:
	case OUT_OF_MEMORY:
	case FORK_FAILED:
	case INTERNAL_ERROR:
	case EXEC_FAILED:
	  break;
	default:
	  kill (parent_pid, s);
	  break;
	}
    }

  return res;
}
