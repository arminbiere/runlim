# Runlim

This Linux program can be used to run and monitor benchmarks
under resource limits (time and memory usage).

It samples the resource usage of the executed program and
all its child processes and stops the program if resource
limits are exhausted.

In order to support multi-threaded programs a limit on the
wall clock time can be given as well.

Note, that for multi-threaded programs the time spent in each
thread is accumulated by commands like `time`.  This is the
same model we use for multiple processes forked by a program
unless you are only interested in walk clock time.

To compile:

> `./configure.sh && make`

Also see [LICENSE](LICENSE).
