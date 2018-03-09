#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "lib/kernel/list.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct start_process_info
{
  char* file_name;
  char* cmd_line;
  struct semaphore* p_sp;
  int c_status;
};

#endif /* userprog/process.h */
