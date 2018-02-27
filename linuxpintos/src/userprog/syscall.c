#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

//Our imports
#include "threads/init.h"

#include "userprog/process.h"

#include "filesys/filesys.h"
#include "filesys/file.h"

#include "lib/kernel/stdio.h"
#include "lib/kernel/console.h"

#include "devices/input.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
call_exec(struct intr_frame* f)
{
  const char* cmd_line = *(char**)(f->esp+4); // No error correction  
  tid_t pid = process_execute(cmd_line);

  if (pid == TID_ERROR) { f->eax = -1; }
  else			{ f->eax = pid;}
}

/* Sys-call specific functions */

static void 
call_exit(struct intr_frame *f)
{
  int status = *(int*)(f->esp+4);
  f->eax = status;

  struct thread* cur = thread_current();
  struct child_return* child_return = (struct child_return*)( malloc(sizeof(struct child_return)) );
  child_return->tid = cur->tid;
  child_return->returned_val = status;

  /* Add current childs return value to parents return list */
  if (cur->p_rel->parent_alive)
  {
    lock_acquire(cur->p_rel->return_lock);
    struct list* return_list = cur->p_rel->return_list;
    list_push_back(return_list, &child_return->elem);
    lock_release(cur->p_rel->return_lock);
    sema_up( cur->p_rel->p_sema );
  }

  cur->p_rel->alive_count -= 1;


  thread_exit();
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




/* syscall handler */

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  unsigned syscall_nr = *(unsigned*)f->esp;

  switch (syscall_nr)
  {
    case SYS_EXEC:
      call_exec(f);
      break;

    case SYS_HALT:
      power_off();
      break;

    case SYS_EXIT:
      call_exit(f);
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

    default:  ;
      thread_exit ();
      break;
  } 
}



