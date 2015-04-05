#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <thread.h>
#include <current.h>

/* 
 * This simple default synchronization mechanism allows only creature at a time to
 * eat.   The globalCatMouseSem is used as a a lock.   We use a semaphore
 * rather than a lock so that this code will work even before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
// static struct semaphore *mouseSem;
// static struct semaphore *catSem;
static struct cv *nomouse;
static struct cv *nocat;
static volatile int eat_cat_num = 0;
static volatile int eat_mouse_num = 0;

static struct lock *globalCatMouseLk;
// static struct lock *globalBowlLk;
static volatile char *bowl_arr;

static struct lock **bowl_lk_arr;
static struct cv **bowl_cv_arr;

/* functions defined and used internally */
static bool is_cat_eating(void){
  int i = 0;
  while (bowl_arr[i]) {
    if (bowl_arr[i] == 'c'){
      // kprintf("bowl[%d] is occupied by cat\n", i+1);
      return true;
    }
    i++;
  }
  return false;
}

static bool is_mouse_eating(void){
  int i = 0;
  while (bowl_arr[i]) {
    if (bowl_arr[i] == 'm'){
      // kprintf("bowl[%d] is occupied by mouse\n", i+1);
      return true;
    }
    i++;
  }
  return false;
}

// static void print_bowl(void){
//   int i = 0;
//   while (bowl_arr[i]) {
//     kprintf("%c \t",bowl_arr[i]);
//     i++;
//   }
//   kprintf("\n");
// }

/* 
 * The CatMouse simulation will call this function once before any cat or
 * mouse tries to each.
 *
 * You can use it to initialize synchronization and other variables.
 * 
 * parameters: the number of bowls
 */
void
catmouse_sync_init(int bowls)
{
  /* replace this default implementation with your own implementation of catmouse_sync_init */

  (void)bowls; /* keep the compiler from complaining about unused parameters */
  bowl_arr = kmalloc(bowls*sizeof(char));
  /* initialize bowls */
  for (int i = 0; i < bowls; i++){
    bowl_arr[i] = '-';
  }
  // print_bowl();

  bowl_cv_arr = kmalloc(bowls*sizeof(struct cv));
  for (int i = 0; i < bowls; i++){
    bowl_cv_arr[i] = kmalloc(sizeof(struct cv));
    bowl_cv_arr[i] = cv_create("bowl_cv");
    if (bowl_cv_arr[i] == NULL) {
      panic("could not create bowl cv %d", i);
    }
  }

  bowl_lk_arr = kmalloc(bowls*sizeof(struct lock));
  for (int i = 0; i < bowls; i++){
    bowl_lk_arr[i] = kmalloc(sizeof(struct lock));
    bowl_lk_arr[i] = lock_create("bowl_lock");
    if (bowl_lk_arr[i] == NULL) {
      panic("could not create bowl lock %d", i);
    }
  }

  globalCatMouseLk = lock_create("globalCatMouseLk");
  if (globalCatMouseLk == NULL) {
    panic("could not create global status synchronization lock");
  }

  // globalBowlLk = lock_create("globalBowlLk");
  // if (globalBowlLk == NULL) {
  //   panic("could not create global bowl status synchronization lock");
  // }

  nomouse = cv_create("nomouse");
  if (nomouse == NULL) {
    panic("could not create nomouse cv");
  }
  nocat = cv_create("nocat");
  if (nocat == NULL) {
    panic("could not create nocat cv");
  }

  // kprintf("catmouse_sync_init\n");

  return;
}

/* 
 * The CatMouse simulation will call this function once after all cat
 * and mouse simulations are finised.
 *
 * You can use it to clean up any synchronization and other variables.
 *
 * parameters: the number of bowls
 */
void
catmouse_sync_cleanup(int bowls)
{
  /* replace this default implementation with your own implementation of catmouse_sync_cleanup */
  (void)bowls; /* keep the compiler from complaining about unused parameters */
  // KASSERT(globalCatMouseSem != NULL);

  if (bowl_arr != NULL) {
    kfree( (void *) bowl_arr );
    bowl_arr = NULL;
  }

  KASSERT(globalCatMouseLk != NULL);
  lock_destroy(globalCatMouseLk);

  // KASSERT(globalBowlLk != NULL);
  // lock_destroy(globalBowlLk);

  if (bowl_cv_arr != NULL) {
    kfree( (void *) bowl_cv_arr );
    bowl_cv_arr = NULL;
  }

  if (bowl_lk_arr != NULL) {
    kfree( (void *) bowl_lk_arr );
    bowl_lk_arr = NULL;
  }

  KASSERT(nomouse != NULL);
  cv_destroy(nomouse);

  KASSERT(nocat != NULL);
  cv_destroy(nocat);

  // kprintf("catmouse_sync_cleanup\n");
}


/*
 * The CatMouse simulation will call this function each time a cat wants
 * to eat, before it eats.
 * This function should cause the calling thread (a cat simulation thread)
 * to block until it is OK for a cat to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the cat is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_before_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of cat_before_eating */
  (void)bowl;  /* keep the compiler from complaining about an unused parameter */
  
  lock_acquire(globalCatMouseLk);
    // step 1: determine if there is any mouse eating
    while(is_mouse_eating()){
        // kprintf("%s waits because mouse eating\n", curthread->t_name);
        cv_wait(nomouse, globalCatMouseLk);
      }
  lock_release(globalCatMouseLk);

  lock_acquire(bowl_lk_arr[bowl-1]);
    // step 2: determine if there is any cat eating the same bowl
    while(bowl_arr[bowl-1] != '-' || is_mouse_eating()){
      // kprintf("%s waits to get bowl[%d]\n", curthread->t_name, bowl);
      cv_wait(bowl_cv_arr[bowl-1], bowl_lk_arr[bowl-1]);
    }
    // step 3: sync bowl state, ready to eat
    bowl_arr[bowl-1] = 'c';
    // kprintf("%s is going to eat bowl[%d]\n", curthread->t_name, bowl);
  lock_release(bowl_lk_arr[bowl-1]);
}

/*
 * The CatMouse simulation will call this function each time a cat finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this cat finished.
 *
 * parameter: the number of the bowl at which the cat is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_after_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of cat_after_eating */
  (void)bowl;  /* keep the compiler from complaining about an unused parameter */
  
  lock_acquire(bowl_lk_arr[bowl-1]);
    // kprintf("%s fininshes eating bowl[%d]\n", curthread->t_name, bowl);
    
    // step 1: sync bowl state, ready for new animal to eat
    bowl_arr[bowl-1] = '-';
    cv_broadcast(bowl_cv_arr[bowl-1], bowl_lk_arr[bowl-1]);
  lock_release(bowl_lk_arr[bowl-1]);

  lock_acquire(globalCatMouseLk);
    // step 2: signal mice cats leave
  if (!is_cat_eating())
    cv_broadcast(nocat, globalCatMouseLk);
  lock_release(globalCatMouseLk);

}

/*
 * The CatMouse simulation will call this function each time a mouse wants
 * to eat, before it eats.
 * This function should cause the calling thread (a mouse simulation thread)
 * to block until it is OK for a mouse to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the mouse is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_before_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of mouse_before_eating */
  (void)bowl;  /* keep the compiler from complaining about an unused parameter */
  
  lock_acquire(globalCatMouseLk);
    // step 1: determine if there is any mouse eating
    while(is_cat_eating()){
        // kprintf("%s waits because cat eating\n", curthread->t_name);
        cv_wait(nocat, globalCatMouseLk);
      }
  lock_release(globalCatMouseLk);

  
  lock_acquire(bowl_lk_arr[bowl-1]);
    // step 2: determine if there is any mouse eating the same bowl
    while(bowl_arr[bowl-1] != '-' || is_cat_eating()){
      // kprintf("%s waits to get bowl[%d]\n", curthread->t_name, bowl);
      cv_wait(bowl_cv_arr[bowl-1], bowl_lk_arr[bowl-1]);
    }
    // step 3: sync bowl state, ready to eat
    bowl_arr[bowl-1] = 'm';
    // kprintf("%s is going to eat bowl[%d]\n", curthread->t_name, bowl);
  lock_release(bowl_lk_arr[bowl-1]);

}

/*
 * The CatMouse simulation will call this function each time a mouse finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this mouse finished.
 *
 * parameter: the number of the bowl at which the mouse is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_after_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of cat_after_eating */
  (void)bowl;  /* keep the compiler from complaining about an unused parameter */
  
  lock_acquire(bowl_lk_arr[bowl-1]);
    // kprintf("%s fininshes eating bowl[%d]\n", curthread->t_name, bowl);
    // step 1: sync bowl state, ready for new animal to eat
    bowl_arr[bowl-1] = '-';
    cv_broadcast(bowl_cv_arr[bowl-1], bowl_lk_arr[bowl-1]);
  lock_release(bowl_lk_arr[bowl-1]);

  lock_acquire(globalCatMouseLk);
      // step 2: signal cats mice leave
    if (!is_mouse_eating())
      cv_broadcast(nomouse, globalCatMouseLk);
  lock_release(globalCatMouseLk);
  

}
