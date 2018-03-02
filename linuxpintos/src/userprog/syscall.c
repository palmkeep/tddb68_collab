#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

//Our imports
#include "threads/init.h"

#include "userprog/process.h"
#include "userprog/pagedir.h"

#include "filesys/filesys.h"
#include "filesys/file.h"

#include "lib/kernel/stdio.h"
#include "lib/kernel/console.h"

#include "devices/input.h"

#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static int
check_user_ptr(const void* ptr)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  pd = cur->pagedir;

  if ( !is_user_vaddr(ptr) || NULL == ptr || NULL == pagedir_get_page(pd, ptr) )
  {
    return -1;
  }
  else
  {
    return 1;
  }
}

static int
check_ptr(const void* ptr)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  pd = cur->pagedir;

  if ( !is_kernel_vaddr(ptr) || NULL == ptr )
  {
    return -1;
  }
  else
  {
    return 1;
  }
}




static bool
check_user_str_ptr(const char* ptr)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  pd = cur->pagedir;

  int i = 0;
  char c = *(ptr+i);
  do
  {
    if ( NULL == pagedir_get_page(pd, ptr+i) ) { return false; }
    c = *(ptr+(i++));
  } while( c != '\0');

  if ( NULL == pagedir_get_page(pd, ptr+i) ) { return false; } // Check nullptr

  if (i != 0) { return true; }
	      { return false;}
}


static bool
check_user_buf_ptr(const char* ptr, const int size)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  pd = cur->pagedir;

  int i = 0;
  while (i < size)
  {
    if ( NULL == pagedir_get_page(pd, ptr+i) ) { return false; }
  };

  if (i != 0) { return true; }
	      { return false;}
}





/* Sys-call specific functions */

static void 
call_exit(struct intr_frame* f, int status)
{
  //printf("[call_exit] entrance . . . ");
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
    cur->p_rel->alive_count -= 1;
  }
  

  printf("%s: exit(%d)\n", cur->name, status);
  thread_exit();
}

static void
call_exec(struct intr_frame* f)
{
  const char* cmd_line = *(char**)(f->esp+4); // No error correction  
  tid_t pid = process_execute(cmd_line);

  if (pid == TID_ERROR) { f->eax =  -1;}
  else			{ f->eax = pid;}
}

static void
call_wait(struct intr_frame* f, tid_t tid)
{
  int return_val = process_wait(tid);
  f->eax = return_val;
}

static void
call_create(struct intr_frame *f, char* filename_ptr, off_t file_size)
{
  if ( *filename_ptr != '\0' && filesys_create( filename_ptr, file_size) )
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
  int ptr_stat = check_user_ptr( f->esp );
  if ( ptr_stat == 1 && NULL != f->esp )
  {

    unsigned syscall_nr = *(unsigned*)f->esp;
    int s;
  
    switch (syscall_nr)
    {
      case SYS_HALT:
        power_off();
        break;

      case SYS_EXIT:
	if ( 1 == check_user_ptr( (f->esp+4) ) )
	{
	  call_exit( f, *(int*)(f->esp+4) );
	}
	else
	{
	  call_exit(f, -1); // Exit(-1) on bad ptr
	}
        break;

      case SYS_EXEC:
        call_exec(f);
        break;

      case SYS_WAIT:
	if (check_user_ptr(f->esp+4))
	{
	  call_wait( f, *(tid_t*)(f->esp+4) );
	}
	else
	{
	  call_exit(f, -1);
	}
	break;
  
      case SYS_CREATE:
	if ( check_user_str_ptr( *(char**)(f->esp+4) ) && check_user_ptr( (f->esp+8) ) && 0 <= *(off_t*)(f->esp+8)  )
	{
	  call_create( f, *(char**)(f->esp+4), *(off_t*)(f->esp+8) );
	}
	else
	{
	  call_exit(f, -1);
	}
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
  
      default:
	call_exit(f, -1); // Bad syscall number passed
        break;
    }
  }
  else if (ptr_stat == -1)
  {
    call_exit(f, -1);
  }
  else
  {
    call_exit(f, -1);
  }
}



