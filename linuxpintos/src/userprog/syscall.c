#include "userprog/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

//Our imports
#include "threads/init.h"
#include "threads/malloc.h"

#include "userprog/process.h"
#include "userprog/pagedir.h"

#include "filesys/filesys.h"
#include "filesys/file.h"

#include "lib/kernel/stdio.h"
#include "lib/kernel/console.h"

#include "lib/string.h"

#include "devices/input.h"

#include "userprog/process.h"

#include "threads/vaddr.h"
#include "threads/synch.h"

#include "filesys/inode.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool
check_user_ptr(const void* ptr)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  pd = cur->pagedir;

  if ( !is_user_vaddr(ptr) || NULL == ptr || NULL == pagedir_get_page(pd, ptr) )
  {
    return false;
  }
  else
  {
    return true;
  }
}

/*
static bool
check_kernel_ptr(const void* ptr)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  pd = cur->pagedir; // ???

  if ( !is_kernel_vaddr(ptr) || NULL == ptr )
  {
    return false;
  }
  else
  {
    return true;
  }
}
*/

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
check_user_buf_ptr(const void* ptr, const unsigned size)
{
//  printf("CHECK BUFFER\n");
  struct thread *cur = thread_current ();
  uint32_t *pd;
  pd = cur->pagedir;

  unsigned i = 0;

  while (i < size)
  {
    printf("%c",ptr+i);
    if ( NULL == pagedir_get_page(pd, ptr+i) ) { return false; }
    i++;
  }

  if (i != 0) { return true; }
	      { return false;}
}





/* Sys-call specific functions */
static void 
call_exit(struct intr_frame* f, int status)
{
  struct thread* cur = thread_current();
  f->eax = status;
  cur->ret_status = status;

  printf("%s: exit(%d)\n", cur->name, cur->ret_status);
  thread_exit();
}

static void
call_exec( struct intr_frame* f, char* cmd_line )
{
  tid_t pid = process_execute(cmd_line);
  if (pid == TID_ERROR) {
    f->eax = -1;
  }
  else
  { 
    f->eax = pid;
  }
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
  if ( strlen( filename_ptr ) != 0  )
  {
    if ( filesys_create( filename_ptr, file_size )  )
    {
      f->eax = true;
    }
    else
    {
      f->eax = false;
    }
  }
  else
  {
    f->eax = false;
  }
}


/* The removal of the file is delayed until it is not opened by any process.
 * Make sure that this counter is synched.
 * */
static void
call_remove(struct intr_frame *f, const char* file_name)
{
  struct file *file = filesys_open(file_name);
  if ( file != NULL)
  {
    struct inode *inode = file_get_inode(file);

    inode_remove(inode);

    f->eax = true;
  }
  else
  {
    f->eax = false;
  }
}


static void
call_open(struct intr_frame *f, char* filename)
{
  struct thread* current_thread = thread_current();
  int fd = add_file_to_fd(current_thread, filename);
  f->eax = fd;  // Returns -1 if the file could not be opened
  printf("SYS_OPEN ret: %d\n", fd);
}


static void
call_filesize(struct intr_frame *f, int file_descriptor)
{
  struct file* file_ptr = get_file_from_fd( thread_current(), file_descriptor);
  int s = (int)file_length( file_ptr );
  f->eax = s;
  printf("SYS_FILESIZE ret: %d\n", s);
}


// SYS_READ Does not work for large buffers
static void
call_read(struct intr_frame *f, int fd, void *buffer, off_t size)
{
  printf("In SYS_READ\n");
  printf("Buf ptr: %p\n", buffer);
  printf("Size: %u\n", size);
  if( check_user_buf_ptr( buffer, size ) ) 
  {
    printf("AA\n");
    struct thread* current_thread = thread_current();
  
    struct file* file_struct;
    if (fd == 0)  // Read from keyboard
    {
      off_t it = 0;
      while (it < size)
      {
        uint8_t ch = input_getc();
        ((uint8_t*)buffer)[it] = ch;
        it++;
      }
      f->eax = size;
    }
    else if (fd == 1) // Read from STDOUT
    {
      printf("Exit with -1\n");
      f->eax = -1;
    }
    else              // Read from file
    {
      printf("B\n");
      file_struct = get_file_from_fd(current_thread, fd);
      if (file_struct == NULL)
      {
	printf("Call exit -1");
        call_exit(f,-1);//might be bad???
	//f->eax = -1;// Error reading file, return -1 error code
      }
      else
      {
	printf("C\n");
        f->eax = file_read( file_struct, buffer, size );  // Return number of bytes read
      }
    }
  }
  else
  {
    printf("Call exit -1 B\n");
    call_exit(f,-1); 
    //f->eax = -1;
  }
}


static void
call_write(struct intr_frame *f, int file_descriptor, void* buffer, unsigned size)
{
  if (file_descriptor == 1)
  {
    size_t size_buffer = (size_t)size;  // Recast to size_t for use with putbuf()
    char* char_buffer = (char*)buffer;  // Recast to write as char to console

    putbuf(char_buffer, size_buffer);
  printf("End SYS_WRITE, buff size: %d\n", size_buffer);
    f->eax = size_buffer;               // Output amount of chars written
  }
  else
  {
    off_t size_buffer = (off_t)size;
    struct thread* current_thread = thread_current();
    struct file* file_ptr = get_file_from_fd(current_thread, file_descriptor);
    if ( file_ptr == NULL)
    {
      call_exit(f, -1);
    }

    file_write( file_ptr, buffer, size_buffer );
  printf("End SYS_WRITE, buff size: %d\n", size_buffer);
    f->eax = size_buffer;
  }
}

// Remove intr_frame from f-call
static void
call_seek(struct intr_frame *f, int file_descriptor, unsigned position)
{
  struct file* file_ptr = get_file_from_fd( thread_current(), file_descriptor);
  file_seek( file_ptr, position );
}


static void
call_tell(struct intr_frame *f, int file_descriptor)
{
  struct file* file_ptr = get_file_from_fd( thread_current(), file_descriptor);
  f->eax = (unsigned)file_tell( file_ptr );
}


static void
call_close(struct intr_frame *f)
{
  if(check_user_ptr(f->esp+4) && ((*(int*)(f->esp+4)) != 0 && (*(int*)(f->esp+4) != 1)) )
  {
    int fd = *(int*)(f->esp+4);
    struct thread* current_thread = thread_current();
    close_file_from_fd(current_thread, fd);
  }
  else
  {
    call_exit(f,-1);//tried to be fishy 
  }
}










/* syscall handler */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  if ( check_user_ptr(f->esp) )
  {
    unsigned syscall_nr = *(unsigned*)f->esp;
    int s;

    printf("SYSCALL NR: %d\n", syscall_nr);
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
	if ( check_user_ptr( f->esp+4 ) && check_user_str_ptr( *(char**)(f->esp+4) ) )
	{
          call_exec(f, *(char**)(f->esp+4) );
	}
	else
	{
	  call_exit(f, -1);
	}
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
	  call_exit(f, -1); // Bad ptr
	}
        break;

      case SYS_REMOVE:
	if (check_user_ptr(f->esp+4) && check_user_str_ptr( *(char**)(f->esp+4) ) )
	{
	  const char* file_name = *(char**)(f->esp+4);
	  call_remove(f, file_name);
	}
	else
	{
	  f->eax = false;
	}
	break;
    
      case SYS_OPEN:
	if ( check_user_str_ptr( *(char**)(f->esp+4) ) )
	{
	  call_open(f, *(char**)(f->esp+4));
	}
	else
	{
	  call_exit(f, -1); // Bad ptr
	}
        break;

      case SYS_FILESIZE:
	if (check_user_ptr( f->esp+4 ))
	{
	  call_filesize(f, *(int*)(f->esp+4));
	}
	else
	{
	  f->eax = -1;
	}

      case SYS_READ:
	if ( check_user_ptr(f->esp+4) && check_user_ptr(f->esp+8) && check_user_ptr(f->esp+12) )
	{
	  printf("SYS_READ buffer ptr: %p\n", f->esp+8);
	  printf("SYS_READ size: %u\n", (off_t)*(unsigned*)(f->esp+12));
	  printf("SYS_READ size ptr: %p\n", f->esp+12);
	  call_read( f, *(int*)(f->esp+4), *(void**)(f->esp+8), (off_t)*(unsigned*)(f->esp+12) );
	}
	else
	{
	  printf("Exit in switch\n");
	  call_exit(f, -1);
	}
        break; 
  
      case SYS_WRITE:
	if ( check_user_ptr( f->esp+4 ) && check_user_ptr( f->esp+12 ) )
	{
	  unsigned size = *(unsigned*)(f->esp+12);
	  if ( size != 0 )
	  {
	    if  ( check_user_ptr( f->esp+8 ) && check_user_buf_ptr( *(void**)(f->esp+8), size ) )
	    {
	      call_write(f, *(int*)(f->esp+4), *(void**)(f->esp+8), *(unsigned*)(f->esp+12));
	    }
	    else
	    {
	      call_exit(f, -1);
	    }
	  }
	  else
	  {
	    f->eax = 0;
	    break;
	  }
	}
	else
	{
	  call_exit(f, -1);
	}
        break;

      case SYS_SEEK:
	if ( check_user_ptr( f->esp+4 ) && check_user_ptr( f->esp+8) )
	{
	  call_seek( f, *(int*)( f->esp+4 ), *(unsigned*)(f->esp+8) );
	}
	else
	{
	  call_exit(f, -1);
	}
	break;

      case SYS_TELL:
	if ( check_user_ptr( f->esp+4) )
	{
	  call_tell( f, *(int*)( f->esp+4) );
	}
	else
	{
	  call_exit(f, -1);
	}
	break;
      
      case SYS_CLOSE:
	call_close(f);
	break;
  
      default:
	call_exit(f, -1); // Bad syscall number passed
        break;
    }
  }
  else
  {
    call_exit(f, -1); // Bad syscall number pointer
  }
}



