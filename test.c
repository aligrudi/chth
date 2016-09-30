/* Challenging Thursdays Judge */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#define TESTUID		12345
#define TESTGID		12345
#define TIMEOUT		2000		/* process timeout in milliseconds */
#define WAITDELAY	5		/* delay after each waitpid() in milliseconds */
#define LLEN		256
#define MAXMEM		(500l << 20)	/* memory limit */
#define MAXPROC		(12)		/* process count limit */
#define MAXFILE		(12)		/* file count limit */
#define MAXFILESIZE	(1l << 30)	/* file size limit */

/* current time stamp in milliseconds */
static long util_ts(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* return nonzero for regular files */
static int util_isfile(char *path)
{
	struct stat st;
	if (stat(path, &st) < 0)
		return 0;
	return S_ISREG(st.st_mode);
}

/* return nonzero for directories */
static int util_isdir(char *path)
{
	struct stat st;
	if (stat(path, &st) < 0)
		return 0;
	return S_ISDIR(st.st_mode);
}

/* return zero if the given files match */
static int util_cmp(char *path1, char *path2)
{
	char l1[1024], l2[1024];
	FILE *f1, *f2;
	int equal = 1;
	f1 = fopen(path1, "r");
	f2 = fopen(path2, "r");
	if (!f1 || !f2)
		equal = 0;
	while (equal) {
		char *r1 = fgets(l1, sizeof(l1), f1);
		char *r2 = fgets(l2, sizeof(l2), f2);
		if (!r1 || !r2) {
			equal = !r1 && !r2;
			break;
		}
		equal = !strcmp(l1, l2);
	}
	if (f1)
		fclose(f1);
	if (f2)
		fclose(f2);
	return !equal;
}

/* copy spath into dpath */
static int util_cp(char *spath, char *dpath)
{
	FILE *src = fopen(spath, "r");
	FILE *dst = fopen(dpath, "w");
	char buf[1024];
	int failed = 0;
	if (!src || !dst) {
		failed = 1;
		return 1;
	}
	while (!failed && fgets(buf, sizeof(buf), src))
		if (fputs(buf, dst) < 0)
			failed = 1;
	if (src)
		fclose(src);
	if (dst)
		fclose(dst);
	return failed;
}

static char *getinterpreter(char *lang)
{
	if (lang && !strcmp("sh", lang))
		return "sh";
	if (lang && !strcmp("py", lang))
		return "python";
	if (lang && !strcmp("py2", lang))
		return "python2";
	if (lang && !strcmp("py3", lang))
		return "python3";
	return NULL;
}

static char *getcompiler(char *lang)
{
	if (lang && !strcmp("c", lang))
		return "cc";
	if (lang && (!strcmp("c++", lang) || !strcmp("cpp", lang)))
		return "c++";
	return NULL;
}

/* kill all processes owned by TESTUID */
static void util_slaughter(void)
{
	int i;
	for (i = 0; i < 3; i++) {
		int pid = fork();
		if (!pid) {
			if (setgid(TESTGID) || setuid(TESTUID))
				exit(1);
			kill(-1, SIGKILL);
			exit(0);
		}
		if (pid > 0)
			waitpid(pid, NULL, 0);
	}
}

/* execute epath, with ipath as stdin and opath as stdout; return NULL on success */
static char *ct_exec(char **argv, char *tdir, char *ipath, char *opath, char *epath)
{
	int pid, st;
	int ret = 0;
	struct rlimit rlp;
	if (!(pid = fork())) {
		chdir(tdir);
		nice(1);
		rlp.rlim_cur = MAXFILE;
		rlp.rlim_cur = MAXFILE;
		setrlimit(RLIMIT_NOFILE, &rlp);
		rlp.rlim_cur = MAXFILESIZE;
		rlp.rlim_cur = MAXFILESIZE;
		setrlimit(RLIMIT_FSIZE, &rlp);
		rlp.rlim_cur = MAXMEM;
		rlp.rlim_cur = MAXMEM;
		setrlimit(RLIMIT_AS, &rlp);
		rlp.rlim_cur = MAXPROC;
		rlp.rlim_cur = MAXPROC;
		setrlimit(RLIMIT_NPROC, &rlp);
		if (setgid(TESTUID) || setuid(TESTUID))
			exit(1);
		close(0);
		open(ipath, O_RDONLY);
		close(1);
		open(opath, O_WRONLY | O_TRUNC | O_CREAT, 0600);
		close(2);
		open(epath, O_WRONLY | O_TRUNC | O_CREAT, 0600);
		execvp(argv[0], argv);
		exit(1);
	}
	if (pid > 0) {
		long beg = util_ts();
		while (util_ts() - beg < TIMEOUT && (ret = waitpid(pid, &st, WNOHANG)) != pid)
			usleep(WAITDELAY * 1000);
		if (ret != pid) {
			util_slaughter();
			kill(pid, SIGKILL);
			while (waitpid(pid, &st, 0) != pid)
				if (util_ts() - beg > TIMEOUT * 2)
					break;
			return "Time Limit Exceeded";
		}
		if (WIFSIGNALED(st))
			return "Runtime Error";
		if (WEXITSTATUS(st))
			return "Runtime Error";
		return NULL;
	}
	return "Fork Failed";
}

static int compilefile(char *src, char *lang, char *out)
{
	char *cc = lang ? getcompiler(lang) : NULL;
	char *argv[] = {cc, "-O2", "-o", out, src, NULL};
	if (cc) {
		int pid = fork();
		int st;
		if (pid < 0)
			return 1;
		if (!pid) {
			if (setgid(TESTUID) || setuid(TESTUID))
				exit(1);
			close(1);
			open("/dev/null", O_WRONLY);
			close(2);
			open("/dev/null", O_WRONLY);
			execvp(argv[0], argv);
			exit(1);
		}
		if (waitpid(pid, &st, 0) != pid)
			return 1;
		return WEXITSTATUS(st);
	}
	return util_cp(src, out);
}

int main(int argc, char *argv[])
{
	char *cont, *prog, *lang, *cmt = NULL;
	char idat[LLEN], odat[LLEN];
	char tdir[LLEN], tdir_i[LLEN], tdir_o[LLEN], tdir_s[LLEN], tdir_x[LLEN];
	char *args[4];
	long beg_ms, end_ms, tot_ms = 0;
	int passes = 0, failed = 0, i;
	if (argc != 4) {
		fprintf(stderr, "usage: %s cont prog lang\n", argv[0]);
		return 1;
	}
	cont = argv[1];
	prog = argv[2];
	lang = argv[3];
	if (!util_isdir(cont)) {
		fprintf(stderr, "nonexistent contest <%s>\n", cont);
		return 1;
	}
	if (!util_isfile(prog)) {
		fprintf(stderr, "nonexistent program <%s>\n", prog);
		return 1;
	}
	snprintf(tdir, sizeof(tdir), "/tmp/ct%06d", getpid());
	snprintf(tdir_i, sizeof(tdir_i), "%s/.i", tdir);
	snprintf(tdir_o, sizeof(tdir_o), "%s/.o", tdir);
	snprintf(tdir_s, sizeof(tdir_s), "%s/p.%s", tdir, lang);
	snprintf(tdir_x, sizeof(tdir_x), "%s/.x", tdir);
	mkdir(tdir, 0700);
	chown(tdir, TESTUID, TESTGID);
	util_cp(prog, tdir_s);
	chown(tdir_s, TESTUID, TESTGID);
	if (compilefile(tdir_s, lang, tdir_x)) {
		failed = 1;
		cmt = "Compilation Error";
	}
	unlink(tdir_s);
	args[0] = getinterpreter(lang) ? getinterpreter(lang) : tdir_x;
	args[1] = getinterpreter(lang) ? tdir_x : NULL;
	args[2] = NULL;
	chmod(tdir_x, 0700);
	for (i = 0; i < 100; i++) {
		snprintf(idat, sizeof(idat), "%s/%02d", cont, i);
		snprintf(odat, sizeof(idat), "%s/%02do", cont, i);
		if (!util_isfile(idat) || !util_isfile(odat))
			break;
		util_cp(idat, tdir_i);
		if (!failed) {
			chown(tdir_i, TESTUID, TESTGID);
			chown(tdir_x, TESTUID, TESTGID);
			chmod(tdir_i, 0600);
			beg_ms = util_ts();
			cmt = ct_exec(args, tdir, ".i", ".o", "/dev/null");
			end_ms = util_ts();
			if (cmt)
				failed = 1;
			if (!cmt && util_isfile(tdir_o) && !util_cmp(odat, tdir_o)) {
				passes++;
				tot_ms += end_ms - beg_ms;
			}
		}
		unlink(tdir_i);
		unlink(tdir_o);
	}
	unlink(tdir_x);
	rmdir(tdir);			/* fails if tdir is not empty */
	if (!cmt)
		cmt = passes == i ? "Success" : "Wrong Answer";
	printf("%d/%d\t%ld.%02ld\t# %s!\n",
		passes, i, tot_ms / 1000, (tot_ms % 1000) / 10, cmt);
	return 0;
}
