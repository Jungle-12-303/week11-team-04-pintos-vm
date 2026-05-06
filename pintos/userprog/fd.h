#ifndef USERPROG_FD_H
#define USERPROG_FD_H

#include <stddef.h>

#define FD_ENTRY_N (1 << 8)

struct file;

enum fd_type {
    FD_STDIN,
    FD_STDOUT,
    FD_FILE,
};

struct fd_entry {
    enum fd_type type;
    struct file* file;
};

struct fd_table {
    struct fd_entry **fds;
    size_t size;
};

struct fd_table *fd_table_init (void);
int fd_find_blank (struct fd_table *fdt);
void fd_entry_free (struct fd_table *fdt, size_t index);
void fd_table_free (struct fd_table *fdt);

#endif /* USERPROG_FD_H */
