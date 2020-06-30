#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat xv6_stat  // avoid clash with host struct stat
#include "types.h"
#include "fs.h"
#include "stat.h"
#include "param.h"

#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

//inode per iblock * iblocks per BG * BG per FS = total inodes
#define NINODES IPB*IBPBG*NBG

// Disk layout:
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]

int nbitmap = FSSIZE/(BSIZE*8) + 1;
int ninodeblocks = NINODES / IPB + 1; //개수니까 int나눗셈에서 truncate 되어버리는거 때문에 1 더해주는건가?
int nlog = LOGSIZE;
int nmeta;    // Number of meta blocks (boot, sb, nlog, inode, bitmap)
int nblocks;  // Number of data blocks //FSSIZE - nmeta. 그니까 이걸 늘리고 싶으면 FSSIZE를 키우는게 맞는

int fsfd;
struct superblock sb;
char zeroes[BSIZE];
uint freeinode = 1;
uint freeblock;
uint freeblock0;

void balloc(int,int,int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);

// convert to intel byte order
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i, cc, fd;
  uint rootino, inum, off;
  struct dirent de;
  char buf[BSIZE];
  struct dinode din;
  int surplus_space_divBy32 = FSSIZE%BGSIZE;
	
	//dirent de 는 디렉토리 엔트리인듯.


  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if(argc < 2){
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);
  }

  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0){
    perror(argv[1]);
    exit(1);
  }

  // 1 fs block = 1 disk sector
  nmeta = 2 + nlog + IBPBG + BBPBG;
  nmeta += surplus_space_divBy32;
  nblocks = FSSIZE - nmeta;
 
 
 
 	//sb = super block !!!
  sb.size = xint(FSSIZE);
  sb.nblocks = xint(nblocks);
  sb.ninodes = xint(NINODES);
  sb.nlog = xint(nlog);
  sb.logstart = xint(2);
 
  for (int blkgroup =0; blkgroup<FSSIZE; blkgroup+=BGSIZE){
  	int i = blkgroup / BGSIZE;
  	sb.inodestart[i] = xint(2+nlog + blkgroup);
  	sb.bmapstart[i] = xint(2+nlog+ (IBPBG) + blkgroup);
	}
 
 	
  printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         2+nlog+ninodeblocks+nbitmap, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE);
         
	printf("BGSIZE!!  %d\n",BGSIZE);
  freeblock = nmeta;     // the first free block that we can allocate


	//일단 전체 FS을 0으로 밀어버리기
  for(i = 0; i < FSSIZE; i++)
    wsect(i, zeroes);

	
	//char buf[BSIZE]
	//memset(start_location(pointer),value,size)
  memset(buf, 0, sizeof(buf));//buffer 를 0으로 밀어버림.
  memmove(buf, &sb, sizeof(sb));//0으로 밀어버린 버퍼에 sb를 때려박기
  wsect(1, buf); //1번 섹터에 버퍼내용 쓰기. 1번이면 superblock이겠지.

  rootino = ialloc(T_DIR); //inode 하나를 만들어서 할당해주는듯? 코드보면 free inode 개수 한개 늘림.
  assert(rootino == ROOTINO);

  bzero(&de, sizeof(de)); //새로운 Directory Entry 를 만들어서 (사실 전역변수로 선언되어 있고 그냥 0으로 밀어버린거임)
  de.inum = xshort(rootino); //어디에 소속된 directory인지 알려주려고 inode 번호 준ㄷㅅ
  strcpy(de.name, "."); // "." 자기 자신 추가
  iappend(rootino, &de, sizeof(de)); //rootinode에 해당 Dir Entry 추가

  bzero(&de, sizeof(de)); //위에랑 똑같은데 이번엔 ".." 상위폴더 추가.
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de)); //이제 여기까지가 root inode만드는거인듯?

  for(i = 2; i < argc; i++){ //인자로 들어온건 아마 파일이겠지? 파일 경로인듯. 사실 그게 파일 이름이지ㅇㅇ
  
  	assert(index(argv[i], '/') == 0);

    if((fd = open(argv[i], 0)) < 0){
      perror(argv[i]);
      exit(1);
    }

    // Skip leading _ in name when writing to file system.
    // The binaries are named _rm, _cat, etc. to keep the
    // build operating system from trying to execute them
    // in place of system binaries like rm and cat.
    if(argv[i][0] == '_')
      ++argv[i];

		//ialloc---> return inum = free_inum++
    inum = ialloc(T_FILE); //뭔가 inode하나 생성해서 현재 dinode에 있는거 넣어주는듯.

    bzero(&de, sizeof(de)); //bzero는 하나의 블럭을 0으로 밀어버리는거임.
    de.inum = xshort(inum); //
    strncpy(de.name, argv[i], DIRSIZ); //strncpy(DST,SRC,n)  //그냥 파일 이름 지정해주는거같은디. DIRSIZ = 14. 그냥 파일이름 길이임.
    iappend(rootino, &de, sizeof(de));

    while((cc = read(fd, buf, sizeof(buf))) > 0)
      iappend(inum, buf, cc);

    close(fd);
  }

  // fix size of root inode dir
  rinode(rootino, &din);
  off = xint(din.size);
  off = ((off/BSIZE) + 1) * BSIZE;
  din.size = xint(off);
  winode(rootino, &din);  //rootino 정보를 din 에 넣어주는듯 
	
	
	
	freeblock0 = freeblock;
  
	for(int i=0; i<FSSIZE; i+=BGSIZE){
	  
	  if (i==0) balloc(0,freeblock0,0);
		//else	balloc(i,i+IBPBG+BBPBG,i/BGSIZE);
		break;
	}
  
  exit(0);
}

void
wsect(uint sec, void *buf)
{
	// lseek 는 파일을 읽거나 쓸때 위치를 받아내는 함수.
	// lseek (fd, offset, whence)
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE){
    perror("lseek");
    exit(1);
  }
  if(write(fsfd, buf, BSIZE) != BSIZE){
    perror("write");
    exit(1);
  }
}

void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE){
    perror("lseek");
    exit(1);
  }
  if(read(fsfd, buf, BSIZE) != BSIZE){
    perror("read");
    exit(1);
  }
}

uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  winode(inum, &din);
  return inum;
}

void
balloc(int start, int used, int index)
{
  uchar buf[BSIZE];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < (index*BGSIZE) + (BSIZE*8));
  bzero(buf, BSIZE);
  for(i = 0; i < used; i++){
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart[index]);
  wsect(sb.bmapstart[index], buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint indirect[NINDIRECT];
  uint x;

  rinode(inum, &din);
  off = xint(din.size);
  // printf("append inum %d at off %d sz %d\n", inum, off, n);
  while(n > 0){
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);
    if(fbn < NDIRECT){
      if(xint(din.addrs[fbn]) == 0){
        din.addrs[fbn] = xint(freeblock++);
      }
      x = xint(din.addrs[fbn]);
    } else {
      if(xint(din.addrs[NDIRECT]) == 0){
        din.addrs[NDIRECT] = xint(freeblock++);
      }
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      if(indirect[fbn - NDIRECT] == 0){
        indirect[fbn - NDIRECT] = xint(freeblock++);
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      }
      x = xint(indirect[fbn-NDIRECT]);
    }
    n1 = min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * BSIZE), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}
