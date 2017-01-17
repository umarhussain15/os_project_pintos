#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <stdint.h>
#include "filesys/inode.h"
static void syscall_handler (struct intr_frame *);
void exit(int);
void create(struct intr_frame *f, int* esp);
void open(struct intr_frame *f, int* esp) ;
void write(struct intr_frame *f, int* esp);
void read(struct intr_frame *f, int* esp);
void exec(struct intr_frame *f, int* esp);
void wait(struct intr_frame *f, int* esp);
void filesize(struct intr_frame *f, int* esp);
void close(struct intr_frame *f, int* esp);
void tellme(struct intr_frame *f, int* esp);
void seeker(struct intr_frame *f, int* esp);
void removeit(struct intr_frame *f, int* esp);
void delete_files_list(struct process_info *inf);

struct list 
// processes which are running currently
process_info_list, 
//list of process exited
process_exit_list;


struct list my_allfiles;
// using sema for sync process list access and file list
// static struct semaphore psema,flist;
// sema for sync on system calls
// i.e. functions called from filesys.c and file.c
static struct semaphore filesema;
static struct lock pil_lock;
// static struct lock flock,syslock;
static int fdc;

bool check_ptr(void* ptr) {
  struct thread* t = thread_current();
  if ( !is_user_vaddr (ptr) || pagedir_get_page(t->pagedir, ptr) == NULL) {
    return false;
  }
  return true;
}

struct process_info* get_process_info(tid_t pid) {
  struct list_elem *e;

  struct process_info* pi = NULL;
  lock_acquire(&pil_lock);
  // sema_down(&psema);
  for (e = list_begin (&process_info_list); e != list_end (&process_info_list);
       e = list_next (e))
  {
      struct process_info *p = list_entry (e, struct process_info, elem);
      if (p->pid == pid) {
        pi = p;
        break;
      }
      // free(p);
  }
  lock_release(&pil_lock);
  // sema_up(&psema);
  return pi;
}

struct process_info* get_process_exit(tid_t pid) {
  struct list_elem *e;

  struct process_info* pi = NULL;
  lock_acquire(&pil_lock);
  // sema_down(&psema);
  for (e = list_begin (&process_exit_list); e != list_end (&process_exit_list);
       e = list_next (e))
  {
      struct process_info *p = list_entry (e, struct process_info, elem);
      if (p->pid == pid) {
        pi = p;
        break;
      }
      // free(p);
  }
  lock_release(&pil_lock);
  // sema_up(&psema);
  return pi;
}

void add_process_to_list(const char* name, tid_t tid) {
  struct process_info *pi  = (struct process_info*) malloc (sizeof(struct process_info));
  pi->exit_code = -1000;
  pi->pid = tid;
  pi->loaded=-1;
  pi->cfiled=-1;
  pi->cfs=NULL;
  // pi->fname="";
  //initializing all locks and list which are part of
  //process info struct
  sema_init(&pi->cont,0);
  sema_init(&pi->wait,0);
  // sema_init(&pi->flist,1);
  // list_init(&pi->my_allfiles);
  pi->waiting=0;
  //file descriptor initialized with 2
  // always incremented before used first time
  pi->filed=++fdc;
  memcpy(pi->name, name, strlen(name)+1);
  lock_acquire(&pil_lock);
  // sema_down(&psema);
  list_push_back(&process_info_list, &pi->elem);
  lock_release(&pil_lock);
  // sema_up(&psema);
}

void set_process_exitcode(tid_t pid, int exit_code) {
  struct list_elem *e;

  lock_acquire(&pil_lock);
  // sema_down(&psema);
  for (e = list_begin (&process_info_list); e != list_end (&process_info_list);
       e = list_next (e))
    {
      struct process_info *p = list_entry (e, struct process_info, elem);
      if (p->pid == pid) {
        p->exit_code = exit_code;
        sema_up(&p->wait);
        list_remove(e);
        // since finihsed take out from running list and put in waiting list
        list_push_back(&process_exit_list,e);
        break;
      }
    }
      // sema_up(&psema);
  lock_release(&pil_lock);
}

// function to add "opened file" with its "fd" to a global files list
void add_file_to_list(int fd, struct file * f,struct process_info *inf){
  struct process_onefile *pi  = (struct process_onefile*) malloc (sizeof(struct process_onefile));
  pi->filed= fd;
  pi->fs = f;
  pi->fileof=inf;
  // memcpy(pi->fname, fname, strlen(fname)+1);
  // memcpy(inf->fname, fname, strlen(fname)+1);
  inf->filed=fd;
  inf->cfiled=fd;
  inf->cfs=f;
  // sema_down(&flist);
  // lock_acquire(&flock);
  list_push_back(&(my_allfiles), &pi->elem);
  // sema_up(&flist);
  // lock_release(&flock);
}

//used to get file struct and its fd from file list
struct process_onefile* get_file_data(int fd,struct process_info *inf){
  struct list_elem *e;

  struct process_onefile* pi = NULL;
  // sema_down(&flist);
  // lock_acquire(&flock);
  for (e = list_begin (&my_allfiles); e != list_end (&my_allfiles);
       e = list_next (e))
  {
      struct process_onefile *p = list_entry (e, struct process_onefile, elem);
      if (p->filed == fd && inf== p->fileof) {
        //printf("getting pid=%d, exit=%d\n", pid, p->exit_code);
        pi = p;
        break;
      }
      
  }
  // sema_up(&flist);
  // lock_release(&flock);
  return pi;
}

// if exit is due to error make sure to free resources
void free_res(struct process_info *inf){
  struct list_elem *e;
  // sema_down(&flist);
  // lock_acquire(&flock);
  for (e = list_begin (&my_allfiles); e != list_end (&my_allfiles);
       e = list_next (e))
  {
      struct process_onefile *p = list_entry (e, struct process_onefile, elem);
      if (inf== p->fileof) {
        file_close(p->fs);
        // free(p);
      }
      // free(p);
  }

}
//remove file details from file list when close is called for given file
// and also close that file 
int delete_file_fr_list(int fd,struct process_info *inf){
  struct list_elem *e;
  // sema_down(&flist);
  // lock_acquire(&flock);
  for (e = list_begin (&my_allfiles); e != list_end (&my_allfiles);
       e = list_next (e))
  {
      struct process_onefile *p = list_entry (e, struct process_onefile, elem);
      if (p->filed == fd && inf== p->fileof) {
        list_remove(e);
        file_close(p->fs);
        if(inf->cfiled==fd){
          inf->cfiled=-1;
          inf->cfs=NULL;
          // memcpy(inf->fname, "", strlen("")+1);
        }
        free(p);
        return 1;
      }
      // free(p);
  }
  // sema_up(&flist);
  // lock_release(&flock);
  return -1;
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  list_init(&process_info_list);
  list_init(&process_exit_list);
  list_init(&my_allfiles);
  fdc=3;
  // sema_init(&openlock,1);
  sema_init(&filesema,1);
  // sema_init(&psema,1);
  // sema_init(&flist,1);
  lock_init(&pil_lock);
  // lock_init(&flock);
  // lock_init(&syslock);
}

static void
syscall_handler (struct intr_frame *f)
{
  int* esp = f->esp;
 
  if ( !check_ptr(esp)) {
    exit(-1);
    return;
  }

  int number = *esp;
  if (number == SYS_HALT) {
    shutdown_power_off();
  }
  else if (number == 1) {
    if ( !check_ptr(esp+1) ) {
      exit(-1);
      return;
    }
    int exit_code = *(esp+1) ;
    exit(exit_code);
  }
  else if (number == SYS_WRITE) {
    write(f, esp);
  }
  else if (number == SYS_CREATE) {
    create(f, esp);
  }
  else if (number == SYS_OPEN) {
    open(f,esp);
  }
  else if (number == SYS_READ) {
    read(f, esp);
  }
  else if (number == SYS_WAIT) {
    wait(f, esp);
  }
  else if (number == SYS_EXEC) {
    exec(f, esp);
  }
  else if (number == SYS_FILESIZE) {
    filesize(f, esp);
  }
  else if (number == SYS_CLOSE) {
    close(f,esp);
  }
  else if (number == SYS_TELL) {
    tellme(f,esp);
  }
  else if (number == SYS_SEEK) {
    seeker(f,esp);
  }
  else if(number == SYS_REMOVE){
    removeit(f,esp);
  }

}

void exit(int exit_code) {
  set_process_exitcode(thread_current()->tid, exit_code);
  // struct process_info* pi = get_process_info(thread_current()->tid) ;
  struct process_info* pi = get_process_exit(thread_current()->tid) ;
  if(pi->exit_code==-1)
  free_res(pi);
  printf("%s: exit(%d)\n", pi->name , exit_code);
  thread_exit();
} 


void create(struct intr_frame *f, int* esp) {
  //check for valid pointers
  if ( !check_ptr(esp+1) || !check_ptr(esp+2) ) {
    exit(-1);
    return;
  }
  if (!check_ptr((void*)(*(esp + 1))) ){
    exit(-1);
    return;
  }

  char* buffer = *(esp + 1);
  unsigned int size = *(esp + 2);
  // if name is empty return false
  if (strlen(buffer) == 0) {
    f->eax = false;
    return;
  }
  else {
    // sema_down(&filesema);
    f->eax = filesys_create(buffer, size);
    // sema_up(&filesema);
  }
}

//delete file from disk not from process file list 
void removeit(struct intr_frame *f, int* esp){
  if ( !check_ptr(esp+1)  ) {
    exit(-1);
    return;
  }
  if (!check_ptr((void*)(*(esp + 1))) ){
    exit(-1);
    return;
  }

  char* fil = *(esp + 1);
  if (strlen(fil) == 0) {
    f->eax = -1;
    //free(fil);
    return;
  }
  else {
    // sema_down(&filesema);
    f->eax=filesys_remove(fil);
  // sema_up(&filesema);

  }
}

void open(struct intr_frame *f, int* esp) {
if ( !check_ptr(esp+1)  ) {
    exit(-1);
    return;
  }
  if (!check_ptr((void*)(*(esp + 1))) ){
    exit(-1);
    return;
  }

  char* fil = *(esp + 1);
  //if file name is empty return -1
  if (strlen(fil) == 0) {
    f->eax = -1;
    //free(fil);
    return;
  }
  else {
    // sema_down(&filesema);
    struct file * tem= filesys_open(fil);
    // sema_up(&filesema);
    if (tem!= NULL){
      // struct inode  *in= tem->inode;
      struct process_info *inf=get_process_info( thread_current()->tid);
      fdc++;
      // deny write if the file opened is the executable currently running
      // deny will end when the open file is closed.
      if(strstr(inf->name,fil)!=NULL)
      file_deny_write(tem);
      
      add_file_to_list(fdc,tem,inf);

      f->eax =fdc;
      //free(inf);
    }
    else{
      // sema_up(&filesema);
      f->eax =-1;
    }
    //free(fil);
    return;
  }

}

void close(struct intr_frame *f, int* esp){
  if ( !check_ptr(esp+1)  ) {
    exit(-1);
    return;
  }
  struct process_info *inf=get_process_info( thread_current()->tid);
  // struct process_info *inf=thread_current()->inf;
  // int cfd=inf->filed;
  int gfd=*(esp + 1);
  if(gfd<3 || gfd >inf->filed){

    return;
  }
  // also checks for twice close 
  if(delete_file_fr_list(gfd,inf)==-1){
    f->eax=-1;
    return;
  }
  
}

void write(struct intr_frame *f, int* esp) {
  if ( !check_ptr(esp+1) || !check_ptr(esp+2) || !check_ptr(esp+3) ) {
    exit(-1);
    return;
  }

  int fd = *(esp + 1);
  void* buffer = *(esp + 2);
  unsigned int len = *(esp + 3);
  struct process_info *inf=get_process_info( thread_current()->tid);
  // struct process_info *inf=thread_current()->inf;
  if (!check_ptr( buffer )){
    exit(-1);
    return;
  }
  //if length of given bytes is 0
  if(len<1){
    f->eax=0;
    return;
  }
  // exclude fd==1 since it will be used
  if(fd> inf->filed || fd<0)
    return;

  if (fd == STDIN_FILENO ) {
    exit(-1);
    return;
  }
  else if (fd == STDOUT_FILENO) {
    putbuf(buffer, len);
    f->eax = len;
  }
  else {

    if(fd==inf->cfiled){
      
      sema_down(&filesema);
      f->eax= file_write (inf->cfs, buffer, len) ;
      sema_up(&filesema);
    }
    else{
      
      sema_down(&filesema);
      struct process_onefile * ck=get_file_data(fd,inf);
      // lock_acquire(&syslock);
      f->eax= file_write (ck->fs, buffer, len) ;
      // lock_release(&syslock);
      sema_up(&filesema);
   }
  }
}

void read(struct intr_frame *f, int* esp) {
  // esp+1: fd, esp+2:buffer pointer esp+3:number of bytes to read
if ( !check_ptr(esp+1) || !check_ptr(esp+2) || !check_ptr(esp+3) ) {
    exit(-1);
    return;
  }
  int gfd=*(esp + 1);
  void* buffer = *(esp + 2);
  int btr=*(esp + 3);
  struct process_info *inf=get_process_info( thread_current()->tid);
  // struct process_info *inf=thread_current()->inf;
  if (!check_ptr( buffer )){
    exit(-1);
    return;
  }
  
  if (btr<1){
    f->eax=0;
    return;
  }
  //checking if incoming fd is within limit of thread fd
  
  if(gfd<3 || gfd >inf->filed){
    f->eax=-1;
    return;
  }

  if (inf->cfiled==gfd){
    sema_down(&filesema);
    int bread= file_read(inf->cfs,buffer,btr);
    sema_up(&filesema);
    f->eax=bread;
  }
  else{
    sema_down(&filesema);
    // lock_acquire(&syslock);
    int bread= file_read(get_file_data(gfd,inf)->fs,buffer,btr);
    // lock_release(&syslock);
    sema_up(&filesema);
    f->eax=bread;
  }
}

void exec(struct intr_frame *f, int* esp) {
if ( !check_ptr(esp+1)  ) {
    exit(-1);
    return;
  }
  //if valid buffer pointer is not received
  if ( !check_ptr((void*)(*(esp + 1))) ){
    exit(-1);
    return;
  }
  const char * cmd=*(esp+1);
  if( strlen(cmd)==0){
    f->eax=-1;
    return;
  }

  int ti=process_execute (cmd);
  if (ti==TID_ERROR){
    f->eax=-1;
    return;
  }
  struct process_info *pi=get_process_info(ti);
  sema_down(&pi->cont);
  // while(pi->execw!=1){
  //   pi=get_process_info(ti);
  // }
  if(pi->loaded==1)
    f->eax=ti;
  else
    f->eax=-1;
  //free(pi);
}

void wait(struct intr_frame *f, int* esp) {
  // int will be recevied at esp+1 so no need to check for pointer
if ( !check_ptr(esp+1)  ) {
    exit(-1);
    return;
  }
   tid_t t= *(esp+1);
  // now check for tid if valid or not
  if(t==-1){
    f->eax=-1;
    return;
  }
  // int status=;
  f->eax=process_wait(t);
}

void filesize(struct intr_frame *f, int* esp) {
  // since filsize only get fd arugnment which is int
if ( !check_ptr(esp+1)  ) {
    exit(-1);
    return;
  }
  int fd=*(esp+1);
  struct process_info *inf=get_process_info( thread_current()->tid);
  // struct process_info *inf=thread_current()->inf;
  if(fd==inf->cfiled){
    // sema_down(&filesema);
    f->eax= file_length (inf->cfs);
      // sema_up(&filesema);
    
  }
  else{
  struct process_onefile * ck= get_file_data(fd,inf);
    if(ck!=NULL){
      struct file * st=ck->fs;
      // sema_down(&filesema);
      f->eax= file_length (st);
      // sema_up(&filesema);
      // f->eax= file_length (st);
    }
  }
}

void tellme(struct intr_frame *f, int* esp){
  if ( !check_ptr(esp+1)  ) {
    exit(-1);
    return;
  }
  struct process_info *inf=get_process_info( thread_current()->tid);
  // struct process_info *inf=thread_current()->inf;
  // int cfd=;
  int gfd=*(esp + 1);
  if(gfd<3 || gfd >inf->filed){
    f->eax=-1;
    return;
  }
  if (inf->cfiled==gfd){
    f->eax=file_tell(inf->cfs);
  }
  else{
    // sema_down(&filesema);
    f->eax=file_tell(get_file_data(gfd,inf)->fs);
    // sema_up(&filesema);
  }
}

void seeker(struct intr_frame *f, int* esp){
  if ( !check_ptr(esp+1) ||!check_ptr(esp+2)  ) {
    exit(-1);
    return;
  }
  struct process_info *inf=get_process_info( thread_current()->tid);
  // struct process_info *inf=thread_current()->inf;
  // int cfd=;
  int gfd=*(esp + 1);
  int bseek=*(esp+2);
  if(gfd<3 || gfd >inf->filed){
    // f->eax=-1;
    return;
  }
  //if seek is given invlid arg
  if (bseek<0){
    return;
  }
  if(gfd==inf->cfiled){
    file_seek(inf->cfs,bseek);
  }
  else{
  // sema_down(&filesema);
  file_seek(get_file_data(gfd,inf)->fs,bseek);
  // sema_up(&filesema);
  }
}

