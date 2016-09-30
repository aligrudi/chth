/* Challenging Thursdays Server */
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "conn.h"

#define CTPORT		"40"		/* server port */
#define CTCONNS		16		/* maximum simultaneous connections */
#define LLEN		256		/* maximum input line length */
#define CTSUBS		32		/* maximum queued submissions */
#define CTSUBSZ		(1 << 16)	/* maximum submission size */
#define CTTIMEOUT	10		/* connection timeout in seconds */
#define CTUSERS		"USERS"		/* file containing the list of users */
#define CTLOGS		"logs"		/* directory to store the logs */
#define CTTEST		"./test"	/* verification program */
#define CTRESULT	"logs/test.out"	/* verification results file */
#define CTEOF		"EOF\n"		/* default eof mark */

#define LEN(a)		((sizeof(a)) / sizeof((a)[0]))

/* conn struct extensions */
static void conn_printf(struct conn *conn, char *fmt, ...);
static int conn_eol(struct conn *conn);
static int conn_recveol(struct conn *conn, char *buf, int len);
static int conn_ends(struct conn *conn, char *s);

/* pending submission */
struct sub {
	char user[LLEN];		/* submitting user */
	char cont[LLEN];		/* contest name */
	char lang[LLEN];		/* submission language */
	char path[LLEN];		/* program path */
	time_t date;			/* submission date */
	int valid;			/* pending submission */
};

static char **conts;			/* open contests */
static int conts_n;			/* number of open contests */
static struct sub subs[CTSUBS];		/* pending submissions */
static int test_pid;			/* pid of verifying program */
static int test_idx;			/* index of the program being tested */

/* return a server socket listening on the given port */
static int mksocket(char *addr, char *port)
{
	struct addrinfo hints, *addrinfo;
	int fd;
	int yes = 1;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = addr ? 0 : AI_PASSIVE;
	if (getaddrinfo(addr, port, &hints, &addrinfo))
		return -1;
	fd = socket(addrinfo->ai_family, addrinfo->ai_socktype,
			addrinfo->ai_protocol);
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	if (bind(fd, addrinfo->ai_addr, addrinfo->ai_addrlen) < 0)
		return -1;
	if (listen(fd, CTCONNS * 2))
		return -1;
	freeaddrinfo(addrinfo);
	return fd;
}

/* log in a user; return nonzero on failure */
static int users_login(char *quser, char *qpass)
{
	char line[LLEN], user[LLEN], pass[LLEN];
	FILE *fp = fopen(CTUSERS, "r");
	int logged = 0;
	if (!fp)
		return 1;
	while (fgets(line, sizeof(line), fp)) {
		if (sscanf(line, "%s %s", user, pass) != 2)
			continue;
		if (!strcmp(quser, user))
			logged = qpass == NULL || !strcmp(qpass, pass);
	}
	fclose(fp);
	return !logged;
}

/* add the given user */
static void users_add(char *user, char *pass)
{
	FILE *fp = fopen(CTUSERS, "a");
	fprintf(fp, "%s %s\n", user, pass);
	fclose(fp);
}

/* find the submission from the given user and for the given contest */
static int subs_find(char *user, char *cont)
{
	int i;
	for (i = 0; i < LEN(subs); i++)
		if (subs[i].valid && !strcmp(user, subs[i].user) &&
				!strcmp(cont, subs[i].cont))
			return i;
	return -1;
}

/* find the first unprocessed submission */
static int subs_first(void)
{
	int i;
	for (i = 0; i < LEN(subs); i++)
		if (subs[i].valid)
			return i;
	return -1;
}

/* queue the given submission */
static int subs_add(char *user, char *cont, char *lang, char *path)
{
	int i;
	for (i = 0; i < LEN(subs); i++) {
		if (!subs[i].valid) {
			subs[i].date = time(NULL);
			strcpy(subs[i].cont, cont);
			strcpy(subs[i].lang, lang);
			strcpy(subs[i].path, path);
			strcpy(subs[i].user, user);
			subs[i].valid = 1;
			return 0;
		}
	}
	return 1;
}

/* begin testing a submission */
static void test_beg(void)
{
	test_idx = subs_first();
	if (test_idx < 0) {
		test_pid = 0;
		return;
	}
	test_pid = fork();
	if (!test_pid) {
		char *argv[5] = {CTTEST, subs[test_idx].cont,
			subs[test_idx].path, subs[test_idx].lang};
		close(1);
		open(CTRESULT, O_WRONLY | O_TRUNC | O_CREAT, 0600);
		execvp(argv[0], argv);
		exit(1);
	}
}

/* the termination of the verification program */
static void sigchild(int sig)
{
	char line[1 << 10];
	char path[LLEN];
	FILE *resfp, *statfp;
	signal(SIGCHLD, sigchild);
	if (waitpid(test_pid, NULL, WNOHANG) == test_pid) {
		resfp = fopen(CTRESULT, "r");
		if (resfp && fgets(line, sizeof(line), resfp)) {
			snprintf(path, sizeof(path), "%s.stat",
					subs[test_idx].cont);
			statfp = fopen(path, "a");
			if (statfp) {
				fprintf(statfp, "%s\t%ld\t%s",
					subs[test_idx].user,
					subs[test_idx].date,
					line);
				fclose(statfp);
			}
		}
		if (resfp)
			fclose(resfp);
		subs[test_idx].valid = 0;
		if (subs_first() >= 0)
			test_beg();
		else
			test_pid = 0;
	}
}

static int ct_register(struct conn *conn, char *req)
{
	char user[LLEN], pass[LLEN];
	int i;
	if (sscanf(req, "register %s %s", user, pass) != 2) {
		conn_printf(conn, "register: insufficient arguments!\n");
		return 1;
	}
	if (strlen(user) < 4) {
		conn_printf(conn, "register: username is too short!\n");
		return 1;
	}
	if (strlen(user) > 16) {
		conn_printf(conn, "register: username too long!\n");
		return 1;
	}
	for (i = 0; user[i]; i++) {
		int c = (unsigned char) user[i];
		if (!isalnum(c) && c != '_' && c != '.') {
			conn_printf(conn, "register: username can contain only [a-zA-Z0-9_.]!\n");
			return 1;
		}
	}
	if (!users_login(user, NULL)) {
		conn_printf(conn, "register: user exists!\n");
		return 1;
	}
	users_add(user, pass);
	conn_printf(conn, "register: user %s added.\n", user);
	return 0;
}

static int ct_report(struct conn *conn, char *req)
{
	char buf[1 << 12];
	char cont[LLEN];
	char path[LLEN];
	int statfd, nr, i;
	if (sscanf(req, "report %s", cont) != 1) {
		conn_printf(conn, "report: insufficient arguments!\n");
		return 1;
	}
	snprintf(path, sizeof(path), "%s.stat", cont);
	statfd = open(path, O_RDONLY);
	if (statfd >= 0) {
		while ((nr = read(statfd, buf, sizeof(buf))) > 0)
			if (conn_send(conn, buf, nr))
				return 1;
		close(statfd);
	}
	for (i = 0; i < LEN(subs); i++)
		if (subs[i].valid && !strcmp(cont, subs[i].cont))
			conn_printf(conn, "%s\t%ld\t-\t-\t# Waiting\n",
				subs[i].user, subs[i].date);
	return 0;
}

static int isdir(char *path)
{
	struct stat st;
	if (stat(path, &st) < 0)
		return 0;
	return S_ISDIR(st.st_mode);
}

static int conts_find(char *cont)
{
	int i;
	for (i = 0; i < conts_n; i++)
		if (!strcmp(cont, conts[i]))
			return i;
	return -1;
}

/* find submit end marker */
static void endmarker(char *req, char *end)
{
	char *s;
	int i;
	strcpy(end, CTEOF);
	s = req;
	for (i = 0; i < 5; i++) {
		while (*s && !isspace((unsigned char) *s))
			s++;
		while (*s && isspace((unsigned char) *s))
			s++;
	}
	if (*s)
		strcpy(end, s);
}

static char *ct_langs[] = {
	"c", "c++", "py", "py2", "py3", "sh", "elf",
};

static int langok(char *lang)
{
	int i;
	for (i = 0; i < LEN(ct_langs); i++)
		if (!strcmp(lang, ct_langs[i]))
			return 1;
	return 0;
}

static int ct_submit(struct conn *conn, char *req)
{
	char user[LLEN], pass[LLEN], cont[LLEN], lang[LLEN];
	char path[LLEN], end[LLEN];
	void *buf;
	long buflen;
	int fd;
	endmarker(req, end);
	if (conn_ends(conn, end))
		end[0] = '\0';
	if (sscanf(req, "submit %s %s %s %s", user, pass, cont, lang) != 4) {
		conn_printf(conn, "submit: insufficient arguments!\n");
		return 1;
	}
	if (conts_find(cont) < 0) {
		conn_printf(conn, "submit: contest is not open!\n");
		return 1;
	}
	if (!langok(lang)) {
		conn_printf(conn, "submit: unknown language!\n");
		return 1;
	}
	if (users_login(user, pass)) {
		conn_printf(conn, "submit: failed to log in!\n");
		return 1;
	}
	if (subs_find(user, cont) >= 0) {
		conn_printf(conn, "submit: pending submission, wait!\n");
		return 1;
	}
	if (!isdir(CTLOGS))
		mkdir(CTLOGS, 0700);
	snprintf(path, sizeof(path), "%s/%s-%s.%s", CTLOGS, cont, user, lang);
	fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0600);
	if (fd < 0) {
		conn_printf(conn, "submit: cannot write!\n");
		return 1;
	}
	conn_recvall(conn, &buf, &buflen);
	buflen -= strlen(end);
	write(fd, buf, buflen);
	free(buf);
	close(fd);
	if (!subs_add(user, cont, lang, path))
		conn_printf(conn, "submit: submission queued.\n");
	else
		conn_printf(conn, "submit: too many submissions, retry later!\n");
	if (!test_pid && subs_first() >= 0)
		test_beg();
	return 0;
}

static int ct_log(struct conn *conn, char *req)
{
	fputs(req, stderr);
	return 0;
}

static struct conn *conns[CTCONNS];	/* server connections */
static int conns_lim[CTCONNS];		/* read until 1:EOL or 2:EOF */
static int conns_ts[CTCONNS];		/* start timestamp (in seconds) */
static char conns_req[CTCONNS][LLEN];	/* connection request line */

static int ct_poll(int fd)
{
	struct pollfd fds[CTCONNS + 1];
	char end[LLEN], cmd[LLEN];
	int cfd;
	int i;
	for (i = 0; i < CTCONNS; i++)		/* kill slow connections */
		if (conns[i] && conns_ts[i] + CTTIMEOUT < time(NULL))
			conn_hang(conns[i]);
	for (i = 0; i < CTCONNS; i++) {		/* initialize fds[] */
		if (!conns[i] || conn_hung(conns[i])) {
			fds[i].fd = -1;
			fds[i].events = 0;
		} else {
			fds[i].fd = conn_fd(conns[i]);
			fds[i].events = conn_events(conns[i]);
		}
	}
	fds[CTCONNS].fd = fd;
	fds[CTCONNS].events = POLLRDNORM | POLLHUP | POLLERR | POLLNVAL;
	if (poll(fds, CTCONNS + 1, 1000) < 0)
		return 0;
	for (i = 0; i < CTCONNS; i++) {		/* check connection events */
		if (fds[i].revents) {
			if (conn_poll(conns[i], fds[i].revents))
				conn_hang(conns[i]);
			if (conns_lim[i] == 1 && conn_eol(conns[i]) >= 0) {
				conn_recveol(conns[i], conns_req[i], sizeof(conns_req[i]));
				ct_log(conns[i], conns_req[i]);
				sscanf(conns_req[i], "%s", cmd);
				conns_lim[i] = 0;
				if (!strcmp("register", cmd)) {
					ct_register(conns[i], conns_req[i]);
				} else if (!strcmp("report", cmd)) {
					ct_report(conns[i], conns_req[i]);
				} else if (!strcmp("submit", cmd)) {
					conns_lim[i] = 2;
				} else {
					conn_hang(conns[i]);
				}
			}
			if (conns_lim[i] == 2) {
				endmarker(conns_req[i], end);
				if (conn_hung(conns[i]) || !conn_ends(conns[i], end)) {
					ct_submit(conns[i], conns_req[i]);
					conns_lim[i] = 0;
				}
				if (conn_len(conns[i]) > CTSUBSZ)
					conn_hang(conns[i]);
			}
			if (conns_lim[i] == 0 && !(conn_events(conns[i]) & POLLWRNORM))
				conn_hang(conns[i]);
		}
	}
	for (i = 0; i < CTCONNS; i++) {		/* remove dead connections */
		if (conns[i] && conn_hung(conns[i])) {
			conn_free(conns[i]);
			conns[i] = NULL;
		}
	}
	if (fds[CTCONNS].revents & POLLRDNORM) {
		for (i = 0; i < CTCONNS; i++)
			if (!conns[i])
				break;
		if ((cfd = accept(fd, NULL, NULL)) >= 0) {
			fcntl(cfd, F_SETFD, fcntl(cfd, F_GETFD) | FD_CLOEXEC);
			fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL) | O_NONBLOCK);
			if (i < CTCONNS) {
				conns[i] = conn_make(cfd);
				conns_lim[i] = 1;
				conns_ts[i] = time(NULL);
			} else {
				close(cfd);
			}
		}
	}
	if (fds[CTCONNS].revents & (POLLHUP | POLLERR | POLLNVAL))
		return 1;
	return 0;
}

int main(int argc, char *argv[])
{
	int ifd;
	conts = argv + 1;
	conts_n = argc - 1;
	ifd = mksocket(NULL, CTPORT);
	signal(SIGCHLD, sigchild);
	while (!ct_poll(ifd))
		;
	close(ifd);
	return 0;
}

/* conn struct extensions */
static void conn_printf(struct conn *conn, char *fmt, ...)
{
	va_list ap;
	char buf[LLEN * 2];
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	conn_send(conn, buf, strlen(buf));
}

static int conn_eol(struct conn *conn)
{
	void *r, *eol;
	long rlen;
	if (conn_recvbuf(conn, &r, &rlen))
		return -1;
	eol = memchr(r, '\n', rlen);
	return eol ? eol - r + 1 : -1;
}

static int conn_recveol(struct conn *conn, char *buf, int len)
{
	int eol = conn_eol(conn);
	if (eol < 0 || eol + 1 > len)
		return 1;
	conn_recv(conn, buf, eol);
	buf[eol] = '\0';
	return 0;
}

/* check if the connection ends with the given string */
static int conn_ends(struct conn *conn, char *s)
{
	void *r;
	long slen = strlen(s);
	long rlen;
	if (conn_recvbuf(conn, &r, &rlen))
		return 1;
	if (rlen < slen)
		return 1;
	return memcmp(s, r + rlen - slen, slen) != 0;
}
