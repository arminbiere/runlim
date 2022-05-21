/*------------------------------------------------------------------------*\
     See LICENSE for copyright and restrictions on using this software.
\*------------------------------------------------------------------------*/

#include <asm/param.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/*------------------------------------------------------------------------*/

#define SAMPLE_RATE 100000l	/* in microseconds */
#define REPORT_RATE 100l	/* in terms of sampling */
#define KILL_DELAY 512l		/* in milliseconds */

/*------------------------------------------------------------------------*/

typedef struct Process Process;
typedef enum Status Status;

/*------------------------------------------------------------------------*/

enum Status
{
  OK = 0,
  OUT_OF_TIME = 1,
  OUT_OF_MEMORY = 2,
  BUS_ERROR = 7,
  SEGMENTATION_FAULT = 11,
  OTHER_SIGNAL = 100,
  INTERNAL_ERROR = 300,
  FORK_FAILED = 200,
  EXEC_FAILED = 1000
};

/*------------------------------------------------------------------------*/

struct Process
{
  char new;
  char active;
  char cyclic_sampling;
  char cyclic_killing;
  int pid;
  int ppid;
  long sampled;
  double time;
  double memory;
  Process * next_process;
  Process * first_child;
  Process * last_child;
  Process * parent;
  Process * next_sibbling;
};

/*------------------------------------------------------------------------*/

#define USAGE \
"usage: runlim [option ...] program [arg ...]\n" \
"\n" \
"where option is from the following list:\n" \
"\n" \
"  -h                         print this command line summary\n" \
"  --help\n" \
"\n" \
"  --version                  print version number\n" \
"\n" \
"  --output-file=<file>       output file (default '<stderr>')\n" \
"  -o <file>\n" \
"\n" \
"  --space-limit=<number>     set space limit to <number> MB\n" \
"  -s <number>\n"\
"\n" \
"  --time-limit=<number>      set time limit to <number> seconds\n" \
"  -t <number>\n"\
"\n" \
"  --real-time-limit=<number> set real time limit to <number> seconds\n" \
"  -r <number>\n"\
"\n" \
"  --sample-rate=<number>     sample rate in microseconds " \
"(default %ld)\n" \
"\n" \
"  --report-rate=<number>     report rate in terms of sampling " \
"(default %ld)\n" \
"\n" \
"  --kill-delay=<number>      initial kill delay " \
"(default %ld milliseconds)\n" \
"\n" \
"  --debug                    print debugging information\n" \
"  -d\n" \
"\n" \
"  --single                   assume single child process\n" \
"\n" \
"  --kill                     propagate signals\n" \
"  -k\n" \
"\n" \
"  --propagate                propagate exit code\n" \
"  -p\n" \
"\n" \
"The program is the name of an executable followed by its arguments.\n"

/*------------------------------------------------------------------------*/

static FILE *log;
static int close_log;
static int debug_messages;

/*------------------------------------------------------------------------*/

static void
usage (void)
{
  fprintf (log, USAGE, SAMPLE_RATE, REPORT_RATE, KILL_DELAY);
  fflush (log);
}

/*------------------------------------------------------------------------*/

static void
error (const char * fmt, ...)
{
  va_list ap;
  assert (log);
  fputs ("runlim error: ", log);
  va_start (ap, fmt);
  vfprintf (log, fmt, ap);
  fputc ('\n', log);
  va_end (ap);
  fflush (log);
  exit (1);
}

static void
warning (const char * fmt, ...)
{
  va_list ap;
  assert (log);
  fputs ("runlim warning: ", log);
  va_start (ap, fmt);
  vfprintf (log, fmt, ap);
  fputc ('\n', log);
  va_end (ap);
  fflush (log);
}

static void
message (const char * type, const char * fmt, ...)
{
  size_t len;
  va_list ap;
  assert (log);
  fputs ("[runlim] ", log);
  fputs (type, log);
  fputc (':', log);
  for (len = strlen (type); len < 14; len += 8)
    fputc ('\t', log);
  fputc ('\t', log);
  va_start (ap, fmt);
  vfprintf (log, fmt, ap);
  va_end (ap);
  fputc ('\n', log);
  fflush (log);
}

#define debug(TYPE,FMT,ARGS...) \
do { \
  if (debug_messages <= 0) break; \
  message (TYPE, FMT, ##ARGS); \
} while (0)

/*------------------------------------------------------------------------*/

static int
is_positive_long (const char *str, long * res_ptr)
{
  const char *p;
  int ch, digit;
  long res;

  if (!*str)
    return 0;

  res = 0;
  for (p = str; (ch = *p); p++)
    {
      if (LLONG_MAX/10 < res)
	return 0;

      res *= 10;
      digit = ch - '0';

      if (LLONG_MAX - digit < res)
	return 0;

      res += digit;
    }

  *res_ptr = res;

  return 1;
}

/*------------------------------------------------------------------------*/

static long
parse_number_argument (int *i, int argc, char **argv)
{
  char ch = argv[*i][1];
  long res;

  if (argv[*i][2])
    {
      if (!is_positive_long (argv[*i] + 2, &res))
        error ("invalid argument in '%s'", argv[*i]);
    }
  else if (*i + 1 < argc && is_positive_long (argv[*i + 1], &res))
    {
      *i += 1;
    }
  else
    error ("argument missing for '-%c'", ch);

  return res;
}

/*------------------------------------------------------------------------*/

static long
parse_number_rhs (char *str)
{
  long res;
  char *p;

  p = strchr (str, '=');
  assert (p);

  if (!p[1])
    error ("argument missing in '%s'", str);

  if (!is_positive_long (p + 1, &res))
    error ("invalid argument in '%s'", str);

  return res;
}

/*------------------------------------------------------------------------*/

static char * buffer;
static size_t size_buffer;
static size_t pos_buffer;

/*------------------------------------------------------------------------*/

static void
push_buffer (int ch)
{
  if (size_buffer == pos_buffer)
    {
      size_buffer = size_buffer ? 2*size_buffer : 128;
      buffer = realloc (buffer, size_buffer);
      if (!buffer)
	error ("out-of-memory reallocating buffer");
    }

  buffer[pos_buffer++] = ch;
}

/*------------------------------------------------------------------------*/

static int
try_to_remount_proc_file_system (void)
{
  const char * remount_path = "runlim-remount-proc";
  const char * type = "remount '/proc'";
  int pid, res, status;

  debug (type, "trying to remount '/proc' file system");

  pid = fork ();
  if (pid < 0)
    return 0;

  if (!pid)
    {
      execlp (remount_path, remount_path, (char*) 0);
      exit (2);
    }

  res = waitpid (pid, &status, 0);
  if (res < 0)
    {
      debug (type, "failed to wait for '%s' process child", remount_path);
      return 0;
    }

  assert (res == pid);

  if (!WIFEXITED (status))
    {
      debug (type, "'%s' process did not exit properly", remount_path);
      return 0;
    }

  res = WEXITSTATUS (status);
  if (res == 2)
    {
      debug (type, "execution of '%s' process failed", remount_path);
      return 0;
    }

  if (res)
    {
      debug (type, "mounting '/proc' through '%s' failed", remount_path);
      return 0;
    }

  warning ("remounted '/proc' file system");

  return 1;
}

static FILE *
open_proc_file_path_for_reading (const char * path)
{
  FILE * file = fopen (path, "r");
  if (!file)
    {
      if (try_to_remount_proc_file_system ())
	file = fopen (path, "r");
      if (!file)
	error ("can not open '%s' for reading", path);
    }
  return file;
}

/*------------------------------------------------------------------------*/

static const char *
read_host_name ()
{
  const char * host_name_path = "/proc/sys/kernel/hostname";
  FILE * file;
  int ch;

  file = open_proc_file_path_for_reading (host_name_path);

  pos_buffer = 0;
  while ((ch = getc_unlocked (file)) != EOF && ch != '\n')
    push_buffer (ch);

  push_buffer (0);

  (void) fclose (file);

  return buffer;
}

/*------------------------------------------------------------------------*/

static long page_size;
static long clock_ticks;

static double memory_per_page;
static double physical_memory;

static void
get_page_size ()
{
  page_size = (long) sysconf (_SC_PAGE_SIZE);
  if (page_size <= 0) page_size = 4096;
  memory_per_page = page_size / (double)(1<<20);
  debug ("page size", "%ld bytes", page_size);
  debug ("memory per page", "%g MB", memory_per_page);
}

static void
get_physical_memory ()
{
  long tmp;
  assert (page_size > 0);
  tmp = page_size * sysconf (_SC_PHYS_PAGES);
  physical_memory = tmp / (double)(1<<20);
  debug ("physical memory", "%.0f MB", physical_memory);
}

#ifndef HZ
#define HZ 100
#endif

static void
get_clock_ticks ()
{
  clock_ticks = sysconf (_SC_CLK_TCK);
  if (clock_ticks <= 0) clock_ticks = HZ;
  debug ("clock ticks", "%ld", clock_ticks);
}

/*------------------------------------------------------------------------*/

static int child_pid = -1;
static int parent_pid = -1;
static int group_pid = -1;
static int session_pid = -1;

/*------------------------------------------------------------------------*/

static long num_samples;
static long num_reports;

static long num_samples_since_last_report;

static double max_time;
static double max_memory;

static double max_load;

/*------------------------------------------------------------------------*/

static int single;
static int propagate_signals;
static int propagate_exit_code;
static int children;

/*------------------------------------------------------------------------*/

/* Do 'man 5 proc' and search for 'proc..pid..stat' for explanations. */

#define PID_POS 1
#define COMM_POS 2
#define PPID_POS 4
#define PGID_POS 5
#define SESSION_POS 6
#define UTIME_POS 14
#define STIME_POS 15
#define RSS_POS 24
#define MAX_POS 24

/*------------------------------------------------------------------------*/

static double start_time;
static double start_time_tai;
static double time_limit;
static double real_time_limit;
static double space_limit;

/*------------------------------------------------------------------------*/

static Process ** process_hash_table;
static size_t size_of_process_hash_table;
static size_t processes;

#define PRIME1 10007
#define PRIME2 27

static size_t hash_process_id (int pid)
{
  return PRIME1 * (size_t) pid;
}

#ifndef NDEBUG

static int is_power_of_two (size_t n)
{
  return n && !(n & (n-1));
}

#endif

static size_t mod_size_of_process_hash_table (size_t n)
{
  assert (is_power_of_two (size_of_process_hash_table));
  return n & (size_of_process_hash_table - 1);
}

static Process **
look_up_process_in_process_hash_table (int pid)
{
  size_t hash, pos;
  Process ** res, * process;

  assert (size_of_process_hash_table > processes);

  hash = hash_process_id (pid);
  pos = mod_size_of_process_hash_table (hash);

  for (;;)
    {
      res = process_hash_table + pos;
      process = *res;
      if (!process)
	return res;
      if (process->pid == pid)
	return res;
      pos = mod_size_of_process_hash_table (pos + PRIME2);
    }
}

static void
resize_process_hash_table (void)
{
  Process **  old_process_hash_table, * process, ** p;
  size_t old_size_of_process_hash_table, pos;
  int pid;

  old_size_of_process_hash_table = size_of_process_hash_table;
  old_process_hash_table = process_hash_table;

  size_of_process_hash_table = 2*old_size_of_process_hash_table;
  if (!size_of_process_hash_table)
    size_of_process_hash_table = 2;

  debug ("resize", "%zu", size_of_process_hash_table);

  process_hash_table =
    calloc (size_of_process_hash_table, sizeof (Process));
  if (!process_hash_table)
    error ("could not resize process hash table");

  for (pos = 0; pos < old_size_of_process_hash_table; pos++)
    {
      process = old_process_hash_table[pos];
      if (!process)
	continue;
      pid = process->pid;
      p = look_up_process_in_process_hash_table (pid);
      *p = process;
    }
  free (old_process_hash_table);
}

static Process *
find_process (int pid)
{
  Process * res, ** p;

  if (processes >= size_of_process_hash_table/2)
    resize_process_hash_table ();

  p = look_up_process_in_process_hash_table (pid);
  assert (p);
  res = *p;

  res = *p;
  if (res)
    {
      assert (res->pid == pid);
      return res;
    }

  debug ("insert", "%d", pid);

  res = malloc (sizeof * res);
  if (!res)
    error ("could not allocate process data");

  memset (res, 0, sizeof *res);
  res->pid = pid;

  *p = res;
  processes++;

  return res;
}

/*------------------------------------------------------------------------*/

static Process * active_processes;
static Process * last_active_process;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int killing;

static void
add_process (pid_t pid, pid_t ppid, double time, double memory)
{
  const char * type;
  Process * p;

  assert (0 < pid);
  assert (0 <= ppid); 

  p = find_process (pid);
  
  if (p->active)
    {
      p->new = 0;

      assert (p->pid == pid);
      if (p->ppid != ppid)
	{
	  p->ppid = ppid;
	  type = "add (new parent)";
	}
      else
	type = "add";

      p->time = time;
      p->memory = memory;
    }
  else
    {
      type = "add (new)";
      p->new = 1;
      p->active = 1;
      p->pid = pid;
      p->ppid = ppid;
      p->time = time;
      p->memory = memory;
      p->next_process = 0;

      if (last_active_process)
	last_active_process->next_process = p;
      else
	{
	  assert (!active_processes);
	  active_processes = p;
	}

      last_active_process = p;
    }

  debug (type,
    "%d (parent %d, %.3f sec, %.3f MB)",
    pid, ppid, time, memory);

  p->sampled = num_samples;
}

/*------------------------------------------------------------------------*/

#ifndef NDEBUG
static int parsed;
#endif

#define FAILED \
do { \
  fclose (file); \
  return 0; \
} while (0)

#define READ(POS,TYPE,NAME,FMT) \
  TYPE NAME; \
  do { \
    if (fscanf (file, FMT, &NAME) != 1) \
      FAILED; \
    assert (++parsed == (POS)); \
  } while (0)

#define IGNR(POS,TYPE,NAME,FMT) \
  do { \
    TYPE NAME; \
    if (fscanf (file, FMT, &NAME) != 1) \
      FAILED; \
    (void) NAME; \
    assert (++parsed == (POS)); \
  } while (0)

#define COMM(POS) \
do { \
  if (getc (file) != ' ') \
    FAILED; \
  if (getc (file) != '(') \
    FAILED; \
  int ch; \
  while ((ch = getc (file)) != ')') \
    if (ch == EOF) \
      FAILED; \
  assert (++parsed == (POS)); \
} while (0)

static int
read_process (long pid)
{
  char path[64];
  FILE *file;
  sprintf (path, "/proc/%ld/stat", pid);
  file = fopen (path, "r");
  if (!file)
    return 0;
#ifndef NDEBUG
  parsed = 0;
#endif
  READ (1, int, rid, "%d");
  if (rid != pid)
    FAILED;
  COMM (2);
  if (getc (file) != ' ')
    FAILED;
  IGNR (3, char, state, "%c");
  READ (4, int, ppid, "%d");
  READ (5, int, pgrp, "%d");
  READ (6, int, session, "%d");
  debug ("read", "pid=%d ppid=%d pgrp=%d session=%d", pid, ppid, pgrp, session);
  if (pgrp != pid && pgrp != parent_pid &&
      pgrp != group_pid && session != session_pid)
    FAILED;
  IGNR (7, int, tty_nr, "%d");
  IGNR (8, int, tpgid, "%d");
  IGNR (9, unsigned int, flags, "%u");
  IGNR (10, unsigned long, minflt, "%lu");
  IGNR (11, unsigned long, cminflt, "%lu");
  IGNR (12, unsigned long, majflt, "%lu");
  IGNR (13, unsigned long, cmajflt, "%lu");
  READ (14, unsigned long, utime, "%lu");
  if (utime < 0)
    FAILED;
  READ (15, unsigned long, stime, "%lu");
  if (stime < 0)
    FAILED;
  IGNR (16, long, cutime, "%ld");
  IGNR (17, long, cstime, "%ld");
  IGNR (18, long, priority, "%ld");
  IGNR (19, long, nice, "%ld");
  IGNR (20, long, num_threads, "%ld");
  IGNR (21, long, itrealvalue, "%ld");
  IGNR (22, unsigned long long, starttime, "%llu");
  IGNR (23, unsigned long, vsize, "%lu");
  READ (24, long, rss, "%ld");
  if (rss < 0)
    FAILED;
  fclose (file);
  debug ("utime", "%f microseconds", utime);
  debug ("stime", "%f microseconds", stime);
  const double time = (utime + stime) / (double) clock_ticks;
  const double memory = rss * memory_per_page;
  add_process (pid, ppid, time, memory);
  return 1;
}

/*------------------------------------------------------------------------*/

static void
read_parent_status_and_mount_proc_file_system_if_necessary (void)
{
  char path[64];
  FILE *file;

  sprintf (path, "/proc/%ld/stat", (long) parent_pid);
  file = fopen (path, "r");
  if (file)
    fclose (file);
  else
    (void) try_to_remount_proc_file_system ();
}

static long
read_all_processes (void)
{
  struct dirent *de;
  long pid;
  DIR *dir;

  long res = 0;

  dir = opendir ("/proc");
  if (!dir)
    {
      if (try_to_remount_proc_file_system ())
	dir = opendir ("/proc");
      if (!dir)
	error ("can not open directory '/proc'");
    }

  read_parent_status_and_mount_proc_file_system_if_necessary ();

  while ((de = readdir (dir)) != NULL)
    {
      if (!is_positive_long (de->d_name, &pid)) continue;
      if (pid <= 0) continue;
      if (pid == parent_pid) continue;
      if (read_process (pid)) res++;
    }
  
  (void) closedir (dir);
  debug ("added", "%ld processes", res);

  return res;
}

static long
read_processes (void) {
  if (single) return read_process (child_pid);
  else return read_all_processes ();
}

static void
clear_tree_connections (Process * p)
{
  p->parent = p->first_child = p->last_child = p->next_sibbling = 0;
}

static void
connect_process_tree (void)
{
  Process * p, * parent;
  long connected = 0;

  for (p = active_processes; p; p = p->next_process)
    {
      assert (p->active);
      assert (find_process (p->pid) == p);
      parent = find_process (p->ppid);
      clear_tree_connections (parent);
      clear_tree_connections (p);
    }

  for (p = active_processes; p; p = p->next_process)
    {
      if (p->pid == child_pid) continue;
      assert (p->pid != parent_pid);
      parent = find_process (p->ppid);
      p->parent = parent;
      if (parent->first_child) {
	assert (parent->last_child);
	assert (!parent->last_child->next_sibbling);
	parent->last_child->next_sibbling = p;
	parent->last_child = p;
	assert (!p->next_sibbling);
      } else {
	assert (!parent->last_child);
        parent->first_child = parent->last_child = p;
	assert (!p->next_sibbling);
      }
      debug ("connect", "%d -> %d", p->ppid, p->pid);
      connected++;
    }

  debug ("connected", "%ld processes", connected);
}

/*------------------------------------------------------------------------*/

static double accumulated_time;

/*------------------------------------------------------------------------*/

static long
flush_inactive_processes (void)
{
  Process * prev = 0;
  Process * next;
  long res = 0;
  Process * p;

  for (p = active_processes; p; p = next)
    {
      assert (p->active);

      next = p->next_process;

      if (p->sampled == num_samples)
	{
	  prev = p;
	}
      else
	{
	  p->active = 0;

	  if (prev)
	    prev->next_process = next;
	  else
	    active_processes = next;

	  debug ("deactive", "%d (%.3f sec)", p->pid, p->time);
	  accumulated_time += p->time;
	  p->next_process = 0;
	  res++;
	}
    }

  last_active_process = prev;

  debug ("flushed", "%ld processes", res);

  return res;
}

/*------------------------------------------------------------------------*/

static double sampled_time;
static double sampled_memory;

/*------------------------------------------------------------------------*/

static long
sample_recursively (Process * p)
{
  const char * type;
  Process * child;
  long res = 0;

  if (p->cyclic_sampling)
    {
      warning ("cyclic process dependencies during sampling");
      return 0;
    }

  if (p->sampled == num_samples)
    {
      if (p->new)
	{
	  children++;
	  type = "sampling (new)";
	}
      else
	type = "sampling";

      sampled_time += p->time;
      sampled_memory += p->memory;

      res++;
      debug (type,
        "%d (%.3f sec, %.3f MB)", p->pid, p->time, p->memory);
    }

  p->cyclic_sampling = 1;

  for (child = p->first_child; child; child = child->next_sibbling)
    res += sample_recursively (child);

  assert (p->cyclic_sampling);
  p->cyclic_sampling = 0;
  
  return res;
}

/*------------------------------------------------------------------------*/

static struct itimerval timer;
static struct itimerval old_timer;

static volatile int caught_out_of_memory;
static volatile int caught_out_of_time;

/*------------------------------------------------------------------------*/

static void
term_process (Process * p)
{
  assert (p->pid != parent_pid);
  debug ("kill with SIGTERM ", "%d", p->pid);
  kill (p->pid, SIGTERM);
}

static void
kill_process (Process * p)
{
  assert (p->pid != parent_pid);
  debug ("kill with SIGKILL ", "%d", p->pid);
  kill (p->pid, SIGKILL);
}

static long
kill_recursively (Process * p, void(*killer)(Process *))
{
  Process * child;
  long res = 0;

  if (p->cyclic_killing)
    return 0;

  p->cyclic_killing = 1;
  for (child = p->first_child; child; child = child->next_sibbling)
    res += kill_recursively (child, killer);
  assert (p->cyclic_killing);
  p->cyclic_killing = 0;

  killer (p);
  res++;

  return res;
}

static long kill_delay = KILL_DELAY;

static void
kill_all_child_processes (void)
{
  static void (*killer) (Process *);
  long ms = kill_delay * 1000;
  long rounds = 0;
  Process * p;
  long killed;
  int ignore;
  long read;

  assert (getpid () == parent_pid);

  pthread_mutex_lock (&mutex);
  if (!(ignore = killing)) killing = 1;
  pthread_mutex_unlock (&mutex);
  if (ignore) return;

  debug ("killing", "all child processes");

  for (;;)
    {
      if (ms >= 2000) killer = term_process;
      else            killer = kill_process;

      read = read_processes ();

      killed = 0;

      if (read > 0)
	{
	  connect_process_tree ();
	  p = find_process (child_pid);
	  if (p->active)
	    killed = kill_recursively (p, killer);
	}

      debug ("killed", "%ld processes", killed);

      if (!killed) break;
      if (ms <= 1000) break;

      rounds++;

      usleep (ms);
      ms /= 2;
    }
}

/*------------------------------------------------------------------------*/

static double
wall_clock_time (void)
{
  double res = -1;
  struct timeval tv;
  if (!gettimeofday(&tv, 0))
    {
      res = 1e-6 * tv.tv_usec;
      res += tv.tv_sec;
    }
  return res;
}

static double
tai_time (void)
{
  double res = -1;
  struct timespec ts;
  if (!clock_gettime(CLOCK_TAI, &ts))
    {
      res = 1e-9 * ts.tv_nsec;
      res += ts.tv_sec;
    }
  return res;
}

/*------------------------------------------------------------------------*/

static double
real_time (void) 
{
  double res;
  if (start_time_tai < 0) return -1;
  res = tai_time() - start_time_tai;
  return res;
}

/*------------------------------------------------------------------------*/

static void
report (double time, double memory, double load)
{
  double real = real_time ();
  message ("sample", "%.2f time, %.2f real, %.0f MB, %.2f load",
           time, real, memory, load);
  num_reports++;
}

/*------------------------------------------------------------------------*/

void print_process_tree (Process * p)
{
  Process * c;
  debug ("edge", "%d -> %d", p->ppid, p->pid);
  for (c = p->first_child; c; c = c->next_sibbling)
    print_process_tree (c);
}

/*------------------------------------------------------------------------*/

static double
sample_load (void)
{
  double load;
  int res = getloadavg (&load, 1);
  if (res != 1)
    return 0;
  if (load > max_load)
    max_load = load;
  return load;
}

/*------------------------------------------------------------------------*/

static long sample_rate = SAMPLE_RATE;
static long report_rate = REPORT_RATE;

static void
sample_all_child_processes (int s)
{
  long sampled, read;
  double load;
  Process * p;
  int ignore;

  assert (s == SIGALRM);
  assert (getpid () == parent_pid);

  pthread_mutex_lock (&mutex);
  ignore = killing;
  pthread_mutex_unlock (&mutex);
  
  if (ignore) return;

  load = sample_load ();

  num_samples++;

  read = read_processes ();
  connect_process_tree ();

  sampled_time = sampled_memory = 0;

  if (read > 0)
    {
      p = find_process (child_pid);
      sampled = sample_recursively (p);
    }
  else
    sampled = 0;

  debug ("sampled", "%ld processes", sampled);

  sampled += flush_inactive_processes ();
  sampled_time += accumulated_time;

  if (sampled > 0)
    { 
      if (sampled_memory > max_memory)
	max_memory = sampled_memory;

      if (sampled_time > max_time)
	max_time = sampled_time;
    }

  if (++num_samples_since_last_report >= report_rate)
    {
      num_samples_since_last_report = 0;
      if (sampled > 0)
	{
	  print_process_tree (find_process (child_pid));
	  report (sampled_time, sampled_memory, load);
	}
    }

  if (sampled > 0)
    {
      if (sampled_time > time_limit || real_time () > real_time_limit)
	{
	  if (!caught_out_of_time)
	    {
	      caught_out_of_time = 1;
	      kill_all_child_processes ();
	    }
	}
      else if (sampled_memory > space_limit)
	{
	  if (!caught_out_of_memory)
	    {
	      caught_out_of_memory = 1;
	      kill_all_child_processes ();
	    }
	}
    }
}

/*------------------------------------------------------------------------*/

static volatile int caught_usr1_signal;
static volatile int caught_other_signal;

static pthread_mutex_t caught_other_signal_mutex = PTHREAD_MUTEX_INITIALIZER;

/*------------------------------------------------------------------------*/

static void
sig_usr1_handler (int s)
{
  assert (s == SIGUSR1);
  caught_usr1_signal = 1;
}

static void (*old_sig_int_handler);
static void (*old_sig_segv_handler);
static void (*old_sig_kill_handler);
static void (*old_sig_term_handler);
static void (*old_sig_abrt_handler);

static void restore_signal_handlers ()
{
  (void) signal (SIGINT, old_sig_int_handler);
  (void) signal (SIGSEGV, old_sig_segv_handler);
  (void) signal (SIGKILL, old_sig_kill_handler);
  (void) signal (SIGTERM, old_sig_term_handler);
  (void) signal (SIGABRT, old_sig_abrt_handler);
}

static void
sig_other_handler (int s)
{
  int already_caught;
  pthread_mutex_lock (&caught_other_signal_mutex);
  already_caught = caught_other_signal;
  caught_other_signal = 1;
  pthread_mutex_unlock (&caught_other_signal_mutex);
  if (already_caught) return;
  restore_signal_handlers ();
  kill_all_child_processes ();
  usleep (1000);
  // raise (s);
}

/*------------------------------------------------------------------------*/

static const char *
ctime_without_new_line (time_t * t)
{
  const char * str, * p;
  str = ctime (t);
  pos_buffer = 0;
  for (p = str; *p && *p != '\n'; p++)
    push_buffer (*p);
  push_buffer (0);
  return buffer;
}

/*------------------------------------------------------------------------*/

int
main (int argc, char **argv)
{
  const char * log_name = 0, * tmp_name;
  int i, j, res, status, s, ok;
  char signal_description[80];
  const char * description;
  double real;
  time_t t;

  log = stderr;
  assert (!close_log);

  for (i = 1; i < argc; i++)
    {
      if (argv[i][0] == '-')
	{
	  tmp_name = 0;

	  switch (argv[i][1])
	    {
	      case 'o':
	        if (++i == argc)
		  error ("file argument to '-o' missing (try '-h')");
		tmp_name = argv[i];
	        break;

	      case 's':
	      case 't':
	        i++;
		continue;

	      case 'd':
	      case 'h':
	      case 'k':
	      case 'p':
	        continue;

	      case '-':
	        if (strstr (argv[i], "--output-file=") == argv[i])
		  {
		    tmp_name = strchr (argv[i], '=');
		    assert (tmp_name);
		    assert (*tmp_name == '=');
		    tmp_name++;
		    break;
		  }
		else
		  continue;
	    }

	  if (log_name)
	    error ("multiple output files '%s' and '%s'",
	      log_name, tmp_name);

	  assert (tmp_name);
	  log_name = tmp_name;
	  log = fopen (log_name, "w");
	  if (!log)
	    error ("can not write output to '%s'", log_name);
	  close_log = 1;
	}
      else
	break;
    }

  get_page_size ();
  get_physical_memory ();
  get_clock_ticks ();

  ok = OK;				/* status of the runlim */
  s = 0;				/* signal caught */

  time_limit = 60 * 60 * 24 * 3600;	/* one year */
  real_time_limit = time_limit;		/* same as time limit by default */
  space_limit = physical_memory;

  for (i = 1; i < argc; i++)
    {
      if (argv[i][0] == '-')
	{
	  if (argv[i][1] == 'o')
	    {
	      assert (close_log);
	      i++;
	      assert (i < argc);
	    }
	  else if (argv[i][1] == 't')
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
	  else if (strstr (argv[i], "--output-file=") == argv[i])
	    {
	      assert (close_log);
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
	  else if (strstr (argv[i], "--sample-rate=") == argv[i])
	    {
	      sample_rate = parse_number_rhs (argv[i]);
	      if (sample_rate <= 0)
		error ("invalid sample rate '%ld'", sample_rate);
	    }
	  else if (strstr (argv[i], "--report-rate=") == argv[i])
	    {
	      report_rate = parse_number_rhs (argv[i]);
	      if (report_rate <= 0)
		error ("invalid report rate '%ld'", report_rate);
	    }
	  else if (strstr (argv[i], "--kill-delay=") == argv[i])
	    {
	      kill_delay = parse_number_rhs (argv[i]);
	      if (kill_delay <= 0 || kill_delay >= 1e6)
		error ("invalid kill delay '%ld'", kill_delay);
	    }
	  else if (strcmp (argv[i], "-v") == 0 ||
	           strcmp (argv[i], "--version") == 0)
	    {
	      printf ("%s\n", VERSION);
	      fflush (stdout);
	      exit (0);
	    }
	  else if (strcmp (argv[i], "-d") == 0 ||
	           strcmp (argv[i], "--debug") == 0)
	    {
	      debug_messages = 1;
	    }
	  else if (strcmp (argv[i], "--single") == 0)
	    {
	      single = 1;
	    }
	  else if (strcmp (argv[i], "-k") == 0 ||
	           strcmp (argv[i], "--kill") == 0)
	    {
	      propagate_signals = 1;
	    }
	  else if (strcmp (argv[i], "-p") == 0 ||
	           strcmp (argv[i], "--propagate") == 0)
	    {
	      propagate_exit_code = 1;
	    }
	  else if (strcmp (argv[i], "-h") == 0 ||
	           strcmp (argv[i], "--help") == 0)
	    {
	      usage ();
	      exit (0);
	    }
	  else
	    error ("invalid option '%s' (try '-h')", argv[1]);
	}
      else
	break;
    }

  if (i >= argc)
    error ("no program specified (try '-h')");

  message ("version", "%s", VERSION);
  message ("host", "%s", read_host_name ());
  message ("time limit", "%.0f seconds", time_limit);
  message ("real time limit", "%.0f seconds", real_time_limit);
  message ("space limit", "%.0f MB", space_limit);

  for (j = i; j < argc; j++)
    {
      char argstr[80];
      sprintf (argstr, "argv[%d]", j - i);
      message (argstr, "%s", argv[j]);
    }

  t = time (0);
  message ("start", "%s", ctime_without_new_line (&t));

  (void) signal (SIGUSR1, sig_usr1_handler);

  start_time_tai = tai_time();
  start_time = wall_clock_time();

  parent_pid = getpid ();
  group_pid = getpgid (0);
  session_pid = getsid (0);
  child_pid = fork ();

  if (child_pid != 0)
    {
      if (child_pid < 0)
	{
	  ok = FORK_FAILED;
	  res = 1;
	}
      else
	{
	  status = 0;

	  old_sig_int_handler = signal (SIGINT, sig_other_handler);
	  old_sig_segv_handler = signal (SIGSEGV, sig_other_handler);
	  old_sig_kill_handler = signal (SIGKILL, sig_other_handler);
	  old_sig_term_handler = signal (SIGTERM, sig_other_handler);
	  old_sig_abrt_handler = signal (SIGABRT, sig_other_handler);

	  message ("child", "%d", child_pid);
	  debug ("group", "%d", group_pid);
	  debug ("session", "%d", session_pid);
	  debug ("parent", "%d", parent_pid);

	  usleep (10000);

	  timer.it_interval.tv_sec  = sample_rate / 1000000;
	  timer.it_interval.tv_usec = sample_rate % 1000000;
	  timer.it_value = timer.it_interval;

	  signal (SIGALRM, sample_all_child_processes);
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
      execvp (argv[i], argv + i);
      kill (getppid (), SIGUSR1);		// TODO DOES THIS WORK?
      exit (1);
    }

  real = real_time ();

  if (caught_usr1_signal)
    ok = EXEC_FAILED;
  else if (caught_out_of_memory)
    ok = OUT_OF_MEMORY;
  else if (caught_out_of_time)
    ok = OUT_OF_TIME;

  kill_all_child_processes ();

  t = time (0);
  message ("end", "%s", ctime_without_new_line (&t));

  if (max_time >= time_limit || real_time () >= real_time_limit)
    goto FORCE_OUT_OF_TIME_ENTRY;

  switch (ok)
    {
    case OK:
      description = "ok";
      break;
    case OUT_OF_TIME:
FORCE_OUT_OF_TIME_ENTRY:
      description = "out of time";
      res = 2;
      break;
    case OUT_OF_MEMORY:
      description = "out of memory";
      res = 3;
      break;
    case SEGMENTATION_FAULT:
      description = "segmentation fault";
      res = 4;
      break;
    case BUS_ERROR:
      description = "bus error";
      res = 5;
      break;
    case FORK_FAILED:
      description = "fork failed";
      res = 6;
      break;
    case INTERNAL_ERROR:
      description = "internal error";
      res = 7;
      break;
    case EXEC_FAILED:
      description = "execvp failed";
      res = 1;
      break;
    default:
      sprintf (signal_description, "signal(%d)", s);
      description = signal_description;
      res = 11;
      break;
    }

  message ("status", description);
  message ("result", "%d", res);
  message ("children", "%d", children);
  message ("processes", "%d", processes);
  message ("real", "%.2f seconds", real);
  message ("time", "%.2f seconds", max_time);
  message ("space", "%.0f MB", max_memory);
  message ("load","%.2f maximum", max_load);
  message ("samples", "%ld", num_samples);
  debug ("reports", "%ld", num_samples);

  if (ok == OK && !propagate_exit_code)
    res = 0;

  if (close_log)
    {
      log = stderr;
      if (fclose (log))
	warning ("could not close log file");
    }

  if (buffer)
    free (buffer);

  if (process_hash_table)
    {
      for (size_t pos = 0; pos < size_of_process_hash_table; pos++)
	if (process_hash_table[pos])
	  free (process_hash_table[pos]);

      free (process_hash_table);
    }

  restore_signal_handlers ();

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
	  raise (s);
	  break;
	}
    }

  return res;
}
