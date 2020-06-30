// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"

#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "x86.h"
#include "proc.h"



int INIT_LRULIST = 1;
void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;


struct spinlock bitmaplock;

struct page pages[PHYSTOP/PGSIZE];
struct page *page_lru_head;
int num_free_pages;
int num_lru_pages;
int tmp_num_lru_pages;
char* bitmap;


// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
	
  struct run *r;
	if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");



	//시ㅣㅣㅣ발 여기가 해제한다음에 체크하면 당연히 페이지폴트 나지........
	//해제하려는 해당 부분, lru list에 있으면 여기서 없애주기.
	



  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  
  
  
  
  if(kmem.use_lock)
    release(&kmem.lock);
  
  num_free_pages++;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;
	
try_again:
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
//  if(!r && reclaim())
//	  goto try_again;

  if(r)
  {
    kmem.freelist = r->next;
    
  }
  if(kmem.use_lock)
    release(&kmem.lock);    

  if(!r)
  {
  	cprintf("inside... kalloc/if(!r)\n");
  	
  	
  	//***********변수들 선언
  	struct page * victim;
  	int block_number;
  	
  	
  	//***********select victim page and bitmap location
  	victim = (struct page*)selectvictim_lrulist();
  	
  	cprintf("victim val-->%x\n",*(victim->vaddr));
  	*(victim->vaddr) = 0xF;
  	cprintf("victim val-->%x\n",*(victim->vaddr));
  	
  	acquire(&bitmaplock);
  	block_number = getfree_bitmap();
  	set_bitmap(block_number);
  	release(&bitmaplock);
  	
  	
  	//***********Exceptions
  	if((int)victim==-1 || block_number==-1){
  		cprintf("Out of Memory\n");
  		return 0;
  	}
  	
  	//***********swapout
  	cprintf("swapwrite assign blockno: %d\n",block_number);
  	swapwrite((char*)(victim->vaddr), block_number/*4kb단위*/);
  	kfree( P2V(*extern_walkpgdir(victim->pgdir,victim->vaddr,0) & 0xFFFFF000) );
		*extern_walkpgdir(victim->pgdir,victim->vaddr,0) &= 0x00000FFF; 			//clear left 20bit [ 0000...00 ][ ---12bit--- ]
  	*extern_walkpgdir(victim->pgdir,victim->vaddr,0) |= block_number<<12; //wrtie OFFSET(block_number) in the left 20bit		 [ --OFFSET--][ ---12bit--- ]

  	*extern_walkpgdir(victim->pgdir,victim->vaddr,0) &= ~PTE_P; //clear PTE_P
		



//		cprintf("pgdir from kalloc --> %x\n",myproc()->pgdir);
//		int aa = 3;//
//		aa = (int) *(victim->vaddr); //왜 pagefault안일어나지?
//		cprintf("aaaa :%d  -- %x\n",aa,aa);//미친건가? 왜 출력하면 pgflt나고 출력안하면 안나지?
// 																								lazy eval 관련된건가?
//개ㅐㅐㅐㅐㅐㅐㅐㅐㅐㅐㅐㅐㅐ시ㅣㅣㅣㅣㅣㅣㅣㅣㅣㅣㅣ바ㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㅏㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹㄹ
		
		
  	del_lrulist(victim);
  	
  	goto try_again;
  }
    
    

    
    
    
  num_free_pages--;
  return (char*)r;
}



//lru list 에 들어가는 vaddr = user va.


struct page* getfree_lruspace()
{
	//struct page pages[PHYSTOP/PGSIZE];
	for(int i=0; i<PHYSTOP/PGSIZE; ++i)
	{
		if( pages[i].next == 0) 
		{		
			pages[i].next = (struct page*)1;
			return &(pages[i]);
		}
	}

	panic("Out of memory!! NO free lru list");
	return 0;
}

void init_lrulist() //이걸로 비트맵도 초기화 시켜줌.
{	
	//*****************연결리스트에 사용할 배열 초기화 *****************// 뭔가 포인터는 애초에 null로 바뀐다고 배웠던것 같기도 하고...
	for(int i=0; i<PHYSTOP/PGSIZE; ++i)
	{
		pages[i].vaddr=0;
		pages[i].next=0;
		pages[i].prev=0;
		pages[i].pgdir=0;
	}
	
	
	struct page* dummy_page = (struct page*)getfree_lruspace();
	page_lru_head = (struct page*)getfree_lruspace();	
	bitmap = kalloc();
	memset(bitmap, 0, PGSIZE);
	
	//**********꼬임 방지용 DUMMY 노드 하나 생성****************//
	dummy_page->next = dummy_page;
	dummy_page->prev = dummy_page;
	dummy_page->vaddr = DUMMY;
	
	page_lru_head->next = dummy_page;
	
	
	//************값들 초기화 *************************//
	num_free_pages=0;
	num_lru_pages=0;
	
	INIT_LRULIST = 0;
}

void print_lrulist()
{
	struct page* iterpage = page_lru_head->next;
	for(int i=0; i<num_lru_pages; i++)
	{
		iterpage = iterpage->next;
		cprintf("LRU: pgdir: %x  -- vaddr: %x\n",iterpage->pgdir,iterpage->vaddr);
	}
}

void add_lrulist(pde_t* pgdir,char* vaddr)
{
	if(INIT_LRULIST==1){
		init_lrulist();
		set_bitmap(0);
		initlock(&bitmaplock,"bitmaplock");
	}
	//*************** 추가할 새로운 노드 생성*********************//
	struct page* newpage = (struct page*)getfree_lruspace();
	newpage->pgdir = pgdir;
	newpage->vaddr = vaddr;
	
	//***************** add 하는게 처음일 경우, DUMMY NODE 제거용,아마 dummy필요없는데 불안해서 걍 만들*************8//
	
	if ( page_lru_head->next->vaddr == DUMMY) 
	{
		
		page_lru_head->next->pgdir = pgdir;
		page_lru_head->next->vaddr = vaddr; 
	}
	
	//*************** 평범한 경우의 노드 추가 ************************//
	else
	{
		struct page* A = page_lru_head->next;
		struct page* B = page_lru_head->next->next;
		
		A->next = newpage;
		B->prev = newpage;
		newpage->next = B;
		newpage->prev = A;		
	}
	//*************** LRU list 개수 갱신 ****************//
	*extern_walkpgdir(newpage->pgdir,newpage->vaddr,0) |= PTE_A; //set PTE_A
	++num_lru_pages;
	
}


struct page* del_lrulist(struct page* delpage)
{

	//*************** 노드 삭제 ****************//
	
	//시발 이거 못찾아서 존나고생함. 헤드가 가리키고 있는 노드 지울땐 헤드 옮겨주거나 뭐 아무튼 뭔갈 해줘야됨.
	if(delpage == page_lru_head->next)
	{
		page_lru_head->next = page_lru_head->next->next;
	}
	
	
	if(num_lru_pages == 1)
	{
		cprintf("\n\nINSIDE DELETE!!! lru page only 1 left.... init!\n");
		init_lrulist();
		return	page_lru_head;
	}
	
	struct page* tmp_page = delpage->prev;

	delpage->prev->next = delpage -> next;
	delpage->next->prev = delpage -> prev;

	delpage->vaddr=0;
	delpage->next=0;
	delpage->prev=0;
	delpage->pgdir=0;
	
	
	
	//*************** LRU list 개수 갱신 ****************//
	--num_lru_pages;
	
	//cprintf("del finished... page: %d\n",num_lru_pages);
	
	return tmp_page;
}


void movehead_lrulist() 
{
	//head node를 한칸 앞으로 보내서, 앞에있는 놈을 tail로 보낸것 처럼 만들기
	page_lru_head->next = page_lru_head->next->next;
}




int selectvictim_lrulist()
{

	//********* tmp_page로 loop iteration 하기 ********************//
	struct page* tmp_page;
	tmp_page = page_lru_head;
	
	tmp_num_lru_pages = num_lru_pages;
	for(int i = 0; i<tmp_num_lru_pages; i++){
		
		tmp_page = tmp_page->next;
		pte_t* pte_addr = extern_walkpgdir(tmp_page->pgdir, tmp_page->vaddr,0);
		
		//*********PTE_P 가 0이면 안되지. 그럼 이 페이지는 지워줘야지 ***********//
		if (!(*pte_addr&PTE_P))
		{
			tmp_page = del_lrulist(tmp_page);
			continue;
		}
		//*********PTE값이 0이면 문제있는거지 ****************//
		if( pte_addr == 0)
		{
			panic("selectvictim_lrulist");
		}
		//*********PTE_A 값 1이면, 0으로만들고 노드 tail로 보내기(=헤드 옮기기) ***************************//
		if( ((*(pte_addr))&PTE_A) != 0 )
		{
			*pte_addr = (*pte_addr)& (~PTE_A);
			movehead_lrulist();
			cprintf("LOOPING LRU LIST------- NODE's PTE_A is 1\n");
		}
		
		//*********PTE_A 값 0이면, evict하기 ***************************//
		else
		{//select victim!! 찾았다요놈
		cprintf("LOOPING LRU LIST------- FOUND PAGE TO EVICT!!! -----%x---- NODE's PTE_A is 0\n",tmp_page->vaddr);
			return (int)tmp_page;
		}	
	}
	
	cprintf("OUT OF MEMORY!! inside-->selectvictim_lrulist\n");
	return -1;
}

void check_lrulist(pde_t *pgdir,char* uva)
{
	struct page* tmp_page;
	
	
	tmp_page = page_lru_head;


	tmp_num_lru_pages = num_lru_pages;
	for (int i=0; i<tmp_num_lru_pages; ++i)
	{
		tmp_page = tmp_page->next;
		
		/*if(uva<(char*)0x5000){
			cprintf("\t\tCHECK-- pg->vaddr: %x --- pg->pgdir: %x\n",tmp_page->vaddr,tmp_page->pgdir);		
			cprintf("\t\tCHECK-- free_addr: %x --- freepgdir: %x\n\n",uva,pgdir);
		}*/
		
		
		if( (uva==tmp_page->vaddr) && (tmp_page->pgdir==pgdir))
		{
			tmp_page = del_lrulist(tmp_page);
		}
	}
	
}



int get_bitmap(int n)
{
	//acquire(&bitmaplock);
	//*********n번째 bit = n/8번째 byte에서 n%8번째 bit *****************//
	char* ptr; //무야 어쩌다보니 그냥 tmp값이 되어버림.
	int return_val;
	
	ptr = bitmap; //  bitmap의 시작.
	ptr = ptr + (n/8); //    n/8번째 byte
	return_val = (int)(*ptr>>(n%8))&(0x1);   
	 
	//release(&bitmaplock);

	return return_val;
}

void set_bitmap(int n)
{
	//acquire(&bitmaplock);
	//*********n번째 bit = n/8번째 byte에서 n%8번째 bit *****************//
	char* ptr;
	ptr = bitmap; 		//  bitmap의 시작.
	ptr = ptr + (n/8); 							//  n/8번째 byte
	*ptr = (*ptr|(0x1<<(n%8)));   	//  set bitmap
	 
	//release(&bitmaplock);
}

void clear_bitmap(int n)
{
	//acquire(&bitmaplock);
	//*********n번째 bit = n/8번째 byte에서 n%8번째 bit *****************//
	char* ptr;
	ptr = bitmap; 				//  bitmap의 시작.
	ptr = ptr + (n/8); 									//  n/8번째 byte
	*ptr = (*ptr & (~(0x1<<(n%8))) );   //  clear bitmap
	 
	//release(&bitmaplock);
}

int getfree_bitmap()
{
		cprintf("bitmaplock!\n");
		//acquire(&bitmaplock);
	for(int i=0; i<4096*8; ++i)
	{
		if( get_bitmap(i)==0)
		{
			//release(&bitmaplock);
			return i;
		}
		//release(&bitmaplock);
	}
	
	cprintf("\n***************OUT OF MEMORY!!! ---> inside getfree_bitmap() function\n");
	return -1;
}

void print_pages()
{
	cprintf("--pages: %d\n",num_lru_pages);
}

