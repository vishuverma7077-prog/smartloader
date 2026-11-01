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
// Check the ELF headers and load segments before execution.
static int prepare_elf_image(void){
    struct stat file_status;
    uint64_t program_header_table_end;
    size_t page_capacity = 0;
    int has_entry_segment = 0;
    int i;

    if(fstat(fd, &file_status)<0)return -1;
    if(file_status.st_size < 0) {
        errno = EINVAL;
        return -1;
    }
    executable_file_size = (uint64_t)file_status.st_size;
    if (executable_file_size < sizeof(*ehdr)){
        errno = ENOEXEC;
        return -1;
    }
    if (read_exact_at(fd, ehdr, sizeof(*ehdr), 0) < 0)return -1;

    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS32 ||
        ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr->e_ident[EI_VERSION] != EV_CURRENT || ehdr->e_version != EV_CURRENT ||
        ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_386 ||
        ehdr->e_ehsize != sizeof(*ehdr) || ehdr->e_phnum == 0 ||
        ehdr->e_phentsize < sizeof(Elf32_Phdr)) {
        errno = ENOEXEC;
        return -1;
    }
    program_header_table_end = (uint64_t)ehdr->e_phoff +(uint64_t)ehdr->e_phnum * (uint64_t)ehdr->e_phentsize;
    if (program_header_table_end > executable_file_size){
        errno = ENOEXEC;return -1;
    }
    phdr = calloc(ehdr->e_phnum, sizeof(*phdr));
    if (phdr == NULL)return -1;
    for (i = 0; i < (int)ehdr->e_phnum; ++i){
        uint64_t header_offset = (uint64_t)ehdr->e_phoff + (uint64_t)i * (uint64_t)ehdr->e_phentsize;
        if (read_exact_at(fd, &phdr[i], sizeof(phdr[i]), header_offset) < 0)return -1;
        if (phdr[i].p_type == PT_LOAD) {
            uint64_t segment_start = (uint64_t)phdr[i].p_vaddr;
            uint64_t segment_end =segment_start + (uint64_t)phdr[i].p_memsz;
            uint64_t first_page;
            uint64_t rounded_end;
            uint64_t segment_pages;

            if (phdr[i].p_filesz > phdr[i].p_memsz ||(uint64_t)phdr[i].p_offset + phdr[i].p_filesz >executable_file_size ||
            segment_end > UINT64_C(0x100000000)){
                errno = ENOEXEC;
                return -1;
            }
            if((uint64_t)ehdr->e_entry >= segment_start && (uint64_t)ehdr->e_entry < segment_end)has_entry_segment = 1;
            if(phdr[i].p_memsz == 0)continue;
            first_page = segment_start &
                ~((uint64_t)SMART_PAGE_SIZE - 1U);
            rounded_end = (segment_end + SMART_PAGE_SIZE - 1U) &
                ~((uint64_t)SMART_PAGE_SIZE - 1U);
            segment_pages = (rounded_end - first_page) / SMART_PAGE_SIZE;
            if(segment_pages > SIZE_MAX - page_capacity){
                errno=EOVERFLOW;return -1;
            }page_capacity += (size_t)segment_pages;
        }
    }
    if(!has_entry_segment || page_capacity == 0 ||page_capacity > (size_t)SIG_ATOMIC_MAX){
        errno = ENOEXEC;
        return -1;
    }
    if(page_capacity > SIZE_MAX / sizeof(*mapped_pages)){
        errno = EOVERFLOW;return -1;
    }
    mapped_pages = malloc(page_capacity * sizeof(*mapped_pages));
    if(mapped_pages == NULL)return -1;
    mapped_page_capacity = page_capacity;
    mapped_page_count = 0;
    handled_page_faults = 0;
    page_allocations = 0;
    return 0;
}
