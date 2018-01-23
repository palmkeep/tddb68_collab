#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

//Our imports
#include "threads/init.h"

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
syscall_handler (struct intr_frame *f UNUSED) 
{
  unsigned syscall_nr = *(unsigned*)f->esp;

  if ( syscall_nr == SYS_HALT )
  {
    power_off();
  }
  else if ( syscall_nr == SYS_EXIT )
  {
    int status = *(int*)(f->esp+4);
    f->eax = status;
    thread_exit();
  }
  else if ( syscall_nr == SYS_CREATE )
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
  else if ( syscall_nr == SYS_OPEN)
  {
    char* filename = *(char**)(f->esp+4);
    struct thread* current_thread = thread_current();
    int fd = add_file_to_fd(current_thread, filename);

    f->eax = fd;  // Returns -1 if the file could not be opened
  }
  else if(syscall_nr == SYS_CLOSE)
  {
    int fd = *(int*)(f->esp+4);
    struct thread* current_thread = thread_current();
    close_file_from_fd(current_thread, fd);
  }
  else if( syscall_nr == SYS_READ )
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
    else
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
  else if ( syscall_nr == SYS_WRITE )
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
  else
  {
    printf ("IMPLEMENT THIS SYSCALL!\n");
    printf ("SYS_CALL_NR: %d\n", syscall_nr);

    thread_exit ();
  }
}
