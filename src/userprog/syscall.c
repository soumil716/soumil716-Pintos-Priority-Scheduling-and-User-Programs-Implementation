#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <string.h>
#include "lib/kernel/stdio.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);
static int fd_count = 2;
struct list files_list;              /* List of opened files. */ 

void get_args(void *esp, const int total_args);
void is_buf_valid(void *buf, unsigned size);
void is_ptr_bad(int *ptr);

void halt (void);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
pid_t exec (const char *cmd_line);
int wait (pid_t pid);
struct file_details * get_file_from_list(int fd);
void close_all_fds(tid_t tid);


int *args[5];
static struct lock lock;

void
syscall_init (void) 
{
  list_init(&files_list);
  lock_init (&lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  if(f->esp == NULL || pagedir_get_page(thread_current()->pagedir, (const void *)f->esp) == NULL){
    syscall_exit(-1);
  }
  int sys_no = *(int *)f->esp;

  switch (sys_no)
  {
    case SYS_HALT:
    halt();
    break;

    case SYS_EXEC:
    get_args(f->esp, 1);
    is_ptr_bad((int*)*args[0]);
    f->eax = exec((const char *)*args[0]);
    break;

    case SYS_WAIT:
    get_args(f->esp, 1);
    f->eax = wait(*args[0]);
    break;

    case SYS_EXIT:
    get_args(f->esp, 1);
    syscall_exit(*args[0]);
    break;

    case SYS_CREATE:
    get_args(f->esp, 2);
    is_ptr_bad((int*)*args[0]);
    f->eax = create((const void*)(*args[0]), *(unsigned *)args[1]);
    break;

    case SYS_REMOVE:
    get_args(f->esp, 1);
    f->eax = remove((const void*)(*args[0]));
    break;

    case SYS_OPEN:
    get_args(f->esp, 1);
    is_ptr_bad((int*)*args[0]);
    f->eax = open((const void*)(*args[0]));
    break;

    case SYS_FILESIZE:
    get_args(f->esp, 1);
    f->eax = filesize(*args[0]);
    break;

    case SYS_READ:
    get_args(f->esp, 3);
    is_buf_valid((void *)(*args[1]), *(unsigned *)args[2]);
    f->eax = read(*args[0], (void*)(*args[1]),*(unsigned *)args[2]);
    break;

    case SYS_WRITE:
    get_args(f->esp, 3);
    is_buf_valid((void *)(*args[1]), *(unsigned*)args[2]);
    f->eax = write(*args[0], (const void*)(*args[1]), *(unsigned *)args[2]);
    break;

    case SYS_SEEK:
    get_args(f->esp, 2);
    seek(*args[0],*(unsigned *)args[1]);
    break;

    case SYS_TELL:
    get_args(f->esp, 1);
    f->eax = tell(*args[0]);
    break;

    case SYS_CLOSE:
    get_args(f->esp, 1);
    close(*args[0]);
    break;

    default:
    syscall_exit(-1);
    break;
  }
}

void
get_args(void *esp, const int total_args){
    int *ptr;
      for(int i=1; i<=total_args; i++){
        ptr = esp + 4*i;
        is_ptr_bad(ptr);
        args[i-1] = ptr;
    }
}

void 
halt (void)
{
  shutdown_power_off();
}

pid_t 
exec (const char *cmd_line)
{
  return process_execute(cmd_line);
}

int 
wait (pid_t pid)
{
  return process_wait(pid);
}

void
syscall_exit (int status) 
{
  struct thread *cur = thread_current ();
  char *name = cur->name;
  printf ("%s: exit(%d)\n", name, status);
  close_all_fds(cur->tid);

  if(!list_empty(&cur->parent->children_list))
  {
    struct child_details *child_detail = process_get_child_by_tid(cur->parent, cur->tid);
    child_detail->exit_status = status;
    child_detail->has_exit = true;
    if(child_detail->is_parent_waiting)
    {
      sema_up(&cur->parent->wait);
    }
  }
  thread_exit ();
}

bool 
create (const char *file, unsigned initial_size)
{
  if(file == NULL || file[0] == '\0')
    syscall_exit(-1);
  lock_acquire (&lock);
  bool res = filesys_create(file, initial_size);
  lock_release (&lock);
  return res;
}

bool 
remove (const char *file)
{
  if(file == NULL || file[0] == '\0')
    syscall_exit(-1);

  lock_acquire (&lock);
  bool res = filesys_remove(file);
  lock_release (&lock);
  return res;
}

int 
open (const char *file)
{
  if(file == NULL)
    syscall_exit(-1);

  lock_acquire (&lock);
  struct file* opened_file = filesys_open(file);
  lock_release (&lock);

  if(opened_file == NULL)
    return -1;

  struct file_details* file_detail = palloc_get_page (0);
  file_detail->fd = ++fd_count;
  file_detail->file_ptr = opened_file;
  file_detail->tid = thread_current ()->tid;
  list_push_back (&files_list, &file_detail->elem);
  return file_detail->fd;
}

int 
filesize (int fd)
{ 
  struct file_details *file_detail = get_file_from_list(fd);
  lock_acquire (&lock);
  int size = file_length(file_detail->file_ptr);
  lock_release (&lock);
  return size;
}

int 
read (int fd, void *buffer, unsigned size)
{
  if(fd == STDIN_FILENO)
  {
    for(unsigned i=0;i<size;i++)
    {
      *((char*)buffer+i) = input_getc();
    }
    return size;
  }

  struct file_details *file_detail = get_file_from_list(fd);
  if(file_detail == NULL)
    return -1;
  lock_acquire (&lock);
  int bytes_read = file_read(file_detail->file_ptr, buffer, (int32_t) size);
  lock_release (&lock);
  return bytes_read;
}

int 
write (int fd, const void *buffer, unsigned size)
{
  if(fd == STDIN_FILENO)
    syscall_exit(-1);

  if(fd==STDOUT_FILENO)
  {
    putbuf(buffer, size);
    return size;
  }
  struct file_details *file_detail = get_file_from_list(fd);
  if(file_detail == NULL)
    return -1;

  lock_acquire (&lock);
  int bytes_read = file_write(file_detail->file_ptr, buffer, (int32_t) size);
  lock_release (&lock);
  return bytes_read;
}

void 
seek (int fd, unsigned position)
{
  struct file_details *file_detail = get_file_from_list(fd);
  lock_acquire (&lock);
  file_seek(file_detail->file_ptr, position);
  lock_release (&lock);
}

unsigned 
tell (int fd)
{
  struct file_details *file_detail = get_file_from_list(fd);
  lock_acquire (&lock);
  int pos = file_tell(file_detail->file_ptr);
  lock_release (&lock);
  return pos;
}

void 
close (int fd){
  if(fd == STDIN_FILENO || fd == STDOUT_FILENO)
    syscall_exit(-1);

  struct file_details *file_detail = get_file_from_list(fd);
  if(file_detail == NULL || file_detail->tid != thread_current()->tid)
    syscall_exit(-1);

  lock_acquire (&lock);
  file_close(file_detail->file_ptr);
  lock_release (&lock);
  list_remove(&file_detail->elem);
  palloc_free_page(file_detail);
}

void
is_buf_valid(void *buf, unsigned size){
  char *ptr = (char *)buf + size;
  if(ptr == NULL || is_kernel_vaddr((void *)ptr) ||
    pagedir_get_page(thread_current()->pagedir, (const void *)ptr) == NULL)
      syscall_exit(-1);
}

void is_ptr_bad(int *ptr){
if(ptr == NULL || is_kernel_vaddr((const void *)ptr) ||
   pagedir_get_page(thread_current()->pagedir, (const void *)ptr) == NULL)
    syscall_exit(-1);
}

struct file_details *
get_file_from_list(int fd){

  struct list_elem *e;

  for (e = list_begin (&files_list); e != list_end (&files_list);
   e = list_next (e))
  {
    struct file_details *file_detail = list_entry (e, struct file_details, elem);
    if(file_detail->fd == fd)
        return file_detail;
  }
  return NULL;
}

void 
close_all_fds(tid_t tid)
{
  struct list_elem *e = list_begin (&files_list);

  while(e != list_end (&files_list))
  {
    struct file_details *file_detail = list_entry (e, struct file_details, elem);
    e = list_next (e);
    if(file_detail->tid == tid)
    {
      lock_acquire (&lock);
      file_close(file_detail->file_ptr);
      lock_release (&lock);
      list_remove(&file_detail->elem);
      palloc_free_page(file_detail);
    }
  }
}
