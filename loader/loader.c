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
//Copy overlapping PT_LOAD data into the page; untouched bytes remain zero-filled.
static int copy_file_bytes_for_page(uintptr_t page_address, void *page_memory){
    uint64_t page_start = (uint64_t)page_address;
    uint64_t page_end = page_start + SMART_PAGE_SIZE;
    int i;
    for (i = 0;i<(int)ehdr->e_phnum; ++i) {
        uint64_t segment_start;
        uint64_t file_virtual_end;
        uint64_t copy_start;
        uint64_t copy_end;
        uint64_t source_offset;
        size_t copy_length;
        char *destination;
        if(phdr[i].p_type!=PT_LOAD || phdr[i].p_filesz== 0)continue;
        segment_start= (uint64_t)phdr[i].p_vaddr;
        file_virtual_end= segment_start + (uint64_t)phdr[i].p_filesz;
        copy_start =page_start >segment_start ? page_start:segment_start;
        copy_end =page_end <file_virtual_end ? page_end:file_virtual_end;
        if(copy_start>= copy_end)continue;
        source_offset =(uint64_t)phdr[i].p_offset +(copy_start - segment_start);
        copy_length= (size_t)(copy_end - copy_start);
        destination= (char *)page_memory + (size_t)(copy_start - page_start);
        if (read_exact_at(fd, destination, copy_length, source_offset)<0)return -1;
    }return 0;
}
// Map the page containing the fault address so the CPU can retry the instruction.
static void lazy_page_fault_handler(int signal_number, siginfo_t *information,void *machine_context){
    uintptr_t fault_address;
    uintptr_t page_address;
    void *mapped_page;
    (void)signal_number;
    (void)machine_context;
    if (information == NULL || ehdr == NULL || phdr == NULL || fd < 0)re_raise_default_segv();
    fault_address = (uintptr_t)information->si_addr;
    if (!address_belongs_to_load_segment(fault_address))re_raise_default_segv();
    page_address = fault_address & ~((uintptr_t)SMART_PAGE_SIZE - 1U);
    if (page_is_already_mapped(page_address) ||
        mapped_page_count < 0 ||
        (size_t)mapped_page_count >= mapped_page_capacity ||
        page_allocations >= SIG_ATOMIC_MAX ||
        handled_page_faults >= SIG_ATOMIC_MAX) {
        re_raise_default_segv();
    }
