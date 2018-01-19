#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
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
  if (f->esp == SYS_HALT)
  {
    power_off();
  }
  else if (f->esp == SYS_CREATE)
  {


    // If created set f->eax = 1
    // If not created set f->eax = 0
  }
  else
  {
    printf ("YOU SHIT IMPLEMENT THIS SYSCALL!\n");
    thread_exit ();
  }
}
