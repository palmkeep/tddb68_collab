#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include "threads/synch.h"

static thread_func start_process NO_RETURN;


static bool load (const char *cmdline, void (**eip) (void), void **esp);


/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char* command_line) 
{
  printf("Process_exec entrance\n");

  char command_line_copy[sizeof(command_line)];
  strlcpy (command_line_copy, command_line, PGSIZE);

  int it = 0;
  printf("\n");
  while (command_line[it] != ' ' && command_line[it] != '\0')
  {
    it++;
  }

  char* args = NULL;
  if (command_line_copy[it] == ' ')
  {
    command_line_copy[it] = '\0';
    args = &command_line_copy[it+1];
  }
  char *fn_copy;

  char* file_name = command_line_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  //strlcpy (fn_copy, file_name, (it+1)*sizeof(char));
  strlcpy (fn_copy, command_line, (it+1)*sizeof(char));

  /* Create a new thread to execute FILE_NAME. */
  struct semaphore p_sp;
  sema_init (&p_sp, 0);
  struct semaphore c_sp;
  sema_init (&c_sp, 0);

  // Create a child_rel for main thread
  if (thread_current()->child_rel == NULL)
  {
    thread_current()->child_rel = (struct parent_Child_rel*)( malloc(sizeof(struct parent_child_rel)) );
    thread_current()->child_rel->parent_alive = true;
    thread_current()->child_rel->alive_count = 1;
    thread_current()->child_relation_exists = true;
  }
  struct start_process_info new_process_info;
  new_process_info.file_name = fn_copy;
  new_process_info.waiting = true;
  new_process_info.p_sp = &p_sp;
  new_process_info.c_sp = &c_sp;
  new_process_info.args = args;
  new_process_info.rel = thread_current()->child_rel;
  new_process_info.parent = thread_current();

  printf("Thread_create entrance\n");
  tid = thread_create (file_name, PRI_DEFAULT, start_process, &new_process_info);
  printf("Thread_create exit\n");

  sema_down(&p_sp);


  if (tid == TID_ERROR)
  {
    palloc_free_page (fn_copy);
  }
  else
  {

  }

  printf("Process_exec exit\n");
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void* info)
{
  printf("Start_process entrance \n");
  struct start_process_info* args = (struct start_process_info*)info;
  char *file_name = args->file_name;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  if (args->waiting)
  {
    if (success)
    {
      sema_up(args->p_sp);   // Wake waiting parent-thread
      //sema_down(args->c_sp);   // Wake waiting parent-thread
    }
    else
    {

    }
  }


  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success) 
    thread_exit ();

  printf("Stack-alloc: load successful\n");
  unsigned arg_str_len = (unsigned)sizeof(args);
  unsigned quad_offset = arg_str_len % 4;

  if_.esp -= arg_str_len;
  strlcpy ((char*)(if_.esp), args->args, sizeof(args));
  char* stri = (char*)if_.esp;
  char* arg_list = (char*)if_.esp;
  int number_args = 0;
  while(*stri != '\0' )
  {
    if (*stri == ' ' && (*(stri+1) != '\0' || *(stri+1) == ' ')){
      number_args++;
    }
    stri++;
  }
  if_.esp -= quad_offset;
  
  *((char*)(if_.esp)+1) = '\0';
  if_.esp -= 4;;

  char* token = (char*)&if_.esp;
  char* save_ptr;
  int i = 1;
  for (	token = strtok_r(args->args, " ", &save_ptr); token != NULL;
	token = strtok_r(NULL, " ", &save_ptr))
  {
    *((char**)(if_.esp+(i*4))) = token;
    i++;
  }

  if_.esp -= 12;
  *(int*)(if_.esp+4) = number_args;
  *(char**)(if_.esp+8) = &arg_list;


  struct thread* cur = thread_current();  //Does work inside start_process()


  
  // Set proper parent_rel for thread
  cur->parent_rel = args->rel;
  printf("args->rel pointer: %p\n", args->rel);
  if (args->rel != NULL)
  {
    (*(cur->parent_rel)).alive_count += 1;
  }

  // Init. $returned_children
  printf("Malloc $returned_children\n");
  lock_acquire( &(cur->returned_children_list_lock) );
  cur->returned_children = (struct list*)( malloc(sizeof( struct list )) );
  printf("$returned_children gets ptr: %p\n", cur->returned_children);
  list_init( cur->returned_children );
  lock_release( &(cur->returned_children_list_lock) );

  // Init. $parent_rel
  cur->parent = args->parent;

  // Init. $child_rel
  printf("Malloc $child_rel\n");
  cur->child_rel = (struct parent_child_rel*)( malloc(sizeof( struct parent_child_rel )) );
  printf("$child_rel gets ptr: %p\n", cur->child_rel);
  printf("Set $child_rel vars:\n");

  cur->child_rel->parent_alive = true;
  cur->child_rel->alive_count = 1;

  cur->child_relation_exists = true;
  printf("$child_rel vars. set\n");



  printf("Finished init. thread vars.\n");



  /*
  printf("Allocating space for $returned_children\n");
  cur->returned_children = (struct list*)( malloc(sizeof( struct list )) );
  list_init( cur->returned_children );

  printf("Start malloc");
  cur->child_rel = (struct parent_child_rel*)( malloc(sizeof(struct parent_child_rel)) );
  printf("Start_process Thread_current child_rel ptr: %p\n", cur->child_rel);
  printf("Exit malloc");

  cur->child_rel->parent_alive = true;
  cur->child_rel->alive_count = 1;
  cur->child_relation_exists = true;
  */


  printf("Start_process exit \n");
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  printf("Process_wait entrance \n");

  struct thread* t = thread_current();
  t->waiting_for_child_id = child_tid;


  printf("$returend_children ptr: %p\n", t->returned_children);
  if (t->returned_children == NULL)
  {
    printf("Allocating list\n");
    t->returned_children = (struct list*)( malloc(sizeof(struct list)) );
    list_init(t->returned_children);
  }
  printf("Dereferencing pointer at: %p\n", t->returned_children);

  struct list returned_children = *(t->returned_children);

  bool child_returned = false;
  struct child_return_struct* returned_child;

  printf("thread name: %s\n", t->name);
  printf("Test3\n");
  printf("Is list empty: %d\n", list_empty(&returned_children));
  lock_acquire( &(t->returned_children_list_lock) );
  if ( !list_empty(t->returned_children) )
  {
    printf("List not empty. Begin iter.\n");
    struct list_elem* e;
    for (	e = list_begin (t->returned_children);
  	e != list_end (t->returned_children);
  	e = list_next(e))
    {
      printf("elem ptr: %p\n", e);
      returned_child = list_entry(e, struct child_return_struct, elem);
      if (returned_child->id == child_tid)
      {
        child_returned = true;
        break;
      }
    }
    printf("elem ptr: %p\n", list_begin(t->returned_children));
  }
  lock_release( &(t->returned_children_list_lock) );

  printf("Test4\n");

  if (!child_returned)
  {
    struct semaphore sema;
    sema_init(&sema, 0);
    t->waiting_for_child = &sema;
    t->waiting_for_child_id = child_tid;

    if (!child_returned)
    {
      printf("Wait for child\n");
      sema_down(&sema);
    }
    struct list_elem* e;
    for(e = list_begin (&returned_children);
  	e != list_end (&returned_children);
  	e = list_next(e))
    {
      returned_child = list_entry(e, struct child_return_struct, elem);
      if (returned_child->id == child_tid)
      {
        break;
      }
    }
  }
  printf("Process_wait exit\n");
  //t->waiting_for_child_id = NULL;
  return returned_child->id;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  printf("Process_exit entrance\n");
  struct thread *cur = thread_current ();
  if ( list_is_interior(&(cur->waiting_elem)) ) 
  {
    list_remove ( &(cur->waiting_elem) ); //Might help so elements aren't still in waiting list
  }

  printf("sema_up parent . . . ");
  if (cur->parent != NULL && cur->tid == (cur->parent)->waiting_for_child_id)
  {
    sema_up( (cur->parent)->waiting_for_child ); //Wake parent as the child has returned its status
  }
  printf("sema_up parent finished\n");
  


  uint32_t *pd;
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
  printf("Process_exit exit\n");
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  printf("Load entrance\n");
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Set up stack. */
  if (!setup_stack (esp)){
    goto done;
  }

   /* Uncomment the following line to print some debug
     information. This will be useful when you debug the program
     stack.*/
#define STACK_DEBUG

#ifdef STACK_DEBUG
  printf("*esp is %p\nstack contents:\n", *esp);
  hex_dump((int)*esp , *esp, PHYS_BASE-*esp+16, true);
  /* The same information, only more verbose: */
  /* It prints every byte as if it was a char and every 32-bit aligned
     data as if it was a pointer. */
  void * ptr_save = PHYS_BASE;
  i=-15;
  while(ptr_save - i >= *esp) {
    char *whats_there = (char *)(ptr_save - i);
    // show the address ...
    printf("%x\t", (uint32_t)whats_there);
    // ... printable byte content ...
    if(*whats_there >= 32 && *whats_there < 127)
      printf("%c\t", *whats_there);
    else
      printf(" \t");
    // ... and 32-bit aligned content 
    if(i % 4 == 0) {
      uint32_t *wt_uint32 = (uint32_t *)(ptr_save - i);
      printf("%x\t", *wt_uint32);
      printf("\n-------");
      if(i != 0)
        printf("------------------------------------------------");
      else
        printf(" the border between KERNEL SPACE and USER SPACE ");
      printf("-------");
    }
    printf("\n");
    i++;
  }
#endif

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  printf("Load exit\n");
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  printf("Setup_stack entrance\n");
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  printf("Setup_stack exit\n");
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
