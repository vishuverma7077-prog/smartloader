/*
 * Public interface and common ELF loader includes for SimpleSmartLoader.
 */
#ifndef SIMPLE_SMART_LOADER_H
#define SIMPLE_SMART_LOADER_H

#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

void load_and_run_elf(char *exe);
void loader_cleanup(void);

#endif
