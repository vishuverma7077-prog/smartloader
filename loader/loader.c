#define _GNU_SOURCE
#include "loader.h"
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdint.h>
#define SMART_PAGE_SIZE 4096U

Elf32_Ehdr *ehdr = NULL;
Elf32_Phdr *phdr = NULL;
int fd = -1;

static uintptr_t *mapped_pages = NULL;
static size_t mapped_page_capacity = 0;
static volatile sig_atomic_t mapped_page_count = 0;
static volatile sig_atomic_t handled_page_faults = 0;
static volatile sig_atomic_t page_allocations = 0;
static struct sigaction previous_segv_action;
static int previous_action_saved = 0;
static uint64_t executable_file_size = 0;
