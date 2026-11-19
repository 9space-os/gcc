/* APE's stdio from several threads (9space: each FILE locked once a
 * second thread runs, sys/src/ape/lib/ap/stdio/flockfile.c): whole
 * lines from fprintf and fputs, every byte read by getc exactly once,
 * flockfile keeping a group of putc_unlocked together, and _exit ending
 * the other threads. Prints each failure and "stdiothreads: n failed". */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

enum { N = 8, LINES = 2000, BYTES = 40000, R = 4 };

static int failed;
static FILE *out;
static FILE *in;

static void
check(const char *what, int ok)
{
	if(!ok){
		printf("stdiothreads: FAIL %s\n", what);
		failed++;
	}
}

static void *
printer(void *v)
{
	int t, i;

	t = (int)(long)v;
	for(i = 0; i < LINES; i++)
		fprintf(out, "thread %d line %d %s\n", t, i, "abcdefghijklmnopqrstuvwxyz0123456789");
	return NULL;
}

static void *
putser(void *v)
{
	char line[80];
	int t, i;

	t = (int)(long)v;
	snprintf(line, sizeof line, "fputs %d %s\n", t, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	for(i = 0; i < LINES; i++)
		fputs(line, out);
	return NULL;
}

static void *
grouper(void *v)
{
	int t, i, j;

	t = (int)(long)v;
	for(i = 0; i < LINES/10; i++){
		flockfile(out);
		for(j = 0; j < 40; j++)
			putc_unlocked('a' + t, out);
		putc_unlocked('\n', out);
		funlockfile(out);
	}
	return NULL;
}

static long hist[R][256];

static void *
reader(void *v)
{
	int r, c;

	r = (int)(long)v;
	while((c = getc(in)) != EOF)
		hist[r][c]++;
	return NULL;
}

/* run fn in N threads writing to a fresh file; then check each line */
static void
lines(const char *name, void *(*fn)(void *), int n, int (*good)(const char *))
{
	pthread_t t[N];
	char buf[256];
	int i, nl, bad;

	out = fopen("/tmp/stdiothreads.out", "w");
	check("fopen out", out != NULL);
	if(out == NULL)
		return;
	for(i = 0; i < N; i++)
		pthread_create(&t[i], NULL, fn, (void *)(long)i);
	for(i = 0; i < N; i++)
		pthread_join(t[i], NULL);
	fclose(out);
	in = fopen("/tmp/stdiothreads.out", "r");
	nl = bad = 0;
	while(fgets(buf, sizeof buf, in) != NULL){
		nl++;
		if(!good(buf))
			bad++;
	}
	fclose(in);
	snprintf(buf, sizeof buf, "%s: %d lines (want %d), %d torn", name, nl, n, bad);
	check(buf, nl == n && bad == 0);
}

static int
goodprint(const char *s)
{
	int t, i, k;
	char rest[64];

	k = sscanf(s, "thread %d line %d %63s", &t, &i, rest);
	return k == 3 && t >= 0 && t < N && i >= 0 && i < LINES
		&& strcmp(rest, "abcdefghijklmnopqrstuvwxyz0123456789") == 0
		&& s[strlen(s)-1] == '\n';
}

static int
goodputs(const char *s)
{
	int t;
	char rest[64];

	return sscanf(s, "fputs %d %63s", &t, rest) == 2 && t >= 0 && t < N
		&& strcmp(rest, "ABCDEFGHIJKLMNOPQRSTUVWXYZ") == 0;
}

static int
goodgroup(const char *s)
{
	int i;

	if(strlen(s) != 41 || s[40] != '\n' || s[0] < 'a' || s[0] >= 'a' + N)
		return 0;
	for(i = 1; i < 40; i++)
		if(s[i] != s[0])
			return 0;
	return 1;
}

static void *
sleeper(void *v)
{
	int *fd;

	fd = v;
	/* tell the parent this proc's pid, then wait for good */
	write(fd[1], &(int){getpid()}, sizeof(int));
	for(;;)
		sleep(1000);
	return NULL;
}

/* a child with a second thread calls _exit: both procs must go */
static void
exitall(void)
{
	char path[64];
	int tc[2], cp[2], pid, tpid, st, i, gone;
	pthread_t t;

	/* the thread tells the child its pid (tc), the child tells the parent (cp) */
	pipe(tc);
	pipe(cp);
	pid = fork();
	if(pid == 0){
		pthread_create(&t, NULL, sleeper, tc);
		read(tc[0], &tpid, sizeof tpid);	/* the thread runs */
		write(cp[1], &tpid, sizeof tpid);
		_exit(3);
	}
	read(cp[0], &tpid, sizeof tpid);
	check("waitpid", waitpid(pid, &st, 0) == pid);
	check("_exit status 3", WIFEXITED(st) && WEXITSTATUS(st) == 3);
	snprintf(path, sizeof path, "/proc/%d/status", tpid);
	gone = 0;
	for(i = 0; i < 50 && !gone; i++){
		gone = access(path, 0) < 0;
		if(!gone)
			usleep(100000);
	}
	check("_exit ended the other thread", gone);
	if(!gone){
		/* do not leave it behind */
		snprintf(path, sizeof path, "/proc/%d/ctl", tpid);
		if((i = open(path, O_WRONLY)) >= 0){
			write(i, "kill", 4);
			close(i);
		}
	}
}

int
main(void)
{
	pthread_t t[R];
	long total, sum[256];
	int i, c;

	lines("fprintf", printer, N * LINES, goodprint);
	lines("fputs", putser, N * LINES, goodputs);
	lines("flockfile", grouper, N * (LINES/10), goodgroup);

	/* getc: every byte once */
	out = fopen("/tmp/stdiothreads.in", "w");
	for(i = 0; i < BYTES; i++)
		putc(i % 251, out);
	fclose(out);
	in = fopen("/tmp/stdiothreads.in", "r");
	for(i = 0; i < R; i++)
		pthread_create(&t[i], NULL, reader, (void *)(long)i);
	for(i = 0; i < R; i++)
		pthread_join(t[i], NULL);
	fclose(in);
	total = 0;
	memset(sum, 0, sizeof sum);
	for(i = 0; i < R; i++)
		for(c = 0; c < 256; c++){
			sum[c] += hist[i][c];
			total += hist[i][c];
		}
	check("getc: every byte once (total)", total == BYTES);
	for(c = 0; c < 251; c++)
		if(sum[c] != BYTES/251 + (c < BYTES%251)){
			check("getc: every byte once (counts)", 0);
			break;
		}

	exitall();

	remove("/tmp/stdiothreads.out");
	remove("/tmp/stdiothreads.in");
	printf("stdiothreads: %d failed\n", failed);
	return failed != 0;
}
