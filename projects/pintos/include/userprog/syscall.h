#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>

void syscall_init (void);

struct page *check_address (void *address);
void check_buffer (void *buffer, unsigned size, bool writable);

struct lock file_lock;

#endif /* userprog/syscall.h */
