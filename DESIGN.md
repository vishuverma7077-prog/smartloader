Assignment 4, SimpleSmartLoader (Group 41)

## Overview of Implementation

SimpleSmartLoader runs little-endian 32-bit ELF executables on Linux x86, and it loads them too. That design here's a shared library, same as Assignment-1. Two functions get exported by lib_simpleloader.so: load_and_run_elf(). And loader_cleanup(). bin/launch is what calls them.

This loader reads. And validates the ELF header and program headers, then hands off control to the ELF entry point without setting up any memory mappings first. It casts e_entry to a function pointer and calls it. And the first instruction faults because the page isn't present. That SIGSEGV triggers the SA_SIGINFO handler. If the faulting address falls inside a PT_LOAD segment, the handler maps a 4096 byte page, copies the relevant file portion into it, and returns the page. This causes the CPU to retry the faulting instruction.

For tests that don't use glibc, the mappings for read, write, and execute get set up, so code execution works and writable .data/.bss honestly works too. Since pages come zeroed when they're allocated, the rest of the fields in the program segment descriptors stay at zero.

## Data structures and global state

- ehdr: Validated Elf32_Ehdr.
- phdr: Decoded Elf32_Phdr array. The file descriptor stays open for the life of the program since the fault handler uses pread to read file bytes as needed.
- mapped_pages: Addresses for every page the handler mapped successfully. Capacity gets computed up front across all loadable segment page ranges. Bookkeeping only, no real segment memory allocated here.
- mapped_page_count, handled_page_faults and page_allocations: volatile sig_atomic_t counters, shared with the signal handler., previous_segv_action: keeps the process's previous SIGSEGV handler around, so it can be put back when returning from _start and when cleaning up.

The signal handler has a few steps inside it:

1. Get the address out of siginfo_t.si_addr.

2. Check whether that address falls within a PT_LOAD segment's memory range. If it doesn't,

Set SIGSEGV back to SIG_DFL, then raise it again.

3. Round that address down so it sits on its 4096 byte boundary. If a fault hits for an address that's already

A mapped page is a real fault, so it has to be handled.

4. Use mmap to map exactly one page at that address, with the flag

MAP_FIXED.

5. For every PT_LOAD segment, intersect that page with the segment's file

Virtual memory backs this. Copy just the bytes that overlap.

segment, offset, plus the copy address. If the segment isn't page aligned,

Byte offsets within the segment can extend past the segment page. That's also

Handled here.

6. The rest of the bytes stay zeroed out. Fills for the zero page are

BSS is mapped too, and along with some page alignment padding, it's left uninitialized.

The state.

7. Allocation is applied and the page and counters get updated as a result

Faults get processed as pages by the fault handler, and it lets the processor

try the original access again.

When a fault handler can't map a page or read a file, that page gets thrown out.

If we do nothing, the default signal action kicks in and end the process.

Page fault handling algorithm

For every fault address a, the loader checks this first:

p_vaddr <= a < p_vaddr + p_memsz

For at least one PT_LOAD segment, the mapped address comes out to:

page = a & ~(4096, 1)

The loader asks for a single 4096-byte mapping at page. Then it computes, for each loadable segment, where [page, page + 4096) overlaps [p_vaddr, p_vaddr + p_filesz). Bytes from the ELF file get read and copied into the matching page offset, but only when that overlap is nonempty. The mapping is anonymous, so [p_vaddr + p_filesz, p_vaddr + p_memsz) stays zero with no separate clear step needed.

## Statistics

- Page faults: how many valid lazy faults were handled.
- Page allocations: how many one-page mappings succeeded (one per handled fault).
- Internal fragmentation: for every segment, the implementation counts the allocated pages that intersect it. When this value is nonnegative, it applies allocated_pages_for_segment * 4096, p_memsz. If a segment was touched lazily, it may not have all pages allocated yet. That incomplete segment contributes zero rather than a negative fragmentation value. The total prints in KB with two decimal places.

## Build and run

To build on Linux x86, you'll need the 32-bit dev libraries plus GNU make:

make clean
make
./bin/launch test/fib
./bin/launch test/sum

Test programs get compiled with -m32 -no-pie -nostdlib. The loader and launcher run 32-bit. As for the loader, it's built as lib_simpleloader.so.

Test results, Linux/Codespaces run

Here's what was transcribed from the Linux/Codespaces run in the group screenshot, dated 2026-11-01.

| Test | Expected _start return value | Page faults | Page allocations | Internal fragmentation (KB) |
|, |, -: |, -: |, -: |, -: |
| fib | 102334155 | 1 | 1 | 3.89 |
| sum | 2048 | 3 | 3 | 7.88 |

- Member 1 (Vaibhav[2025527]): Demand Paging, Signal Handling & Memory Mapping Engine
, Built the custom `SIGSEGV` handler with `sigaction` and `SA_SIGINFO`.
, Handled page-fault address translation, 4KB page alignment math (`fault_address & ~(4096-1)`), and one-page allocation on the fly through `mmap()` with `MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS`.
, Wrote `copy_file_bytes_for_page()` plus atomic `read_exact_at()` (`pread`) so file-backed segments copy safely across boundary pages into virtual memory.
, Added boundary and BSS safety logic (unmapped BSS and padding bytes stay zero-initialized) and let real segmentation faults pass through by re-raising the default signal (`SIG_DFL`).

- Member 2 (Vaibhav[2025523]): ELF Parsing, State Lifecycle, Metrics & Testing
, Built `prepare_elf_image()` to validate 32-bit x86 ELF binaries: checks ELF magic, 32-bit class, `EM_386`, and `ET_EXEC`, reads program headers (`phdr`), then basically works out dynamic page tracking capacity.
, Wrote the execution driver `load_and_run_elf()`. It casts `ehdr->e_entry` straight to a function pointer with no upfront mapping, so lazy loading fires on purpose.
, Came up with the internal fragmentation algorithm (`calculate_internal_fragmentation()`) and kept the runtime counters for page faults and page allocations., Built `loader_cleanup()` to unmap every tracked page with `munmap()`, free dynamic buffers, and restore the previous signal handlers. Also set up build automation in all subdirectories, then verified tests for `fib` and `sum` on Linux/WSL.
