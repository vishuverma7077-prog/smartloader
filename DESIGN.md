# Assignment 4 — SimpleSmartLoader (Group 41)

## Implementation overview

SimpleSmartLoader runs 32-bit little-endian ET_EXEC ELF files for Linux x86
that do not depend on glibc. It keeps the Assignment-1 shared-library design:
lib_simpleloader.so exports load_and_run_elf(char *exe) and
loader_cleanup(), and bin/launch calls those functions.

The loader reads and validates the ELF header and program headers, but it does
not map any PT_LOAD segment before transferring control. It casts the ELF
entrypoint (e_entry) directly to a function pointer and calls it. The first
instruction faults because its page is not present. The SA_SIGINFO handler
recognizes valid addresses in PT_LOAD memory ranges, maps one 4096-byte page
at the faulting page address with MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
copies the file-backed bytes intersecting that page, and returns. The CPU then
retries the instruction.

Pages are mapped read/write/execute for this assignment's no-glibc tests, so
both code execution and writable .data/.bss work. Anonymous mappings start
zero-filled; therefore bytes from p_filesz through p_memsz remain zero.

## Data structures and global state

- ehdr holds the validated Elf32_Ehdr.
- phdr holds the decoded Elf32_Phdr array. The file descriptor remains open
  while the program runs because the fault handler reads file bytes on demand
  with pread.
- mapped_pages stores the address of each page successfully mapped by the
  handler. Its capacity is computed from all loadable segment page ranges
  before execution; this is bookkeeping only and does not allocate segment
  memory.
- mapped_page_count, handled_page_faults, and page_allocations are
  volatile sig_atomic_t counters shared with the signal handler.
- previous_segv_action saves the process's old SIGSEGV action so it can be
  restored after _start returns and during cleanup.

## Signal handler flow

1. Read the failing address from siginfo_t.si_addr.
2. Check whether it lies in the p_vaddr to p_vaddr + p_memsz range of any
   PT_LOAD program header. If not, restore SIG_DFL and re-raise SIGSEGV.
3. Align the address down to its 4096-byte page boundary. A duplicate fault
   for an already mapped page is treated as a genuine fault.
4. Map exactly one page at that address with mmap and MAP_FIXED.
5. For every PT_LOAD header, intersect that page with the segment's
   file-backed virtual range. Read and copy only the intersecting bytes from
   p_offset + (copy_address - p_vaddr). This also handles a non-page-aligned
   first segment page and ELF segments that share a page.
6. Leave all other bytes untouched. Anonymous mmap zeroes BSS, partial-page
   padding, and the rest of the new page.
7. Record the page and increment both the handled-fault and allocation
   counters. Returning from the handler lets the processor retry the original
   access.

A fault-handler mapping or file-read failure is not silently ignored; the page
is discarded if needed, and the fault is re-raised with the default action.

## Page fault handling algorithm

For each fault address a, the loader first checks:

    p_vaddr <= a < p_vaddr + p_memsz

for at least one PT_LOAD segment. The mapped address is:

    page = a & ~(4096 - 1)

The loader requests one 4096-byte mapping at page. For each loadable segment,
it computes the intersection of [page, page + 4096) and
[p_vaddr, p_vaddr + p_filesz). If the intersection is nonempty, just those
bytes are read from the ELF file and copied into the corresponding page
offset. Since the mapping is anonymous, the memory range
[p_vaddr + p_filesz, p_vaddr + p_memsz) remains zero without a separate
clear operation.

## Statistics

- Page faults: number of valid lazy faults handled.
- Page allocations: number of successful one-page mappings (one per
  handled fault).
- Internal fragmentation: for each segment, the implementation counts
  allocated pages intersecting that segment and applies
  allocated_pages_for_segment * 4096 - p_memsz when this value is
  nonnegative. A lazily touched segment may not yet have all of its pages
  allocated; such an incomplete segment contributes zero instead of a
  negative fragmentation value. The total is printed in KB with two decimal
  places.

## Build and run

Build on Linux x86 with 32-bit development libraries and GNU make:

    make clean
    make
    ./bin/launch test/fib
    ./bin/launch test/sum

The test programs are compiled using -m32 -no-pie -nostdlib; the loader and
launcher are 32-bit, and the loader is built as lib_simpleloader.so.

## Execution attempt on the current host

Attempted on 2026-11-01 using the project Makefile:

- Host: macOS Darwin arm64.
- make clean: completed successfully.
- make: stopped while compiling the loader because this host's Apple SDK does
  not provide the Linux header elf.h (reported from loader/loader.h:7).
- fib and sum: not run; the shared loader did not build.

This is an environment limitation, not a Linux test result. Run the build and
both executables on Linux x86 with 32-bit development support before filling
in the results below.

## Test results template

Run each command on the required Linux x86 environment and record the observed
statistics during the demo/submission.

| Test | Expected _start return value | Page faults | Page allocations | Internal fragmentation (KB) |
| --- | ---: | ---: | ---: | ---: |
| fib | 102334155 | TBD | TBD | TBD |
| sum | 2048 | TBD | TBD | TBD |

- **Member 1 (Vaibhav[2025527]): Demand Paging, Signal Handling & Memory Mapping Engine**
  - Designed and implemented the custom `SIGSEGV` signal handler using `sigaction` and `SA_SIGINFO`.
  - Implemented page-fault address translation, 4KB page alignment arithmetic (`fault_address & ~(4096-1)`), and dynamic one-page allocation using `mmap()` with `MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS`.
  - Implemented `copy_file_bytes_for_page()` and atomic `read_exact_at()` (`pread`) to safely copy file-backed segments across boundary pages into virtual memory.
  - Built boundary and BSS safety logic (ensuring unmapped BSS/padding bytes remain zero-initialized) and genuine segmentation fault pass-through via default signal re-raising (`SIG_DFL`).

- **Member 2 (Vaibhav[2025523]): ELF Parsing, State Lifecycle, Metrics & Testing**
  - Implemented `prepare_elf_image()` to validate 32-bit x86 ELF binaries (checking ELF magic, 32-bit class, `EM_386`, `ET_EXEC`), read program headers (`phdr`), and compute dynamic page tracking capacity.
  - Implemented the execution driver `load_and_run_elf()`, casting `ehdr->e_entry` directly to a function pointer without upfront mapping to intentionally trigger lazy loading.
  - Designed the internal fragmentation calculation algorithm (`calculate_internal_fragmentation()`) and managed runtime counters for page faults and page allocations.
  - Built `loader_cleanup()` to unmap all tracked pages via `munmap()`, free dynamic buffers, and restore previous signal handlers. Configured build automation across all subdirectories and conducted test verification on Linux/WSL for `fib` and `sum`.
## Private GitHub repository

Private repository link: https://github.com/vishuverma7077-prog/smartloader
