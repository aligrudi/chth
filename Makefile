CC = cc
CFLAGS = -Wall -O2
LDFLAGS =

all: test serv
%.o: %.c
	$(CC) -c $(CFLAGS) $<
test: test.o
	$(CC) -o $@ $^ $(LDFLAGS)
serv: serv.o conn.o
	$(CC) -o $@ $^ $(LDFLAGS)
clean:
	rm -f *.o test serv
