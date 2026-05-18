#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

#endif /* userprog/syscall.h */
file_read_at_lock (struct file *file, void *buffer, off_t size, off_t ofs);
file_write_at_lock (struct file *file, const void *buffer, off_t size,off_t ofs);