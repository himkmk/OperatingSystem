#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "weight.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
	p->nice_value = 20;
	p->weight = weight[20];
	p->runtime = 0;
	p->vruntime1 = 0;
	p->vruntime2 = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  cprintf("%p %p\n", _binary_initcode_start, _binary_initcode_size);
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
	np->runtime = np->parent->runtime;
	np->vruntime1 = np->parent->vruntime1;
	np->vruntime2 = np->parent->vruntime2;
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
int
delta_vruntime(int _delta_runtime, int _weight)
{
	//_delta_runtime is millitick
	return (_delta_runtime*1024)/_weight;
}

void
scheduler(void)
{
  	struct proc *p,*p1,*highp,*savehighp;
  	struct cpu *c = mycpu();
	int timeslice =0;
  	c->proc = 0;
	int totalweight=0;
	int delta_runtime=0;
	//int delta_runtime_initial=0;
  
	for(;;){
		// Enable interrupts on this processor.
		sti();

		// Loop over process table looking for process to run.
		acquire(&ptable.lock);
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){ //여기 loop 한번 돌때마다 context switching 일어남. 뒤에서 보정해주니까 여기선 어떤 p인지 상관ㄴㄴ.근데 이 루프가 진짜 필요한가..?
		  	
			if(p->state != RUNNABLE){continue;}
			highp = p;




			if(timeslice<=0){
				totalweight=1; //to refresh total weight
				for(p1 = ptable.proc; p1 < &ptable.proc[NPROC]; p1++){
					//choosing process with smallest vruntime
					if(p1->state == RUNNABLE || p1->state == RUNNING){
						totalweight+=p1->weight;
						if( (p1->vruntime1 < highp->vruntime1) || 
								((p1->vruntime1 == highp->vruntime1) && (p1->vruntime2 < highp->vruntime2)) ) {highp = p1;} // choose process complete! //사실 굳이 highp를 만들어야되나 싶네.
					}
				}
				
				savehighp=highp;
				//cprintf("----------->> selected pid: %d",highp->pid);
				timeslice = (10000*(p->weight))/totalweight; //10 * 1000 milli
				continue;
			}
			timeslice-=1000;




			p=savehighp;
			
			// Switch to chosen process.  It is the process's job
			// to release ptable.lock and then reacquire it
			// before jumping back to us.
			
			if(p->state !=RUNNABLE){continue;}
			delta_runtime = ticks; // saving time befor running process go to 6 lines below


			
			c->proc = p;
			switchuvm(p);
		  	p->state = RUNNING;
			swtch(&(c->scheduler), p->context);
			switchkvm(); 					// <--Process is done running for now. It should have changed its p->state before coming back.



			delta_runtime = ticks - delta_runtime; //this is the actual runtime in ticks metric
			p->vruntime2 += (delta_runtime*1024*1000)/p->weight;
			p->runtime += 1000*delta_runtime; //mult 1000 to change into milli ticks, then assign to p->runtime.
			
			//cprintf("pid %d -- rtim: %d --- vrtime: %d\n",p->pid,p->runtime,p->vruntime2);
			//considering vruntime overflow
			//   vruntime
			// = [ ----- vruntime1 ------- | -----vruntime2------- ]
			if(p->vruntime2>=1000000000){p->vruntime1 +=1;p->vruntime2-=1000000000;}
			
			
			

			//cprintf("pid:%d ---- add vruntime: %d ------- add runtime: %d\n",p->pid,(delta_runtime*1024*1000)/p->weight,1000*delta_runtime);
			//cprintf("nicevalue:%d ---- weight:%d\n\n\n",p->nice_value,p->weight);
			
			
			
			c->proc = 0;
			
		}
		
    	release(&ptable.lock);
  }

}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
	//panic zombie exit 를 막으려면 이 함수는 절대 끝나면 안되는건가..? exit()을 보면 그런듯..?
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
	struct proc *min_p;


  	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
	    	if(p->state == SLEEPING && p->chan == chan){
	      		p->state = RUNNABLE;

		min_p = ptable.proc;
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
			if(p->state != RUNNABLE && p->state != RUNNING){continue;}
			
			
			if((p->vruntime1 < min_p->vruntime1) || (   (p->vruntime1==min_p->vruntime1)&&(p->vruntime2 < min_p->vruntime2) ))
			{//if p-vruntime < minvp -vruntime
				min_p->vruntime1 = p->vruntime1;
				min_p->vruntime2 = p->vruntime2;
			}
		}
		//cprintf("wakeup123\n");
		//if min_p->vruntime is 1,000,000,000   (즉, vrtime1 = 1, vrtime2 = 0일때)
		if((min_p->vruntime1 == 1) && (min_p->vruntime2==0)){min_p->vruntime1 =0; min_p->vruntime2=999999999;}
		//vruntime!=0 이면 vrutime-=1
		else if( (min_p->vruntime1!=0) && (min_p->vruntime2!=0) ){min_p->vruntime2 -=1;}

		}
	}
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


int
csetnice(int pid, int value)
{
	struct proc *p;
	sti();
	acquire(&ptable.lock);
	

	if(value<0 || value >39){release(&ptable.lock);return -1;}
	for(p = ptable.proc; p< &ptable.proc[NPROC]; p++){
		if(p->pid == pid){//cprintf("set nice pid = %d  into nicevalue->%d\n",pid,value);
		p->nice_value = value;
		p->weight = weight[value];
		release(&ptable.lock);
		return 0;}
		//break;
	}
	
	//cprintf("set nice fail... pid=%d, value=%d\n",pid,value);
	release(&ptable.lock);
	return -1;

}

int
cgetnice(int pid)
{
	struct proc *p;
	sti();
	acquire(&ptable.lock);

	for(p = ptable.proc; p< &ptable.proc[NPROC]; p++){
		if(p->pid == pid){release(&ptable.lock);return p->nice_value;}
	}
	
	release(&ptable.lock);
	return -1;

}

//printing process state
int
cps(int pid)
{
	struct proc *p;
	//struct proc *p_this;	

	sti();
	acquire(&ptable.lock);
	
	//p_this = myproc();	
	//cprintf("\nthis pid is %d \n\n",p_this->pid); 

	if (pid==0){ //if pid is 0, print all proccess state
		cprintf("\033[2J\033[1;1H\n");
		cprintf("\nname\t\tpid\tstate\t\tpriority\truntime/weight\truntime\t\t\tvruntime\t\t\tticks %d\n",ticks*1000);
		//cprintf("how big? : %d -- %d,%d\n\n",&ptable.proc[640]-ptable.proc,ptable.proc,ptable.proc + 1); NPROC??sdfgsdgdf gdfg
		for(p = ptable.proc; p< &ptable.proc[NPROC]; p++){
			//cprintf("\n in for loop\n");
			
			if(p->vruntime1==0){
			
				if(p->state == SLEEPING)
					    // cprintf("name \t pid \t state \t\t priority \t runtime/weight \t runtime \t\t vruntime \t\t ticks %d\n",ticks);
					{cprintf("%s\t\t%d\tSLEEPING\t%d\t\t%d\t\t%d\t\t\t%d\t\t\n",
					p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime2);}
				else if(p->state == RUNNING)
					{cprintf("%s\t\t%d\tRUNNING \t%d\t\t%d\t\t%d\t\t\t%d\t\t\n",
					p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime2);}
				else if(p->state == RUNNABLE)
					{cprintf("%s\t\t%d\tRUNNABLE\t%d\t\t%d\t\t%d\t\t\t%d\t\t\n",
					p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime2);}
				else if(p->state == EMBRYO)
					{cprintf("%s\t\t%d\tEMBRYO  \t%d\t\t%d\t\t%d\t\t\t%d\t\t\n",
					p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime2);}
				else if(p->state == ZOMBIE)
					{cprintf("%s\t\t%d\tZOMBIE  \t%d\t\t%d\t\t%d\t\t\t%d\t\t\n",
					p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime2);}			
			}
			else{	
				
					if(p->state == SLEEPING)
					  // cprintf("name \t pid \t state \t\t priority \t runtime/weight \t runtime \t\t vruntime \t\t ticks %d\n",ticks);
					{cprintf("%s\t\t%d\tSLEEPING\t%d\t\t%d\t\t%d\t\t\t%d%d\t\t\n",
					p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime1,p->vruntime2);}
				else if(p->state == RUNNING)
					{cprintf("%s\t\t%d\tRUNNING \t%d\t\t%d\t\t%d\t\t\t%d%d\t\t\n",
					p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime1,p->vruntime2);}
				else if(p->state == RUNNABLE)
					{cprintf("%s\t\t%d\tRUNNABLE\t%d\t\t%d\t\t%d\t\t\t%d%d\t\t\n",
					p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime1,p->vruntime2);}
				else if(p->state == EMBRYO)
					{cprintf("%s\t\t%d\tEMBRYO  \t%d\t\t%d\t\t%d\t\t\t%d%d\t\t\n",
					p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime1,p->vruntime2);}
				else if(p->state == ZOMBIE)
					{cprintf("%s\t\t%d\tZOMBIE  \t%d\t\t%d\t\t%d\t\t\t%d%d\t\t\n",
					p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime1,p->vruntime2);}			
			}
	
		}
		cprintf("\n");
	}
	
	
	else{  //if pid is not 0, print only the specific process
		cprintf("name\t\tpid\tstate\t\tpriority\truntime/weight\truntime\t\t\tvruntime\t\t\tticks %d\n",ticks*1000);
		for(p = ptable.proc; p< &ptable.proc[NPROC]; p++){
			if(p->pid == pid){
			
			
			
			
				if(p->vruntime1 ==0){
					if(p->state == SLEEPING)
						  // cprintf("name \t pid \t state \t\t priority \t runtime/weight \t runtime \t\t vruntime \t\t ticks %d\n",ticks);
						{cprintf("%s\t\t%d\tSLEEPING\t%d\t\t%d\t\t%d\t\t\t%d\t\t\n",
						p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime2);}
					else if(p->state == RUNNING)
						{cprintf("%s\t\t%d\tRUNNING \t%d\t\t%d\t\t%d\t\t\t%d\t\t\n",
						p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime2);}
					else if(p->state == RUNNABLE)
						{cprintf("%s\t\t%d\tRUNNABLE\t%d\t\t%d\t\t%d\t\t\t%d\t\t\n",
						p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime2);}
					else if(p->state == EMBRYO)
						{cprintf("%s\t\t%d\tEMBRYO  \t%d\t\t%d\t\t%d\t\t\t%d\t\t\n",
						p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime2);}
					else if(p->state == ZOMBIE)
						{cprintf("%s\t\t%d\tZOMBIE  \t%d\t\t%d\t\t%d\t\t\t%d\t\t\n",
						p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime2);}			
				}	
					
					
					
				else{
					if(p->state == SLEEPING)
						  // cprintf("name \t pid \t state \t\t priority \t runtime/weight \t runtime \t\t vruntime \t\t ticks %d\n",ticks);
						{cprintf("%s\t\t%d\tSLEEPING\t%d\t\t%d\t\t%d\t\t\t%d%d\t\t\n",
						p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime1,p->vruntime2);}
					else if(p->state == RUNNING)
						{cprintf("%s\t\t%d\tRUNNING \t%d\t\t%d\t\t%d\t\t\t%d%d\t\t\n",
						p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime1,p->vruntime2);}
					else if(p->state == RUNNABLE)
						{cprintf("%s\t\t%d\tRUNNABLE\t%d\t\t%d\t\t%d\t\t\t%d%d\t\t\n",
						p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime1,p->vruntime2);}
					else if(p->state == EMBRYO)
						{cprintf("%s\t\t%d\tEMBRYO  \t%d\t\t%d\t\t%d\t\t\t%d%d\t\t\n",
						p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime1,p->vruntime2);}
					else if(p->state == ZOMBIE)
						{cprintf("%s\t\t%d\tZOMBIE  \t%d\t\t%d\t\t%d\t\t\t%d%d\t\t\n",
						p->name,p->pid,p->nice_value,(p->runtime)/p->weight,p->runtime,p->vruntime1,p->vruntime2);}			
				}	
					
					
					
					
					
					
			}
			//else
			//	{cprintf("DONT KNOW STATE --> %s \n",p->name);}
		}
		cprintf("\n");
	}

	release(&ptable.lock);
	return 888;
}
