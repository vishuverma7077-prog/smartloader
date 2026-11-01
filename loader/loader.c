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
//Read the requested bytes at the given offset without changing the file position.
static int read_exact_at(int file_descriptor, void *buffer, size_t length,uint64_t file_offset){
    size_t bytes_read = 0;
    while (bytes_read < length){
        ssize_t result = pread(file_descriptor,(char *)buffer + bytes_read,length - bytes_read,(off_t)(file_offset + bytes_read));
        if(result<0){
            if(errno==EINTR)continue;
            return -1;
        }
        if(result==0){
            errno = EIO;
            return -1;
        }
        bytes_read += (size_t)result;
    }return 0;
}
// Reinstall the saved SIGSEGV action before freeing loader state.
static int restore_previous_segv_action(void){
    if(!previous_action_saved)return 0;
    if(sigaction(SIGSEGV, &previous_segv_action, NULL)<0)return -1;
    previous_action_saved=0;return 0;
}
//Set SIGSEGV to its default action and re-raise the fault.
static void re_raise_default_segv(void){
    struct sigaction default_action;
    memset(&default_action, 0, sizeof(default_action));
    default_action.sa_handler = SIG_DFL;
    sigemptyset(&default_action.sa_mask);
    if(sigaction(SIGSEGV, &default_action, NULL)<0)_exit(128 + SIGSEGV);
    if(raise(SIGSEGV)!=0)_exit(128+SIGSEGV);
}
///Check whether the address falls within a PT_LOAD segment.
static int address_belongs_to_load_segment(uintptr_t address){
    int i;
    for(i = 0; i<(int)ehdr->e_phnum; ++i) {
        uint64_t start;
        uint64_t end;
        if (phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0)continue;
        start=(uint64_t)phdr[i].p_vaddr;
        end=start+(uint64_t)phdr[i].p_memsz;
        if((uint64_t)address >= start && (uint64_t)address <end)return 1;
    }return 0;
}
//Check whether this page is already mapped.
static int page_is_already_mapped(uintptr_t page_address){
    sig_atomic_t i;
    for (i = 0; i < mapped_page_count; ++i){if (mapped_pages[i] == page_address)return 1;
    }return 0;
}
