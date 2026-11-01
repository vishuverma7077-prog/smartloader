CC = gcc
CFLAGS = -m32 -Wall -Wextra

.PHONY: all clean

all: ../bin/launch

../bin/launch: launch.c ../loader/loader.h ../bin/lib_simpleloader.so
	$(CC) $(CFLAGS) -I../loader -L../bin \
		-Wl,-rpath,'$$ORIGIN' -o $@ launch.c -l_simpleloader

clean:
	rm -f ../bin/launch
