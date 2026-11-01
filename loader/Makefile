CC = gcc
CFLAGS = -m32 -fPIC -Wall -Wextra

.PHONY: all clean

all: ../bin/lib_simpleloader.so

../bin/lib_simpleloader.so: loader.c loader.h | ../bin
	$(CC) $(CFLAGS) -shared -o $@ loader.c

../bin:
	mkdir -p ../bin

clean:
	rm -f ../bin/lib_simpleloader.so
