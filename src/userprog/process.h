#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

/* Child details. */
struct child_details 
{
  tid_t tid;                      /* Thread identifier. */
  int exit_status;                /* Thread exit status. */
  bool is_parent_waiting;         /* Flag to denote if parent is waiting on child. */
  bool has_exit;                  /* Flag to denote if thread has exit. */
  struct list_elem elem;          /* List element. */
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
struct child_details * process_get_child_by_tid(struct thread *th, tid_t child_tid);

#endif /* userprog/process.h */
