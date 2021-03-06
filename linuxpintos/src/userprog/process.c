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
#include "threads/malloc.h"

#include "threads/synch.h"

static thread_func start_process NO_RETURN;

static bool load (const char* file_name, const char *cmdline, void (**eip) (void), void **esp);

/* Frees all structures linked in a given list.
 * Does not call free() on any of the structures own variables. */
static
void free_list(struct list* list)
{
  struct list_elem* e;
  for ( e  = list_begin(list); 
	e != list_end(list);
	e
      )
  {
    struct child_return* tmp = list_entry(e, struct child_return, elem);
    e = list_next(e); 
    free(tmp);
  }
}

/* Frees all the variables contained in a thread_relation.
 * Does not remove the thread_relation itself, this call to
 * free() must be done elsewhere (preferably right after). */
static
void free_relationship_contents( struct thread_relation* rel)
{
  off_t size = sizeof(struct thread);
  sema_up(rel->p_sema);
  free( rel->p_sema ); 
  free( rel->return_lock );

  free_list( rel->return_list );
  free( rel->return_list );
}


/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns. Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char* command_line) 
{
  //printf("[process_execute entrance] . . . ");

  char* name_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
   * Otherwise there's a race between the caller and load(). */
  name_copy = palloc_get_page (0);
  if (name_copy == NULL)
  {
    return TID_ERROR;
  }

  /* Make copy of whole command line with only one space between args. */
  char* cmd_copy = (char*)( malloc( (1+strlen(command_line)*sizeof(char)) ) );
  strlcpy (cmd_copy, command_line, (1+strlen(command_line))*sizeof(char) );

  unsigned stri = 0;
  bool prv_ch_sp = true;
  unsigned cmd_len;
  for (stri; stri < strlen(command_line); stri++)
  {
    if ( cmd_copy[stri] == ' ' )
    {
      if (prv_ch_sp)
      {
	unsigned stri_cp = stri;
	for(stri_cp; stri_cp < strlen(command_line); stri_cp++)
	{
	  cmd_copy[stri_cp] = cmd_copy[stri_cp+1];
	}

	if (cmd_copy[stri_cp] == '\0' || stri_cp == strlen(command_line) )
	{
	  cmd_copy[stri_cp-1] = '\0';
	  cmd_len = stri_cp;
	}
	stri--;
      }
      prv_ch_sp = true;
    }
    else { prv_ch_sp = false; }
  }
  stri = strlen(command_line);
  int string_length = strlen(command_line);
  for (stri ;; stri--)
  {
    if (cmd_copy[stri] == ' ')
    {
      cmd_len -= 1;
      cmd_copy[stri] = '\0';
    }
    else  { break; }
  }

  /* Make copy of filename */
  int it = 0;
  while (cmd_copy[it] != ' ' && cmd_copy[it] != '\0') { it++; }
  strlcpy (name_copy, command_line, (it+1)*sizeof(char));




  /* Create a struct containing all the info needed by a new child
   * from it's parent.
   * Also used by child to return if it had an issue loading it's code
   * thereby forcing the child to immediately call thread_exit() on itself. */
  struct start_process_info* sh = (struct start_process_info*)( malloc(sizeof(struct start_process_info)));
  struct semaphore* p_sp = (struct semaphore*)( malloc(sizeof(struct semaphore)) );
  sema_init (p_sp, 0);
  
  sh->file_name = name_copy;

  char* cmd_line = (char*)( malloc( 1+(strlen(command_line) )*sizeof(char) ) );   // +1 for nullchar
  //strlcpy( cmd_line, cmd_copy, (1+cmd_len)*sizeof(char) ); // +1 for nullchar
  strlcpy( cmd_line, cmd_copy, (1+strlen(cmd_copy))*sizeof(char) ); // +1 for nullchar
  sh->cmd_line = cmd_line;

  free(cmd_copy);
  sh->p_sp = p_sp;

  /* Copy only the first 16 chars into file_name.
   * Used by thread_create() to set (struct thread)->name. */
  char file_name[16];
  int i=0;
  for (; i<16 ; i++)
  {
    (file_name)[i] = (sh->file_name)[i];
  }


  /* DEEEEEBBUUGG*/
//  printf("Start process; sh->filename: %s\n", sh->file_name);
//  printf("Start process; sh->cmd_line: %s\n", sh->cmd_line);

  /* END OF DEEEEEEBUG*/

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name, PRI_DEFAULT, start_process, sh);

  if (tid == TID_ERROR)
  {
    palloc_free_page (name_copy);

    free(sh->cmd_line);	// Free shared info and it's contents
    free(sh->p_sp);
    free(sh);
    return TID_ERROR; // FAILURE to thread_create()
  }


  sema_down(p_sp);    // Wait for child to load
  if (sh->c_status == TID_ERROR)
  {
    // child has called palloc_free_page()
    
    free(sh->cmd_line);	 // Free shared info and it's contents
    free(sh->p_sp);
    free(sh);
    return TID_ERROR; // FAILURE to load
  }

  free(sh->cmd_line); // Free shared info and it's contents 
  free(sh->p_sp);
  free(sh);


  // Create a child_tid entry in this (parent) threads list of children_tids
  struct thread* cur = thread_current();
  struct child_tid* new_child = (struct child_tid*)( malloc(sizeof(struct child_tid)) );
  new_child->tid = tid;
  list_push_back( cur->children_tids, &new_child->elem ); // No synch required, only used by thread_current()

  //printf("[process_execute exit]\n");
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *shared_info)
{
  //printf("[start_process] entrance . . . ");

  struct start_process_info* sh = (struct start_process_info*)shared_info;
  struct intr_frame if_;
  bool success;
  char* file_name = sh->file_name;
  char* cmd_line = sh->cmd_line;


  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  
  success = load (file_name, cmd_line, &if_.eip, &if_.esp);

  struct thread* t = thread_current();
  if ( !success )
  {
    sh->c_status = TID_ERROR;
  }
  else
  {
    sh->c_status = 1;
  }
  thread_current()->p_rel->alive_count += 1;
  sema_up(sh->p_sp); // Make sure no use of sh vars are below here

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success)
  {
    thread_current()->ret_status = -1;
    thread_exit ();
  }


  //printf("[start_process exit]\n");
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
process_wait (tid_t child_tid) 
{
  struct thread* cur = thread_current();
//printf("[process_wait] entrance\n");
//printf("By: %s | Waiting for pid: %d\n", cur->name, child_tid);
  bool child_returned = false;
  struct child_return* returned_child;
  struct child_tid* ret;


  struct list_elem* e;
  {
    bool is_waitable = false;
    for ( e  = list_begin(cur->children_tids); 
	  e != list_end(cur->children_tids);
	  e  = list_next(e)
	)
    {
      ret = list_entry(e, struct child_tid, elem);
      if ( ret->tid == child_tid )
      {
	is_waitable = true;
	break;
      }
    }
    if (!is_waitable && cur->name != "main\0") 
    {
      return -1;
    }
  }

  ret = list_entry(e, struct child_tid, elem);
  list_remove( &ret->elem );
  free(ret); 
  

  cur->c_rel->awaited_tid = child_tid;
  if ( !list_empty(cur->returned_children) )
  {
    lock_acquire( cur->return_lock );
//  struct list_elem* e;
    for ( e  = list_begin(cur->returned_children);
	  e != list_end(cur->returned_children);
	  e  = list_next(e)
	)    
    {
      returned_child = list_entry(e, struct child_return, elem);
      if (returned_child->tid == child_tid)
      {
	child_returned = true;
	break;
      }
    }
    lock_release( cur->return_lock );
  }
  //printf("first iter passed\n");

  if (!child_returned)
  {
    sema_down( cur->c_rel->p_sema );

    struct list_elem* e;
    lock_acquire( cur->return_lock );
    for ( e  = list_begin(cur->returned_children);
	  e != list_end(cur->returned_children);
	  e  = list_next(e)
	)
    {
      returned_child = list_entry(e, struct child_return, elem);
      if (returned_child->tid == child_tid)
      {
	child_returned = true;
	break;
      }
    }
    lock_release( cur->return_lock );
  }
  

//  printf("Found childs return\nChild tid: %d | Child return: %d\n", returned_child->tid, returned_child->returned_val);
//  printf("[process_wait] exit with status: %d \n", returned_child->returned_val);
  return returned_child->returned_val;
}

/* Free the current process's resources. */
void
process_exit (void)
{
//  printf("[process_exit] entrance\n");

  struct thread *cur = thread_current ();
  struct thread_relation* child_rel   = cur->c_rel;
  struct thread_relation* parent_rel  = cur->p_rel;



  /* Add current childs return value to parents return list */
  if (parent_rel->parent_alive)
  {
    struct child_return* child_return = (struct child_return*)( malloc(sizeof(struct child_return)) );
    child_return->tid = cur->tid;

    child_return->returned_val = cur->ret_status;

    lock_acquire(parent_rel->return_lock);

    struct list* return_list = parent_rel->return_list;
    list_push_back(return_list, &child_return->elem);

    lock_release(parent_rel->return_lock);

    if (cur->tid == parent_rel->awaited_tid)
    {
      sema_up( parent_rel->p_sema );
    }
  }

  // CHILD RELATIONSHIP
  // Dec. CHILD and free if alive_count == 0
  lock_acquire( child_rel->alive_lock );

  child_rel->alive_count -= 1;
  if (child_rel->alive_count == 0)
  {
    free_relationship_contents( child_rel ); //Causes problems
    lock_release( child_rel->alive_lock );
    free( child_rel );
  }
  else
  {
    lock_release( child_rel->alive_lock );
  }


  // PARENT RELATIONSHIP
  // Dec. alive_count and free if alive_count == 0
  lock_acquire( parent_rel->alive_lock );

  parent_rel->alive_count -= 1;
  if (parent_rel->alive_count == 0)
  {
    free_relationship_contents( parent_rel );
    lock_release( parent_rel->alive_lock );
    free( parent_rel);
  }
  else
  {
    lock_release( parent_rel->alive_lock );
  }



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
//  printf("[process_exit] exit\n");
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
load (const char* file_name, const char *cmd_line, void (**eip) (void), void **esp) 
{
//  printf("Load entrance\n");
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


  //printf("Manipulate stack . . . ");

  /* Set up stack. */
  if (!setup_stack (esp)){
    goto done;
  }


  // Setup stack frame
  unsigned arg_str_len = (unsigned)strlen(cmd_line);
  unsigned quad_offset = (arg_str_len+1) % 4;


  *esp -= arg_str_len + 1;

  strlcpy ( *(char**)(esp), cmd_line, ( 1+strlen(cmd_line) )*sizeof(char) );

  char* stri = *((char**)esp);
  char* debug_stri = &(*stri);
  int number_args = 1;
  while(*stri != '\0')
  {
    if ( *stri == ' ' && *(stri+1) != '\0' && *(stri+1) != ' ' )
    {
      number_args++;
    }
    stri++;
  }

  stri = *((char**)esp);  // Reset $stri

  *esp -= ((4 - quad_offset) + 4);  // Set *esp to the next block of 4 bytes that are aligned with 0,4,8 or C on the stack

  char* token = *(char**)(esp);
  char* save_ptr;
  int tok_it = 0;
  *esp -= 4*number_args;
  for ( token = strtok_r(stri, " ", &save_ptr); token != NULL;
	token = strtok_r(NULL, " ", &save_ptr))
  {
    *(char**)(((*esp)+(4*tok_it))) = token;
    tok_it++;
  }
  char** arg_list = (char**)(*esp);

  // If second {. . .} in lab spec-sheet needs to be implemented do so here
  

  *esp -= 4;
  *(char**)(*esp) = arg_list;
  *esp -= 4;
  *(int*)(*esp) = number_args;
  *esp -= 4;



   /* Uncomment the following line to print some debug
     information. This will be useful when you debug the program
     stack.*/
//#define STACK_DEBUG

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
//printf("Load exit\n");
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
//  printf("Setup_stack entrance\n");
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE; // DEBUG REMOVE -12 when arg passing is impl.
      else
        palloc_free_page (kpage);
    }
//  printf("Setup_stack exit\n");
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
