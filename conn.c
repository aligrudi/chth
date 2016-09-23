/* Challenging Thursdays Connection Management */
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "conn.h"

#define MAX(a, b)	((a) < (b) ? (b) : (a))

struct conn {
	int fd;			/* socket file descriptor */
	void *ibuf;		/* input buffer */
	long ibuf_n;		/* received bytes in ibuf */
	long ibuf_sz;		/* size of ibuf[] */
	void *obuf;		/* output buffers */
	long obuf_n;		/* number of bytes in obuf */
	long obuf_sz;		/* size of obuf[] */
	int dosend, dorecv;	/* fd can be read from or written to */
};

struct conn *conn_make(int fd)
{
	struct conn *conn = malloc(sizeof(*conn));
	memset(conn, 0, sizeof(*conn));
	conn->fd = fd;
	conn->dosend = 1;
	conn->dorecv = 1;
	return conn;
}

void conn_hang(struct conn *conn)
{
	if (conn->fd >= 0)
		close(conn->fd);
	conn->fd = -1;
}

void conn_free(struct conn *conn)
{
	conn_hang(conn);
	free(conn->ibuf);
	free(conn->obuf);
	free(conn);
}

int conn_events(struct conn *conn)
{
	if (conn->fd >= 0)
		return POLLHUP | POLLERR | POLLNVAL |
			(conn->dorecv ? POLLRDNORM : 0) |
			(conn->dosend && conn->obuf_n ? POLLWRNORM : 0);
	return 0;
}

static int mextend(void **ptr, long *sz, long memsz)
{
	long newsz = MAX(128, *sz * 2);
	void *new = malloc(newsz * memsz);
	if (!new)
		return 1;
	memcpy(new, *ptr, *sz * memsz);
	memset(new + *sz * memsz, 0, (newsz - *sz) * memsz);
	free(*ptr);
	*ptr = new;
	*sz = newsz;
	return 0;
}

int conn_poll(struct conn *conn, int events)
{
	int nr, nw;
	if (events & POLLRDNORM) {
		if (conn->ibuf_n == conn->ibuf_sz)
			if (mextend(&(conn->ibuf), &(conn->ibuf_sz), 1))
				return 1;
		nr = read(conn->fd, conn->ibuf + conn->ibuf_n,
				conn->ibuf_sz - conn->ibuf_n);
		if (nr > 0)
			conn->ibuf_n += nr;
		if (nr == 0)		/* socket is half duplex */
			conn->dorecv = 0;
	}
	if (events & POLLWRNORM) {
		nw = write(conn->fd, conn->obuf, conn->obuf_n);
		if (nw > 0) {
			if (nw < conn->obuf_n)
				memmove(conn->obuf, conn->obuf + nw,
					conn->obuf_n - nw);
			conn->obuf_n -= nw;
		}
		if (nw == 0)
			conn->dosend = 0;
	}
	if (events & (POLLHUP | POLLERR | POLLNVAL)) {
		conn_hang(conn);
		return 1;
	}
	return 0;
}

int conn_send(struct conn *conn, void *buf, long len)
{
	while (conn->obuf_n + len >= conn->obuf_sz)
		if (mextend(&(conn->obuf), &(conn->obuf_sz), 1))
			return 1;
	memcpy(conn->obuf + conn->obuf_n, buf, len);
	conn->obuf_n += len;
	return 0;
}

int conn_hung(struct conn *conn)
{
	return conn->fd < 0 || (!conn->dosend && !conn->dorecv);
}

long conn_len(struct conn *conn)
{
	return conn->ibuf_n;
}

int conn_recv(struct conn *conn, void *buf, long len)
{
	if (conn->ibuf_n < len)
		len = conn->ibuf_n;
	memcpy(buf, conn->ibuf, len);
	memmove(conn->ibuf, conn->ibuf + len, conn->ibuf_n - len);
	conn->ibuf_n -= len;
	return len;
}

int conn_recvall(struct conn *conn, void **buf, long *len)
{
	*buf = conn->ibuf;
	*len = conn->ibuf_n;
	conn->ibuf = NULL;
	conn->ibuf_n = 0;
	conn->ibuf_sz = 0;
	return 0;
}

int conn_recvbuf(struct conn *conn, void **buf, long *len)
{
	*buf = conn->ibuf;
	*len = conn->ibuf_n;
	return conn->ibuf == NULL;
}

int conn_fd(struct conn *conn)
{
	return conn->fd;
}
