#include "userprog/fd.h"
#include "threads/malloc.h"

static struct fd_entry **fd_table;

void
init_fd_table () {
    fd_table = (struct fd_entry **)calloc (FD_ENTRY_N, sizeof (struct fd_entry *));
    fd_table[0] = STD_IN;
}