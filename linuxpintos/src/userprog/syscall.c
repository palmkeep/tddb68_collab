#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

//Our imports
#include "threads/init.h"
#include "filesys/filesys.h"
#include "lib/kernel/stdio.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int sys_call_nr = *(int*)(f->esp);

  if ( sys_call_nr == SYS_HALT )
  {
    power_off();
  }
  else if ( sys_call_nr == SYS_CREATE )
  {
    if ( filesys_create(f->esp+4,f->esp+8) )
    {
      f->eax = 1;
    }
    else
    {
      f->eax = 0;
    }
  }
  else if ( sys_call_nr == SYS_WRITE )
  {

  }
  else
  {
    printf ("IMPLEMENT THIS SYSCALL!\n");
    printf ("SYS_CALL_NR: %d\n", sys_call_nr);
    printf ("SYS_CALL_CREATE: %d\n", SYS_CREATE);

    thread_exit ();
  }
}
