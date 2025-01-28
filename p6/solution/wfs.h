#define BLOCK_SIZE (512)
#define MAX_NAME   (28)
#define MAX_DISK_NUM   (10)
#define D_BLOCK    (10)
#define IND_BLOCK  (D_BLOCK+1)
#define N_BLOCKS   (IND_BLOCK+1)
#define _GNU_SOURCE
#define FUSE_USE_VERSION 30
#define WFS_MAGIC 0x12345678

#define BLOCK_NUM_DISK(block_num) ((block_num) >> 16)
#define BLOCK_NUM_OFFSET(block_num) ((block_num) & 0xFFFF)
#define MAKE_BLOCK_NUM(disk_num, block_num) (((disk_num) << 16) | (block_num))

#include <time.h>
#include <sys/stat.h>

/*
  The fields in the superblock should reflect the structure of the filesystem.
  `mkfs` writes the superblock to offset 0 of the disk image. 
  The disk image will have this format:

          d_bitmap_ptr       d_blocks_ptr
               v                  v
+----+---------+---------+--------+--------------------------+
| SB | IBITMAP | DBITMAP | INODES |       DATA BLOCKS        |
+----+---------+---------+--------+--------------------------+
0    ^                   ^
i_bitmap_ptr        i_blocks_ptr

*/

// Superblock
struct wfs_sb {
    size_t num_inodes;
    size_t num_data_blocks;
    off_t i_bitmap_ptr;
    off_t d_bitmap_ptr;
    off_t i_blocks_ptr;
    off_t d_blocks_ptr;
    // Extend after this line
    int raid_mode;       // 0 for RAID0, 1 for RAID1, 2 for RAID1v
    int num_disks;       // Number of disks in the array
    int disk_array[MAX_DISK_NUM];  // Store disk identifiers/order, max 16 disks supported
    int magic;           // magic num
};

// Inode
struct wfs_inode {
    int     num;      /* Inode number */
    mode_t  mode;     /* File type and mode */
    uid_t   uid;      /* User ID of owner */
    gid_t   gid;      /* Group ID of owner */
    off_t   size;     /* Total size, in bytes */
    int     nlinks;   /* Number of links */

    time_t atim;      /* Time of last access */
    time_t mtim;      /* Time of last modification */
    time_t ctim;      /* Time of last status change */

    off_t blocks[N_BLOCKS];
};

// Directory entry
struct wfs_dentry {
    char name[MAX_NAME];
    int num;
};
