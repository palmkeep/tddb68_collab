#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "lib/kernel/list.h"

struct start_process_info
{
  char* file_name;
  bool waiting;
  struct semaphore* p_sp;
  struct semaphore* c_sp;
  char* args;
};


tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
