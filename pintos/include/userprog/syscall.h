#include "filesys/file.h"

#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

off_t
file_read_at_lock (struct file *file, void *buffer, off_t size, off_t ofs);

off_t
file_write_at_lock (struct file *file, const void *buffer, off_t size,off_t ofs);

#endif /* userprog/syscall.h */