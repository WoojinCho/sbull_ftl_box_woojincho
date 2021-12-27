/*
 * Sample disk driver, from the beginning.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/timer.h>
#include <linux/types.h>	/* size_t */
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/hdreg.h>	/* HDIO_GETGEO */
#include <linux/kdev_t.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>	/* invalidate_bdev */
#include <linux/bio.h>
#include "ftl_box.h"

MODULE_LICENSE("Dual BSD/GPL");

static int sbull_major = 0;
static int hardsect_size = 512;
static int nsectors = NOP;
static int ndevices = 1;

extern int32_t box_create(void);
extern int32_t box_destroy(void);
extern int32_t select_victim(void);
extern void copy_valid_pages(struct sbull_dev *dev, int32_t old_block_num);
extern void flash_block_erase(int32_t victim);
extern void garbage_collect(struct sbull_dev *dev);
extern void flash_page_write(struct sbull_dev *dev, unsigned long sector, char *buffer);
extern void flash_page_read(struct sbull_dev *dev, unsigned long sector, char *buffer);
extern int32_t get_valid_page_copies(void);

/*
 * Minor number and partition management.
 */
#define SBULL_MINORS	16
#define MINOR_SHIFT	4
#define DEVNUM(kdevnum)	(MINOR(kdev_t_to_nr(kdevnum)) >> MINOR_SHIFT

/*
 * After this much idle time, the driver will simulate a media change.
 */
#define INVALIDATE_DELAY	30*HZ

static struct sbull_dev *Devices = NULL;
/*
 * Handle an I/O request.
 */

static void sbull_transfer(struct sbull_dev *dev, unsigned long sector,
		unsigned long nsect, char *buffer, int write)
{
	int i;
	
	if (write)
	{
    		for (i = 0; i < nsect; i++)
      			flash_page_write(dev, sector + i, buffer);
	}
	else 
	{	
    		for (i = 0; i < nsect; i++)
      			flash_page_read(dev, sector + i, buffer);
	}
}


/*
 * Transfer a single BIO.
 */
static int sbull_xfer_bio(struct sbull_dev *dev, struct bio *bio)
{
    /* TODO : Write your codes */
	struct bio_vec bvec;
	struct bvec_iter iter;
	sector_t sector = bio->bi_iter.bi_sector;

	bio_for_each_segment(bvec, bio, iter) {
		char *buffer = kmap_atomic(bvec.bv_page) + bvec.bv_offset;
		//etc=vmalloc(sizeof(buffer));
		sbull_transfer(dev, sector, bio_cur_bytes(bio)>>9,
				buffer, bio_data_dir(bio) == WRITE);
		//printk("bio cur bytes %ld",bio_cur_bytes(bio));
		sector += bio_cur_bytes(bio)>>9;
		kunmap_atomic(buffer);
	}
	return 0; /* Always "succeed" */
}


/*
 * The direct make request version.
 */
static void sbull_make_request(struct request_queue *q, struct bio *bio)
{
	struct sbull_dev *dev = q->queuedata;
	int status;

	status = sbull_xfer_bio(dev, bio);
    bio_endio(bio);	
}


/*
 * Open and close.
 */
/* changed open and release function */

static int sbull_open(struct block_device *bdev, fmode_t mode)
{
	
    struct sbull_dev *dev = bdev->bd_disk->private_data;
    /* file->private_data = dev is deleted*/
    /* TODO : Write your codes */

	spin_lock(&dev->lock);
	if (! dev->users) 
		check_disk_change(bdev);
	dev->users++;
	spin_unlock(&dev->lock);

    return 0;
}

static void sbull_release(struct gendisk *disk, fmode_t mode)
{
	struct sbull_dev *dev = disk->private_data;
    
	spin_lock(&dev->lock);
	dev->users--;

	if (!dev->users) {
	}
	spin_unlock(&dev->lock);

}

/*
 * The ioctl() implementation
 */

int sbull_ioctl (struct block_device *bdev, fmode_t mode,
                 unsigned int cmd, unsigned long arg)
{
    /* TODO : Write your codes */
	//struct sbull_dev *dev=bdev->bd_disk->private_data;
	//
	switch(cmd){
	}

	return 0; /* unknown command */
}



/*
 * The device operations structure.
 */
static struct block_device_operations sbull_ops = {
    /* TODO : Write your codes */
	.owner           = THIS_MODULE,
	.open 	         = sbull_open,
	.release 	 = sbull_release,
	.ioctl	         = sbull_ioctl
};


/*
 * Set up our internal device.
 */
static void setup_device(struct sbull_dev *dev, int which)
{
    /* TODO : Write your codes */
    /*
     * Get some memory.
     */
	memset (dev, 0, sizeof (struct sbull_dev));
	dev->size = nsectors*hardsect_size;
	dev->data=vmalloc(dev->size);
	if (dev->data == NULL) {
		printk (KERN_NOTICE "vmalloc failure.\n");
		return;
	}
	spin_lock_init(&dev->lock);
    

    /*
     * The I/O queue, depending on whether we are using our own
     * make_request function or not.
     */
    dev->queue = blk_alloc_queue(GFP_KERNEL);
    if (dev->queue == NULL)
        goto out_vfree;
    blk_queue_make_request(dev->queue, (make_request_fn *)sbull_make_request);		
    dev->queue->queuedata = dev;
    /*
     * And the gendisk structure.
     */
	dev->gd = alloc_disk(SBULL_MINORS);
	if (! dev->gd) {
		printk (KERN_NOTICE "alloc_disk failure\n");
		goto out_vfree;
	}
	dev->gd->major = sbull_major;
	dev->gd->first_minor = which*SBULL_MINORS;
	dev->gd->fops = &sbull_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;
	snprintf (dev->gd->disk_name, 32, "sbull%c", which + 'a');
	set_capacity(dev->gd, nsectors*(hardsect_size/KERNEL_SECTOR_SIZE));
	add_disk(dev->gd);
    return;

out_vfree:
    if (dev->data)
        vfree(dev->data);
}



static int __init sbull_init(void)
{
	int i;
	/*
	 * Get registered.
	 */
	sbull_major = register_blkdev(sbull_major, "sbull");
	if (sbull_major <= 0) {
		printk(KERN_WARNING "sbull: unable to get major number\n");
		return -EBUSY;
	}
	/*
	 * Allocate the device array, and initialize each one.
	 */
	Devices = vmalloc(ndevices*sizeof (struct sbull_dev));
	if (Devices == NULL)
		goto out_unregister;
	for (i = 0; i < ndevices; i++) 
		setup_device(Devices + i, i);
	printk(KERN_INFO "Loading SBULL Moudule\n");

    return 0;

  out_unregister:
	unregister_blkdev(sbull_major, "sbd");
	return -ENOMEM;
}

static void sbull_exit(void)
{
	int i;

	for (i = 0; i < ndevices; i++) {
		struct sbull_dev *dev = Devices + i;

		if (dev->gd) {
			del_gendisk(dev->gd);
			put_disk(dev->gd);
		}
		if (dev->queue) {
				kobject_put (&dev->queue->kobj);
		}
		if (dev->data)
			vfree(dev->data);
	}
	unregister_blkdev(sbull_major, "sbull");
	vfree(Devices);
	printk(KERN_INFO "Removing SBULL Moudule\n");
}
	
module_init(sbull_init);
module_exit(sbull_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SBULL Module");
MODULE_AUTHOR("WoojinCho");
