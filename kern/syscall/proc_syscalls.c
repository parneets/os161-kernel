#include <types.h>
#include <limits.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <kern/fcntl.h> // O_RDONLY
#include <vfs.h> // 
#include <lib.h>
#include <syscall.h>
#include <mips/trapframe.h> // 
#include <current.h>
#include <proc.h>
#include <synch.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include "opt-A2.h"

/* this implementation of sys__exit does not do anything with the exit code */
/* this needs to be fixed to get exit() and waitpid() working properly */

/*
* the exiting process's children processes:
* recycle pid of exited children proc

* the exiting process's parent process:
* if the parent proc exited or no parent proc,
* if the parent proc is running,
*/
void sys__exit(int exitcode) {

#if OPT_A2
  DEBUG(DB_SYSCALL,"sys_exit(): exitcode - %d\n",exitcode);
  

  struct addrspace *as;
  struct proc *p = curproc;
  

  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  // (void)exitcode;

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  

  // set_exitcode(p, exitcode);

  p -> p_exitcode = _MKWAIT_EXIT(exitcode);
  p -> exitable = true; 

  lock_acquire(p->p_waitpid_lk);
      cv_broadcast(p->p_waitpid_cv, p->p_waitpid_lk);
  lock_release(p->p_waitpid_lk);

  // detach children & parent relationships
  // determin if recycle w.r.t. parent proc existence
  // record exitcode in process table if has parent 
  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);

  thread_exit();
  /* thread_exit() does not return */
  panic("sys_exit: unexpected return from thread_exit() \n");
#endif
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
#if OPT_A2
  KASSERT(curproc != NULL);
  *retval = curproc->p_pid;
#else
  *retval = 1;
#endif

  return(0);
}

/* stub handler for waitpid() system call                */
/*
* parent must receive the child's exit code
*/
int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.       
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }

#if OPT_A2
  struct proc *p = curproc;

  /**  step 1: check restriction: pid should be one of curproc's children **/
  bool valid_wait = if_procchild(p, pid);
  if (valid_wait == false)
  {
    DEBUG(DB_SYSCALL, "process %p wants to get exitcode of non-child process [pid = %d]\n", p, pid);
    kprintf ("proc[pid = %d] wants to get exitcode of non-child proc[pid = %d]\n", p->p_pid, pid);
    return(ECHILD);
  }

  struct proc *waitproc = proc_get_by_pid(pid);
  /**  step 2: check pid proc to see if it exited, and block if it is running **/
  lock_acquire(waitproc->p_waitpid_lk); 
    while (waitproc->exitable == false)
      {
        DEBUG(DB_SYSCALL,"Syscall: _waitpid() is blocked to run process %d\n",pid);
        cv_wait(waitproc->p_waitpid_cv, waitproc->p_waitpid_lk);
      }
  lock_release(waitproc->p_waitpid_lk);

  exitstatus = waitproc->p_exitcode;
#else
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
#endif

  result = copyout((void *)&exitstatus,status,sizeof(int)); // return 0 if succeeds
  if (result) {
    return(result);
  }
  
  *retval = pid;
  return(0);
}

/*
* ref: http://ada.evergreen.edu/sos/os14w/labs/forkexecTutorial.pdf
*/
#if OPT_A2
int
sys_fork(struct trapframe *tf, pid_t *retval)
{
    KASSERT(curproc != NULL);
    
    /* The trap frame is supposed to be 37 registers long. */
    KASSERT(sizeof(struct trapframe)==(37*4));

    char *forkname = kmalloc(sizeof(char) * NAME_MAX);
    strcpy(forkname, curproc->p_name); 
    strcat(forkname, "_forked");

    //STEP 1: Create process structure for child process
    struct proc *childproc = proc_create_runprogram(forkname);
    KASSERT (childproc->p_pid > 0);

    // when proc_create_runprogram returns NULL, 
    if (childproc == NULL) {
        kfree(forkname);
        return ENOMEM;
    }

    // STEP 2: Create and copy(later) address space and register states(tf) from parent to child
    /* use kmalloc to create address space on the heap rather than stack
     *  or it will be lost when the function returns.*/
    struct addrspace *childas = kmalloc(sizeof(struct addrspace));
    if (childas == NULL) {
        kfree(forkname);
        proc_destroy(childproc);
        return ENOMEM;
    }

    struct trapframe *childtf = kmalloc(sizeof(struct trapframe));
    if (childtf == NULL) {
        kfree(forkname);
        kfree(childtf);
        as_destroy(childas);
        proc_destroy(childproc);
        return ENOMEM;
    }

    // STEP 3: Attach the newly created address space to the child process structure
    // as_copy will allocate a struct addrspace 
    // and also copy the address space contents 
    int errno = as_copy(curproc->p_addrspace, &childas);
    if (errno) {
        kfree(forkname);
        kfree(childas);
        proc_destroy(childproc);
        return errno;
    }
    childproc->p_addrspace = childas;
    // deep copy
    memcpy(childtf, tf, sizeof(struct trapframe));
     
    // STEP 4: Assign PID to child process（done） and create the parent/child relationship
    childproc->p_pproc = curproc;

    int result = procarray_add(&curproc->p_children, childproc, NULL);
    if (result){
      DEBUG(DB_SYSCALL, "procarray_add failed\n");
      kprintf("procarray_add failed\n");
    }

    // STEP 5: Create thread for child process and let it be runnable in user space
    void **data = kmalloc(2*sizeof(void *));
    data[0] = (void *)childtf;
    data[1] = (void *)childas;

    errno = thread_fork(forkname, childproc, &enter_forked_process, data, 0);
    if (errno) {
        kfree(forkname);
        kfree(childtf);
        as_destroy(childas);
        proc_destroy(childproc);
        return ENOMEM;
    }

    // return value should be the process ID of the child process
    // and return 0 in parent proc
    *retval = childproc->p_pid;

    return(0);
}
/*
* ref: http://ada.evergreen.edu/sos/os14w/labs/forkexecTutorial.pdf
*/
int
sys_execv(const_userptr_t path, userptr_t argv){
    int argc, result, i, offset;
    size_t path_len;
    struct addrspace *old_as;
    struct vnode *v; //an abstract representation of a file.
    vaddr_t entrypoint, stackptr, argvptr;
    /* 
    * step 1: copy program path to kernel addr space
    */
    char *kpath = kmalloc(PATH_MAX);
    if (kpath == NULL)
        return ENOMEM;

    result = copyinstr(path, kpath, PATH_MAX, &path_len);
    if (result) {
        return result;
    }
    if (path_len == 1){
        //path is null
        return ENOENT;
    }
    /*
    * step 2: copy program argv to kernel addr space
    */
    char** argv_ptr = (char**) argv;
    // Find out the number of arguments (argc). 
    for (argc = 0; argv_ptr[argc] != NULL; ++argc);
    if (argc > ARG_MAX)
        return E2BIG;

    // argv is array of ptrs, so first copyin the user-level addr,
    // then copyinstr the strings that the ptr refering to.
    userptr_t k_ptr = kmalloc(sizeof(userptr_t));
    if (k_ptr == NULL)
        return ENOMEM;

    int arg_size = (argc+1) * sizeof(char*); // don't forget + 1
    char** kargv = kmalloc(arg_size);
    if (kargv == NULL)
        return ENOMEM;

    for (i = 0; i<argc; ++i){
        result = copyin((const_userptr_t)&(argv_ptr[i]), &k_ptr, sizeof(userptr_t));  //copy addr, so &(argv_ptr[i])
        if (result){
          return ENOMEM;
        }
        kargv[i] = kmalloc(PATH_MAX);
        result = copyinstr((const_userptr_t)k_ptr, kargv[i], PATH_MAX, &path_len);
        if (result) {
          return result;
        }
    }
    
    // argument array should end with NULL.
    kargv[argc] = NULL;                                       

   
    /* Open the file. */
    char *fname_temp;
    fname_temp = kstrdup(kpath);
    result = vfs_open(fname_temp, O_RDONLY, 0, &v);             // ???? 
    kfree(fname_temp);
    if (result) {
      return result;
    }

    // Replace old address space &
    // Create a new address space.
    
    KASSERT(curproc_getas() != NULL);
    old_as = curproc_getas();
    spinlock_acquire(&curproc->p_lock);
      curproc->p_addrspace = as_create();
    spinlock_release(&curproc->p_lock);
    
    if (curproc->p_addrspace ==NULL) {
      vfs_close(v);
      return ENOMEM;
    }

    /* activate new as. */
    as_activate();

    /* Load the executable. */
    result = load_elf(v, &entrypoint);
    if (result) {
      /* p_addrspace will go away when curproc is destroyed */
      vfs_close(v);
      return result;
    }

    /* Done with the file now. */
    vfs_close(v);

    /* Define the user stack in the address space */
    // stackptr is the very top of the user address space
    result = as_define_stack(curproc->p_addrspace, &stackptr);
    if (result) {
      /* p_addrspace will go away when curproc is destroyed */
      return result;
    }
    
    // copy argv onto the stack of user address space
    char** addr_ptr = kmalloc((argc+1)*sizeof(char*)); // sizeof(char*) = 4 bytes
      if (addr_ptr == NULL)
        return ENOMEM;

    for (i=argc-1; i>=0; --i){
        char* arg_str = kargv[i];
        int length = strlen(arg_str)+1;
        stackptr-=length;
        result = copyout(arg_str, (userptr_t)stackptr, length);     // assume it automatically fill 0
        if (result) {
          /* p_addrspace will go away when curproc is destroyed */
          return result;
        }
        addr_ptr[i] = (char*)stackptr;
    }
    addr_ptr[argc] = NULL;

    offset = stackptr%4;
    stackptr-=stackptr%4;
    bzero((void *)stackptr, offset);

    offset = (argc+1)*sizeof(char*);
    stackptr-=offset;
    result = copyout(addr_ptr, (userptr_t)stackptr, offset);     // assume it automatically fill 0
    if (result) {
      /* p_addrspace will go away when curproc is destroyed */
      return result;
    }
    argvptr = stackptr;

    offset = stackptr%8;
    stackptr-=stackptr%8;
    bzero((void *)stackptr, offset);

    // clean variables
    as_destroy(old_as);    
    kfree(kargv);
    kfree(kpath);
    kfree(addr_ptr);
    // kfree(k_ptr);
    // kfree(argv_ptr);

    /* Warp to user mode. */
    enter_new_process(argc /*argc*/, (userptr_t)argvptr /*userspace addr of argv*/,
         stackptr, entrypoint);
    
    /* enter_new_process does not return. */
    panic("enter_new_process returned\n");
    return EINVAL;
}
#endif
