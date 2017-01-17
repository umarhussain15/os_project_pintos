#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"
#include "threads/synch.h"

 // struct semaphore cont;
// static struct semaphore openlock;
struct process_info {
  tid_t pid;
  char name[256];
  int exit_code;
  struct semaphore 
  cont,// for exec to check if process is loaded or not
  wait;//for synch between parent child in wait call
  // process's sema for list of files
  // struct semaphore flist;
  // struct lock elock;
  // variable for wait twice check
  int waiting;
  struct list_elem elem;
  // struct list my_allfiles;
  int loaded,
  // variable for storing global fd number
  filed;
  //current open file for fast access
  // and avoiding list traversal
  int cfiled;
  struct file * cfs;
  // char fname[256];
};

// because process loves to open multiple files
// so this element will be stored in process's file list
struct process_onefile{
	struct process_info * fileof;
	int filed;
  // char fname[256];
	struct file * fs;
  struct list_elem elem;
};
void set_process_exitcode(tid_t pid, int exitcode) ;
struct process_info* get_process_info(tid_t pid) ;
void add_process_to_list(const char* name,  tid_t tid) ;
void delete_process(tid_t pid);
void syscall_init (void);

#endif /* userprog/syscall.h */
