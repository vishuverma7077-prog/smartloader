#include "loader.h"

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <ELF32 executable>\n", argv[0]);
        return EXIT_FAILURE;
    }

    load_and_run_elf(argv[1]);
    loader_cleanup();
    return EXIT_SUCCESS;
}
