// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart[32];   // Block number of first inode block
  uint bmapstart[32];    // Block number of first free map block
};

#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
//이거 바꿀때 fs.h 에 있는 dinode랑 file.h에 있는 inode 둘다 바꿔줘야뎀
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT*NINDIRECT)
//#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT*50)


// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+2];   // Data block addresses
};

//block group 개수
#define NBG ((FSSIZE/BGSIZE)==0 ? 0 : (FSSIZE/BGSIZE) - 1)

// Inodes per block. = 8 
// Inode Blocks per Block Group
#define IPB           (BSIZE / sizeof(struct dinode))
#define IBPBG					((BGSIZE/32))
 


// Block containing inode i
#define IBLOCK(i, sb)     ( (((i) / IPB)%IBPBG) + sb.inodestart[(((i) / IPB)/IBPBG)])

// Bitmap bits per block  BSIZE는 바이트니까 비트 단위로 바꿔줬을 뿐이네.
// Num of BitmapBlock Per BlockGroup
#define BPB           (BSIZE*8)
#define BBPBG ((BGSIZE/BPB)+1)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) (((b/BPB)%BBPBG) + sb.bmapstart[(b/BPB)/BBPBG])

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

