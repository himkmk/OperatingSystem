#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

/////////////////////////FREELISTNUM
int FREELISTNUM =0;

void call_freelist(int a)
{
	FREELISTNUM+=a;
}
/////////////////////////mmap_area definition
struct mmap_area{
	struct file *f;
	uint addr;
	int length;
	int offset;
	int prot;
	int flags;
	struct proc *p;
};

struct mmap_area mmap_area_list[64];
int MMAP_FIRST_RUN = 1;

void mmap_area_init()
{
	for(int i=0;i<64;i++){
		mmap_area_list[i].addr=111;//addr=111 means its free
		mmap_area_list[i].length=0;
		mmap_area_list[i].f=0;
	}
}

int check_free(uint address)
{
	for(int i=0;i<64;i++){
		if(mmap_area_list[i].addr!= 111){
			if(mmap_area_list[i].addr <= address && address <= mmap_area_list[i].addr+mmap_area_list[i].length){
			 return mmap_area_list[i].addr;
			}
		}
	}
	
	return 1;
}

int check_free2(uint address)
{
	for(int i=0;i<64;i++){
		if(mmap_area_list[i].addr!= 111){
			if(mmap_area_list[i].addr==address){
			 return 0;
			}
		}
	}
	
	return 1;
}

void set_mmap_area(int fd, uint addr, int length, int offset, int prot, int flags, struct proc *p)
{

	struct file *f = 0;
	if (fd==-1){f=0;}
	else f = p->ofile[fd];
	

	for(int i=0;i<64;i++){
		if(mmap_area_list[i].addr==111){
			
			mmap_area_list[i].f = f;
			mmap_area_list[i].addr = addr;
			mmap_area_list[i].length = length;
			mmap_area_list[i].offset = offset;
			mmap_area_list[i].prot = prot;
			mmap_area_list[i].flags = flags;
			mmap_area_list[i].p = p;	
			
			return;
		}
	}
}

void free_mmap_area(uint addr)
{
	for(int i=0;i<64;i++){
		if(mmap_area_list[i].addr==addr){
			mmap_area_list[i].addr=111;
			mmap_area_list[i].length=0;
			return;
		}
	}
}

struct proc * get_proc(uint addr)
{
	for(int i=0;i<64;i++){
		if(mmap_area_list[i].addr==addr){
			return mmap_area_list[i].p;
		}
	}
	
	return 0;
}



int get_length(uint addr)
{
	for(int i=0;i<64;i++){
		if(mmap_area_list[i].addr==addr){
			return mmap_area_list[i].length;
		}
	}
	
	return -1;
}

int get_flags(uint addr)
{
	for(int i=0;i<64;i++){
		if(mmap_area_list[i].addr==addr){
			return mmap_area_list[i].flags;
		}
	}
	
	return -1;
}

int get_offset(uint addr)
{
	for(int i=0;i<64;i++){
		if(mmap_area_list[i].addr==addr){
			return mmap_area_list[i].offset;
		}
	}
	
	return -1;
}


struct file* get_file(uint addr)
{
	for(int i=0;i<64;i++){
		if(mmap_area_list[i].addr==addr){
			return mmap_area_list[i].f;
		}
	}
	
	return 0;
}
////////////////////until here

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
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
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

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
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


int mmap(uint addr,int length,int prot,int flags,int fd,int offset)
{
//cprintf("MMAP CALLED!\n");
//필요한 함수들...
	/*
	
	kalloc() -> 그냥 페이지 하나 자동으로 할당해주고 그 부분의 가상주소 리턴.
	PGROUNDUP((uint)vstart) 이걸로 페이지 단위로 반올림
	kfree 써야 fd안주어졌을떄 하는거 하는듯. 아니다 freerange쓰는건가..? kalloc가보자
	mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
	readi(struct inode *ip, char *dst, uint off, uint n) 
	writei(struct inode *ip, char *src, uint off, uint n)   

	*/
	
//변수들 선언

	
	struct proc *curproc = myproc();
	char* addr_tmp=0;
	pde_t *pagedir = curproc->pgdir;
	
	int ANONYMOUS=0;
	int AVAILABLE=0;
	if(MMAP_FIRST_RUN){
		mmap_area_init();
		MMAP_FIRST_RUN=0;
	}
//예외처리	
	//addr	
	//addr=0인경우 처리했음.
	
	
	//length
	length=PGROUNDUP((uint)length); //page align맞추는용도
	//prot
	
	//flags
	if(flags&MAP_FIXED && check_free(addr+MMAP_BASE)!=1) {
		cprintf("MMAP with MAP_FIXED fail...memory not available...\n");
		return 0;
	}

	//fd
	if((fd==-1) && ((flags&MAP_ANONYMOUS)!=0)) ANONYMOUS=1; 
	else if((fd==-1) || ((flags&MAP_ANONYMOUS)!=0))return 0;
	
	//offset



//구현부             //perm -> PTE_P, PTE_W, PTE_U 이런거 말하는듯 PTE_P는 내가 안넣는것같은디 아래서 알아서 넣어주네.
	
	//주소 찾는중
	if(addr==0)
	{//cprintf("inside if--\n");
		for (int i=MMAP_BASE;i<MMAP_BASE+PGSIZE*1024;i+=PGSIZE){//페이지전체를돌며
			//cprintf("inside i loop--\n");
			for (int j=0;j<length;j+=PGSIZE){ //파일 사이즈(PGROUNDUP(curproc->ofile[fd]->ip->size)만한 공간이 있나 찾는거임. ㄴㄴ 길이줬자너 그거 써야지
				//cprintf("inside j loop--\n");
				if(check_free((uint)(i+j))!=1){
					//cprintf("PAGE NOT AVAILABLE!!!!!!!!!!!!!!!!!!!!!!\n\n"); //만약 문제가 있으면 다시 i 루프로 돌아가서 또 찾자 이거지.
					i+= (j-PGSIZE > 0 ? j-PGSIZE : 0);//그떄까지 모든 i에 대해 불가능하니까 쩜프!
					AVAILABLE=0;
					break;
				}
				AVAILABLE=1;				
			}
			if(AVAILABLE==1){addr = i;break;} //만약 AVAILABLE==1상태로 j루프를 탈출했다면 충분한 사이즈가 있는것. 그래서 현재 i값으로 addr지정.
		}
	}
	else{addr+=MMAP_BASE;}
	//void set_mmap_area(struct file *f, uint addr, int length, int offset, int prot, int flags, struct proc *p)
	set_mmap_area(fd,(uint)addr,length,offset,prot,flags,curproc);
	//cprintf("addr---------> %p\n\n",addr);
	
	
	//페이지 할당중...
	for(int i=addr; i<addr+length; i+=PGSIZE/*4096*/){ //이거 돌면서도 문제생기면 튕기게 해야지.
		if(!(flags&MAP_POPULATE)) return (int)addr;;
		addr_tmp = kalloc();//물리 메모리 할당 됐고, 해당하는 가상주소를 리턴값으로 받았지.
		if(extern_mappages(pagedir,(void *)i,PGSIZE,V2P(addr_tmp),PTE_U|PTE_W|PTE_P)!=0){cprintf("mappages fail!\n");return 0;} //flag가 그 인풋으로 받은 flag가 맞나?
		//cprintf("extern_mappages running!\n");
	}

	//페이지에 파일 올리는중...
	call_freelist(-(length)/PGSIZE);
	if(!ANONYMOUS){
		//cprintf("NON ANONYMOUS\n");
		acquiresleep(&(curproc->ofile[fd]->ip)->lock);
		readi(curproc->ofile[fd]->ip,(char*)addr,offset,length);
		releasesleep(&(curproc->ofile[fd]->ip)->lock);}
	else{//cprintf("ANONYMOUS!\n");
		for (int i = 0; i < length; i++) 
		*((char*)addr+i)=0;
	}
	//cprintf("%s",(char*)addr);
	//cprintf("inside proc.c / mmap \n");
	return (int)addr;
}

int munmap(uint addr)
{
	if (check_free2(addr)) return -1;
	call_freelist(((get_length(addr))/PGSIZE));
	free_mmap_area(addr);
	//cprintf("111\n");
	
	for(int i=addr;i<addr+get_length(addr);i+=PGSIZE){
		kfree((char*)i);
		call_freelist(-1);
	}
	
	//cprintf("111\n");
	
	return 1;
}

int freemem()
{
	int tmp=FREELISTNUM;
	return tmp;
}

int pgflt(uint addr)
{

	struct proc *proc = get_proc(addr);
		int flags = get_flags(addr); flags+=1;flags-=1;
		int offset = get_offset(addr);
		int length = get_length(addr);
		struct file* file = get_file(addr);
		pde_t *pagedir = proc->pgdir;
		
		char* addr_tmp=kalloc();
	//애초에 va도 할당이 안된 경우. 혹은 되었지만 첫번째 페이지 넘어간 경우.
		if(check_free2(addr)){
			

			if(check_free(addr)!=1){

				call_freelist(-1);
				extern_mappages(get_proc(check_free(addr))->pgdir,(void *)addr,PGSIZE,V2P(addr_tmp),PTE_U|PTE_W|PTE_P);
				if(get_file(check_free(addr))==0){
					//ANONYMOUS
					for (int i = addr; i < addr+PGSIZE; i++) 
						*((char*)addr+i)=0;
				}
				else{
					//NON ANONYMOUS
					acquiresleep(&(get_file(check_free(addr))->ip)->lock);
					readi(get_file(check_free(addr))->ip,(char*)addr,offset+PGSIZE,PGSIZE);
					releasesleep(&(get_file(check_free(addr))->ip)->lock);
				}
				return 1;

			}



			call_freelist(-1);
			extern_mappages(myproc()->pgdir,(void *)addr,PGSIZE,V2P(addr_tmp),PTE_U|PTE_W|PTE_P);
			for (int i = 0; i < PGSIZE; i++) 
					*((char*)addr+i)=0;
			return -1;
		}
		

		//va가 할당된 첫페이지이지만, 물리 메모리 매핑 안해준 경우.

			call_freelist(-1);
		/* 1 */ extern_mappages(pagedir,(void *)addr,PGSIZE,V2P(addr_tmp),PTE_U|PTE_W|PTE_P);
		
		/* 2 */ 
				if( file!=0 ){
					acquiresleep(&(file->ip)->lock);
					readi(file->ip,(char*)addr,offset,PGSIZE);
					releasesleep(&(file->ip)->lock);}
					
				else{
					for (int i = 0; i < length; i++) 
					*((char*)addr+i)=0;
				}
	

		//cprintf("exit fault");
		return 1;




	return 1;
}
