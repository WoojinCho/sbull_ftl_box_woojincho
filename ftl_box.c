#include "ftl_box.h"
#include "sbull.h"
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

_flash_block *flash_blocks;
_mpte *mapping_table;
static int32_t curr_free_block;
static int32_t next_free_block;
static int32_t num_free_blocks;
static unsigned long  valid_page_copies = 0;
int none=-1;
char* val;
static unsigned long write_reqs = 0;

static int32_t box_create(void) {
  	int i;
	flash_blocks = (_flash_block *)vmalloc(sizeof(_flash_block) * NOB); 
	for (i=0; i<NOB; i++) 
	{
		flash_blocks[i].pages = (_page *)vmalloc(sizeof(_page) * PPB);
		flash_blocks[i].valid_pages = 0;
		flash_blocks[i].curr_free_page = 0;
	}
	
	curr_free_block = 0;
	next_free_block = 1;
	num_free_blocks = NOB;
	mapping_table = (_mpte *)vmalloc(sizeof(_mpte) * NOP);
  	memset(mapping_table, 0xff, sizeof(_mpte) * NOP);
	return 0;
}
	
static int32_t box_destroy(void) {
	int i;
	for (i=0; i<NOB; i++) {
		vfree(flash_blocks[i].pages);
	}
	vfree(flash_blocks);
	vfree(mapping_table);
	return 1;
}

static int32_t select_victim(void)
{
	int i;
	int32_t min_valid_pages;
	int32_t victim=0;

	min_valid_pages = PPB;

	for (i = 0; i < NOB; i++)
	{
		if (i == curr_free_block)
			continue;
		if (flash_blocks[i].valid_pages < min_valid_pages)
		{
			min_valid_pages = flash_blocks[i].valid_pages;
			victim = i;
			if (min_valid_pages == 0)
				break;
		}
	}
	return victim;
}

static void copy_valid_pages(struct sbull_dev *dev, int32_t old_block_num)
{
	int i;
	int32_t new_block_num;
	int32_t new_page_num;
  	int32_t old_page_num;
	_flash_block *new_block;
	unsigned long new_offset;
  	unsigned long old_offset;
  	unsigned long sector;
	
	new_block_num = curr_free_block;
	new_block = &flash_blocks[new_block_num];
	// Nothing to copy
	if (flash_blocks[old_block_num].valid_pages == 0)
		return;
  	
	for (i = 0; i < PPB; ++i)
	{
		if (flash_blocks[old_block_num].pages[i].valid)
		{
			// Copy valid page
			new_page_num = new_block->curr_free_page++;
			new_block->pages[new_page_num] = flash_blocks[old_block_num].pages[i];
			++new_block->valid_pages;
			++valid_page_copies;
			// Copy the value to dev. Calculate offset using new_block_num and new_page_num.
      			old_page_num = i;
      			new_offset = (new_block_num * PPB + new_page_num) *  KERNEL_SECTOR_SIZE;
      			old_offset = (old_block_num * PPB + old_page_num) *  KERNEL_SECTOR_SIZE;
      			memcpy(dev->data + new_offset, dev->data + old_offset, KERNEL_SECTOR_SIZE);
			
			// Update mapping table
			sector = new_block->pages[new_page_num].sector; 
			if (sector >= NOP)
				printk("copy_valid_pages: wrong sector number\n");
      			mapping_table[sector].block = new_block_num;
			mapping_table[sector].page = new_page_num;
		}
	}
}

static void flash_block_erase(int32_t victim)
{
	flash_blocks[victim].valid_pages = 0;
	flash_blocks[victim].curr_free_page = 0;
}

static void garbage_collect(struct sbull_dev *dev)
{
	int32_t victim;
	// Select victim block
	victim = select_victim();
	// Copy valid pages in the victim block to curr_free_block
	copy_valid_pages(dev, victim);
	// Erase victim block
	flash_block_erase(victim);
	++num_free_blocks;
	next_free_block = victim;
}

static void flash_page_read(struct sbull_dev *dev, unsigned long sector, char *buffer)//int32_t -> int
{
  int32_t block_num;
  int32_t page_num;
  unsigned long offset;

  if (mapping_table[sector].block == -1) 
	printk("page none %d\n",none);
  else
  {
    	block_num = mapping_table[sector].block;
  	page_num = mapping_table[sector].page;
  	// Read value from dev and return.Calculate offset using block_num and page_num.
  	offset = (block_num * PPB + page_num) *  KERNEL_SECTOR_SIZE;//(block * PPB + page) *  KERNEL_SECTOR_SIZE;
	if (offset + KERNEL_SECTOR_SIZE > dev->size)
		printk("read oversize\n");
    	memcpy(buffer, dev->data + offset, KERNEL_SECTOR_SIZE); 
  }
}

static void flash_page_write(struct sbull_dev *dev, unsigned long sector, char *buffer)//int32_t
{
	_flash_block *block;
	int32_t block_num;
	int32_t page_num;
	int32_t old_block_num;
	int32_t old_page_num;
	unsigned long offset;

	if (sector >= NOP)
		printk("flash_page_write: wrong sector number\n");
	
	++write_reqs;
	// Wrote this value before, invalidation required
	if (mapping_table[sector].block != -1)
	{
		// Invalidate the page with the old value
		old_block_num = mapping_table[sector].block;
		old_page_num = mapping_table[sector].page;
		flash_blocks[old_block_num].pages[old_page_num].valid = 0;
		--flash_blocks[old_block_num].valid_pages;
	}

	// If only one block is a whole free block, perform GC
	if (num_free_blocks == 1)
    		garbage_collect(dev);
	
	// Write the value in flash
	block_num = curr_free_block;
	block = &flash_blocks[block_num];
	page_num = block->curr_free_page;
	block->pages[page_num].valid = 1;
  	block->pages[page_num].sector = sector;
	// Write value to dev. Calculate offset using block_num and page_num.
	offset = (block_num * PPB + page_num) *  KERNEL_SECTOR_SIZE;
	if (offset + KERNEL_SECTOR_SIZE > dev->size)
		printk("write oversize\n");
	memcpy(dev->data + offset, buffer, KERNEL_SECTOR_SIZE);
	++block->valid_pages;
	
	// Update the mapping table
	mapping_table[sector].block = block_num;
	mapping_table[sector].page = page_num;

	// Update curr_free_page, curr_free_block, next_free_block
	if (++block->curr_free_page == PPB)
	{
    		--num_free_blocks;
		curr_free_block = next_free_block;
		if (num_free_blocks == 1)
			next_free_block = -1;
		else
			++next_free_block;
	}
}

static int32_t get_valid_page_copies(void)
{
	return valid_page_copies;
}

static int simple_init(void)
{
        box_create();
        printk(KERN_INFO "Loading FTL Moudule\n");
        return 0;
}

static void simple_exit(void)
{
     	box_destroy();
	printk("write_reqs: %lu\n", write_reqs);
	printk("valid_page_copies: %lu\n", valid_page_copies);
        printk(KERN_INFO "Removing FTL Module\n");
}

module_init( simple_init );
module_exit( simple_exit );

EXPORT_SYMBOL(box_create);
EXPORT_SYMBOL(box_destroy);
EXPORT_SYMBOL(select_victim);
EXPORT_SYMBOL(copy_valid_pages);
EXPORT_SYMBOL(flash_block_erase);
EXPORT_SYMBOL(garbage_collect);
EXPORT_SYMBOL(flash_page_read);
EXPORT_SYMBOL(flash_page_write);
EXPORT_SYMBOL(get_valid_page_copies);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple Module");
MODULE_AUTHOR("WoojinCho");


