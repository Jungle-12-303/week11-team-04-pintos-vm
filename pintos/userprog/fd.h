#ifndef USERPROG_FD_H
#define USERPROG_FD_H

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

#endif /* USERPROG_FD_H */
