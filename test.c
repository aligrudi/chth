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
#define MAXFILESIZE	(1l << 22)	/* file size limit */

#define LEN(a)		((sizeof(a)) / sizeof((a)[0]))

/* supported languages */
static struct lang {
	char *name;		/* language name */
	char *file;		/* source file name */
	char *exec;		/* executable file name */
	char *intr[16];		/* interpreter arguments (SRC=source) */
	char *comp[16];		/* compiler arguments (OUT=output, SRC=source) */
} langs[] = {
	{"sh", "s.sh", ".x", {"bash", "SRC"}},
	{"py", "s.py", ".x", {"python", "SRC"}},
	{"py2", "s.py", ".x", {"python2", "SRC"}},
	{"py3", "s.py", ".x", {"python3", "SRC"}},
	{"c", "s.c", ".x", {NULL}, {"cc", "-O2", "-pthread", "-o", "OUT", "SRC", "-lm"}},
	{"c++", "s.c++", ".x", {NULL}, {"c++", "-O2", "-pthread", "-o", "OUT", "SRC", "-lm"}},
	{"java", "Main.java", "Main.class", {"java", "-Xms64m", "-Xmx512m", "Main"}, {"javac", "SRC"}},
};

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
	int nr;
	if (!src || !dst)
		failed = 1;
	while (!failed && (nr = fread(buf, 1, sizeof(buf), src)) > 0)
		if (fwrite(buf, 1, nr, dst) < nr)
			failed = 1;
	if (src)
		fclose(src);
	if (dst)
		fclose(dst);
	return failed;
}

static int util_install(char *spath, char *dpath, int usr, int grp, int mod)
{
	if (util_cp(spath, dpath))
		return 1;
	if (chown(dpath, usr, grp))
		return 1;
	if (chmod(dpath, mod))
		return 1;
	return 0;
}

/* return interpreter arguments for the given language */
static char **lang_intr(char *lang)
{
	int i;
	for (i = 0; i < LEN(langs); i++)
		if (!strcmp(langs[i].name, lang))
			return langs[i].intr[0] ? langs[i].intr : NULL;
	return NULL;
}

/* return compiler arguments for the given language */
static char **lang_comp(char *lang)
{
	int i;
	for (i = 0; i < LEN(langs); i++)
		if (!strcmp(langs[i].name, lang))
			return langs[i].comp[0] ? langs[i].comp : NULL;
	return NULL;
}

/* return source file name for the given language */
static char *lang_file(char *lang)
{
	int i;
	for (i = 0; i < LEN(langs); i++)
		if (!strcmp(langs[i].name, lang))
			return langs[i].file;
	return NULL;
}

/* return compiler output file name for the given language */
static char *lang_exec(char *lang)
{
	int i;
	for (i = 0; i < LEN(langs); i++)
		if (!strcmp(langs[i].name, lang))
			return langs[i].exec;
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

/* execute epath, with ipath as stdin and opath as stdout; return zero on success */
static int ct_exec(char **argv, char *tdir, char *ipath, char *opath, char *epath)
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
			return 'T';
		}
		if (WIFSIGNALED(st))
			return 'R';
		if (WEXITSTATUS(st))
			return 'R';
		return 0;
	}
	return 'R';
}

static int compilefile(char *src, char *lang, char *out)
{
	char **cc = lang ? lang_comp(lang) : NULL;
	char *args[16];
	int i;
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
			for (i = 0; i + 1 < LEN(args) && cc[i]; i++) {
				args[i] = cc[i];
				if (!strcmp("SRC", cc[i]))
					args[i] = src;
				if (!strcmp("OUT", cc[i]))
					args[i] = out;
			}
			args[i] = NULL;
			execvp(args[0], args);
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
	char *cont, *prog, *lang;
	char idat[LLEN], odat[LLEN];	/* input and output files */
	char vdat[LLEN];		/* verifier program */
	char tdir[LLEN];		/* testing directory */
	char tdir_i[LLEN], tdir_o[LLEN];/* input and output files in tdir */
	char tdir_s[LLEN];		/* source file in tdir */
	char tdir_x[LLEN];		/* compiled source in tdir */
	char tdir_v[LLEN];		/* verifier program in tdir */
	char tdir_r[LLEN];		/* varifier output in tdir */
	int score = 0;			/* total score */
	char stat[128] = "";
	char *args[16];
	long beg_ms, end_ms, tot_ms = 0;
	int passed = 1;
	int cmt = 0;
	int i;
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
	snprintf(tdir_s, sizeof(tdir_s), "%s/%s", tdir, lang_file(lang));
	snprintf(tdir_x, sizeof(tdir_x), "%s/%s", tdir, lang_exec(lang));
	snprintf(tdir_v, sizeof(tdir_v), "%s/.v", tdir);
	snprintf(tdir_r, sizeof(tdir_r), "%s/.r", tdir);
	mkdir(tdir, 0700);
	chown(tdir, TESTUID, TESTGID);
	util_install(prog, tdir_s, TESTUID, TESTGID, 0600);
	if (compilefile(tdir_s, lang, tdir_x))
		cmt = 'E';
	unlink(tdir_s);
	if (lang_intr(lang)) {
		char **intr = lang_intr(lang);
		for (i = 0; i < LEN(args) && intr[i]; i++)
			args[i] = !strcmp("SRC", intr[i]) ? tdir_x : intr[i];
	} else {
		args[0] = tdir_x;
		args[1] = NULL;
	}
	chown(tdir_x, TESTUID, TESTGID);
	chmod(tdir_x, 0700);
	for (i = 0; i < 100; i++) {
		snprintf(idat, sizeof(idat), "%s/%02d", cont, i);
		snprintf(odat, sizeof(odat), "%s/%02do", cont, i);
		snprintf(vdat, sizeof(vdat), "%s/%02dv", cont, i);
		if (!util_isfile(idat) || (!util_isfile(odat) &&
						!util_isfile(vdat)))
			break;
		util_install(idat, tdir_i, TESTUID, TESTGID, 0600);
		beg_ms = util_ts();
		if (cmt != 'E')
			cmt = ct_exec(args, tdir, ".i", ".o", "/dev/null");
		end_ms = util_ts();
		tot_ms += end_ms - beg_ms;
		if (!cmt && util_isfile(odat)) {	/* expected file */
			cmt = 'F';
			if (util_isfile(tdir_o) && !util_cmp(odat, tdir_o))
				cmt = 'P';
			score += cmt == 'P';
		}
		if (!cmt && !util_isfile(odat)) {	/* verifier program */
			char *args_check[] = {"./.v", NULL};
			FILE *filp;
			util_install(idat, tdir_i, TESTUID, TESTGID, 0600);
			util_install(vdat, tdir_v, TESTUID, TESTGID, 0700);
			cmt = 'P';
			if (ct_exec(args_check, tdir, ".o", ".r", "/dev/null"))
				cmt = 'F';
			filp = fopen(tdir_r, "r");
			if (filp) {
				int score_cur;
				if (fscanf(filp, "%d", &score_cur) == 1)
					score += score_cur;
				fclose(filp);
			}
			unlink(tdir_v);
			unlink(tdir_r);
		}
		stat[i] = cmt;
		unlink(tdir_i);
		unlink(tdir_o);
	}
	for (i = 0; stat[i] && passed; i++)
		passed = stat[i] == 'P';
	unlink(tdir_x);
	rmdir(tdir);			/* fails if tdir is not empty */
	printf("%d/%d\t%ld.%02ld\t# %s%c\n",
		score, (int) strlen(stat),
		tot_ms / 1000, (tot_ms % 1000) / 10,
		stat, passed ? '.' : '!');
	return 0;
}
