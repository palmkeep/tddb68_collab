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
  struct parent_child_rel* rel;
  struct thread* parent;
};


tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct start_process_info
{
  char* file_name;
  char* cmd_line;
  struct semaphore* p_sp;
  struct semaphore* c_sp;
  struct thread* p_ptr;
  int c_status;
  int alive_count;
};

#endif /* userprog/process.h */
