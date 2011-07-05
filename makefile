all: runlim
runlim: runlim.c Makefile
	@CC@ @CFLAGS@ -o -DVERSION=\"@VERSION@\" runlim runlim.c
clean:
	rm -f Makefile runlim
	rm -f log/*.exe log/*.out log/*.err log/*.log
.PHONY: all clean
