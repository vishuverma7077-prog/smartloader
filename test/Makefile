CC = gcc
CFLAGS = -m32 -no-pie -nostdlib -Wall -Wextra

.PHONY: all clean

all: fib sum

fib: fib.c
	$(CC) $(CFLAGS) -o $@ $<

sum: sum.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f fib sum
