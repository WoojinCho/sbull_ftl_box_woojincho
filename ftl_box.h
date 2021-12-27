#ifndef __H_FTL_BOX__
#define __H_FTL_BOX__

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>       /* printk() */
#include <linux/slab.h>         /* kmalloc() */
#include <linux/fs.h>           /* everything... */
#include <linux/errno.h>        /* error codes */
#include <linux/timer.h>
#include <linux/types.h>        /* size_t */
#include <linux/fcntl.h>        /* O_ACCMODE */
#include <linux/hdreg.h>        /* HDIO_GETGEO */
#include <linux/kdev_t.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>  /* invalidate_bdev */
#include <linux/bio.h>

MODULE_LICENSE("Dual BSD/GPL");


#define NOB (1024) //Number Of Block
#define PPB (256) //Page Per Block
#define NOP ((NOB)*(PPB)) // Number Of Page
#define KERNEL_SECTOR_SIZE 512

#define NUMKEY 4194304

#define NIL -1
#define PAGE_READ 0
#define PAGE_WRITE 1
#define BLOCK_ERASE 2

typedef struct ftlpage {
	unsigned valid : 1; 
  unsigned long sector;
} _page;

typedef struct flash_block {
	_page *pages;
	int32_t valid_pages; 
	int32_t curr_free_page; 
} _flash_block;

typedef struct mpte {
	int32_t block;
	int32_t page;
} _mpte;

struct sbull_dev {
        int size;                    
        int users;                    
        char* data;
        spinlock_t lock;            
        struct request_queue *queue;
        struct gendisk *gd; 
        _flash_block *flash_blocks;
        _mpte *mapping_table;       
};

#endif // !__H_FTL_BOX__

