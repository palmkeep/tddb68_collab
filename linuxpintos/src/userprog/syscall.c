#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

//Our imports
#include "threads/init.h"
#include "filesys/filesys.h"
#include "lib/kernel/stdio.h"
#include "lib/kernel/console.h"


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
      // f->eax;    // Output amount of chars written
    }
    else
    {
      //struct file* file_struct = filesys_open();
    }
  }
  else
  {
    printf ("IMPLEMENT THIS SYSCALL!\n");
    printf ("SYS_CALL_NR: %d\n", syscall_nr);

    thread_exit ();
  }
}
