#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <list.h>

typedef int tid_t;
typedef int pid_t;

/* File details. */
struct file_details 
  {
    int fd;                         /* fd. */
    struct file *file_ptr ;         /*file pointer. */
    struct list_elem elem;          /* List element. */
    int tid;                        /* Thread identifier. */
  };

void syscall_init (void);
void syscall_exit (int);

#endif /* userprog/syscall.h */