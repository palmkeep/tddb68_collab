#include "userprog/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

//Our imports
#include "threads/init.h"
#include "threads/malloc.h"

#include "filesys/filesys.h"
#include "filesys/file.h"

#include "lib/kernel/stdio.h"
#include "lib/kernel/console.h"

#include "devices/input.h"

#include "userprog/process.h"

#include "threads/vaddr.h"

#include "threads/synch.h"

#include <stdlib.h>

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Sys-call specific functions */
static void 
call_exit(struct intr_frame *f)
{
  /* We need to create a list of pid+return that every parent thread keeps track of
   * All children need a pointer to their parent so that they can add their return-
   * value to the list mentioned in prev. sentence.
   * */

  struct thread* parent = thread_current()->parent;
  int status = *(int*)(f->esp+4);
  f->eax = status;    // Might break horribly
  //very unsure seems right
  struct child_return_struct* child_return = malloc(sizeof(struct child_return_struct));
  child_return->id = thread_current()->tid;
  child_return->returned_val = status;
  list_push_back( &(parent->returned_children), &child_return->elem );

  if (thread_current()->tid == parent->waiting_for_child_id)
  {
    sema_up(parent->waiting_for_child);
  }

  //process_exit();     // Free process resources
  thread_exit();
}

static void
call_wait(struct intr_frame *f)
{
  struct thread* t = thread_current();

  tid_t child_id = *(tid_t*)(f->esp+4);
  t->waiting_for_child_id = child_id;
  struct list returned_children = t->returned_children;

  bool child_returned = false;
  struct child_return_struct* returned_child;

  struct list_elem* e;
  for (	e = list_begin (&returned_children);
	e != list_end (&returned_children);
	e = list_next(e))
  {
    returned_child = list_entry(e, struct child_return_struct, elem);
    if (returned_child->id == child_id)
    {
      break;
    }
  }

  if (!child_returned)
  {
    struct semaphore sema;
    sema_init(&sema, 0);
    t->waiting_for_child = &sema;
    t->waiting_for_child_id = child_id;

    if (!child_returned)
    {
      sema_down(&sema);
    }
    struct list_elem* e;
   for (	e = list_begin (&returned_children);
  	e != list_end (&returned_children);
  	e = list_next(e))
    {
      returned_child = list_entry(e, struct child_return_struct, elem);
      if (returned_child->id == child_id)
      {
        break;
      }
    }
  }

  f->eax = returned_child->returned_val;
}

static void
call_create(struct intr_frame *f)
{
  char* filename_pointer = *(char**)(f->esp+4);
  off_t file_size = *(unsigned*)(f->esp+8);
  if ( filesys_create( filename_pointer, file_size) )
  {
    f->eax = 1;
  }
  else
  {
    f->eax = 0;
  }
}

static void
call_open(struct intr_frame *f)
{
  char* filename = *(char**)(f->esp+4);
  struct thread* current_thread = thread_current();
  int fd = add_file_to_fd(current_thread, filename);
  f->eax = fd;  // Returns -1 if the file could not be opened
}

static void
call_close(struct intr_frame *f)
{
  int fd = *(int*)(f->esp+4);
  struct thread* current_thread = thread_current();
  close_file_from_fd(current_thread, fd);
}


static void
call_read(struct intr_frame *f)
{
  // SYS_READ Does not work for large buffers
  int fd = *(int*)(f->esp+4);
  void* buffer = *(void**)(f->esp+8);
  off_t size = (off_t)*(unsigned*)(f->esp+12);

  struct thread* current_thread = thread_current();

  struct file* file_struct;
  if (fd == 0)  // Read from keyboard
  {
    off_t it = 0;
    //uint8_t* byte_buffer = (uint8_t*)buffer;
    while (it <= size)
    {
      uint8_t ch = input_getc();
      ((uint8_t*)buffer)[it] = ch;
      it++;
    }
    f->eax = size;
  }
  else if (fd == 1) // Read from STDOUT
  {
    f->eax = -1;
  }
  else              // Read from file
  {
    file_struct = get_file_from_fd(current_thread, fd);
    if (file_struct == NULL)
    {
      f->eax = -1;// Error reading file, return -1 error code
    }
    else
    {
      f->eax = file_read( file_struct, buffer, size );  // Return number of bytes read
    }
  }
}

static void
call_write(struct intr_frame *f)
{
  int file_descriptor = *(int*)(f->esp+4);
  void* buffer = *(void**)(f->esp+8);
  unsigned size = *(unsigned*)(f->esp+12);

  if (file_descriptor == 1)
  {
    size_t size_buffer = (size_t)size;  // Recast to size_t for use with putbuf()
    char* char_buffer = (char*)buffer;  // Recast to write as char to console
    putbuf(char_buffer, size_buffer);
    f->eax = size_buffer;               // Output amount of chars written
  }
  else
  {
    off_t size_buffer = (off_t)size;
    struct thread* current_thread = thread_current();
    struct file* file_ptr = get_file_from_fd(current_thread, file_descriptor);
    file_write( file_ptr, buffer, size_buffer );
    f->eax = size_buffer;
  }
}



static void
call_exec(struct intr_frame *f)
{
  char* command_line_args = *(char**)(f->esp+4);
  printf("In CALL_EXEC");
  if ( is_user_vaddr( (void*)command_line_args) && command_line_args != NULL)
  {
    char filename[64];
    int it = 0;
    printf("Hello 0");
    while (command_line_args[it] != ' ')
    {
      filename[it] = command_line_args[it];
      it++;
    }
    tid_t pid = process_execute(filename);
    f->eax = pid;
  }
  else
  {
    // Bad input: Pointer in kernel space or other user space
    printf("hello failure");
    f->eax = -1;
  }
}



/* syscall handler */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  unsigned syscall_nr = *(unsigned*)f->esp;
  printf("Syscall: %d\n", syscall_nr);

  switch (syscall_nr)
  {
    case SYS_HALT:
      power_off();
      break;

    case SYS_EXIT:
      call_exit(f);
      break;
    case SYS_WAIT:
      call_wait(f);
      break;

    case SYS_CREATE:
      call_create(f);
      break;
  
    case SYS_OPEN:
      call_open(f);
      break;

    case SYS_CLOSE:
      call_close(f);
      break;

    case SYS_READ:
      call_read(f);
      break; 

    case SYS_WRITE:
      call_write(f);
      break;
//removed the prevent from this syscall / Julius 
    case SYS_EXEC:
      call_exec(f);
      break;

    default:
      thread_exit ();
      break;
  } 
}



