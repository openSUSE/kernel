/*
 *  linux/drivers/video/fb_defio.c
 *
 *  Copyright (C) 2006 Jaya Kumar
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/list.h>

/* to support deferred IO */
#include <linux/rmap.h>
#include <linux/pagemap.h>

/*
 * struct fb_deferred_io_state
 */

struct fb_deferred_io_state {
	struct kref ref;

	struct mutex lock; /* mutex that protects the pageref list */
	/* fields protected by lock */
	struct fb_info *info;
};

static struct fb_deferred_io_state *fb_deferred_io_state_alloc(void)
{
	struct fb_deferred_io_state *fbdefio_state;

	fbdefio_state = kzalloc(sizeof(*fbdefio_state), GFP_KERNEL);
	if (!fbdefio_state)
		return NULL;

	kref_init(&fbdefio_state->ref);
	mutex_init(&fbdefio_state->lock);

	return fbdefio_state;
}

static void fb_deferred_io_state_release(struct fb_deferred_io_state *fbdefio_state)
{
	mutex_destroy(&fbdefio_state->lock);

	kfree(fbdefio_state);
}

static void fb_deferred_io_state_get(struct fb_deferred_io_state *fbdefio_state)
{
	kref_get(&fbdefio_state->ref);
}

static void __fb_deferred_io_state_release(struct kref *ref)
{
	struct fb_deferred_io_state *fbdefio_state =
		container_of(ref, struct fb_deferred_io_state, ref);

	fb_deferred_io_state_release(fbdefio_state);
}

static void fb_deferred_io_state_put(struct fb_deferred_io_state *fbdefio_state)
{
	kref_put(&fbdefio_state->ref, __fb_deferred_io_state_release);
}

/*
 * struct vm_operations_struct
 */

static void fb_deferred_io_vm_open(struct vm_area_struct *vma)
{
	struct fb_deferred_io_state *fbdefio_state = vma->vm_private_data;

	fb_deferred_io_state_get(fbdefio_state);
}

static void fb_deferred_io_vm_close(struct vm_area_struct *vma)
{
	struct fb_deferred_io_state *fbdefio_state = vma->vm_private_data;

	fb_deferred_io_state_put(fbdefio_state);
}

static struct page *fb_deferred_io_page(struct fb_info *info, unsigned long offs)
{
	void *screen_base = (void __force *) info->screen_base;
	struct page *page;

	if (is_vmalloc_addr(screen_base + offs))
		page = vmalloc_to_page(screen_base + offs);
	else
		page = pfn_to_page((info->fix.smem_start + offs) >> PAGE_SHIFT);

	return page;
}

/* this is to find and return the vmalloc-ed fb pages */
static int fb_deferred_io_fault(struct vm_fault *vmf)
{
	struct fb_info *info;
	unsigned long offset;
	struct page *page;
	int ret;
	struct fb_deferred_io_state *fbdefio_state = vmf->vma->vm_private_data;

	mutex_lock(&fbdefio_state->lock);

	info = fbdefio_state->info;
	if (!info) {
		ret = VM_FAULT_SIGBUS; /* our device is gone */
		goto err_mutex_unlock;
	}

	offset = vmf->pgoff << PAGE_SHIFT;
	if (offset >= info->fix.smem_len) {
		ret = VM_FAULT_SIGBUS;
		goto err_mutex_unlock;
	}

	page = fb_deferred_io_page(info, offset);
	if (!page) {
		ret = VM_FAULT_SIGBUS;
		goto err_mutex_unlock;
	}

	get_page(page);

	if (vmf->vma->vm_file)
		page->mapping = vmf->vma->vm_file->f_mapping;
	else
		printk(KERN_ERR "no mapping available\n");

	BUG_ON(!page->mapping);
	page->index = vmf->pgoff;

	mutex_unlock(&fbdefio_state->lock);

	vmf->page = page;

	return 0;

err_mutex_unlock:
	mutex_unlock(&fbdefio_state->lock);
	return ret;
}

int fb_deferred_io_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct fb_info *info = file->private_data;
	struct inode *inode = file_inode(file);
	int err = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (err)
		return err;

	/* Skip if deferred io is compiled-in but disabled on this fbdev */
	if (!info->fbdefio)
		return 0;

	inode_lock(inode);
	/* Kill off the delayed work */
	cancel_delayed_work_sync(&info->deferred_work);

	/* Run it immediately */
	schedule_delayed_work(&info->deferred_work, 0);
	inode_unlock(inode);

	return 0;
}
EXPORT_SYMBOL_GPL(fb_deferred_io_fsync);

/* vm_ops->page_mkwrite handler */
static int fb_deferred_io_mkwrite(struct vm_fault *vmf)
{
	struct fb_info *info;
	struct page *page = vmf->page;
	struct fb_deferred_io_state *fbdefio_state = vmf->vma->vm_private_data;
	struct fb_deferred_io *fbdefio;
	struct page *cur;

	/* this is a callback we get when userspace first tries to
	write to the page. we schedule a workqueue. that workqueue
	will eventually mkclean the touched pages and execute the
	deferred framebuffer IO. then if userspace touches a page
	again, we repeat the same scheme */

	file_update_time(vmf->vma->vm_file);

	/* protect against the workqueue changing the page list */
	mutex_lock(&fbdefio_state->lock);

	info = fbdefio_state->info;
	if (!info) {
		mutex_unlock(&fbdefio_state->lock);
		return VM_FAULT_SIGBUS; /* our device is gone */
	}

	fbdefio = info->fbdefio;

	/* first write in this cycle, notify the driver */
	if (fbdefio->first_io && list_empty(&fbdefio->pagelist))
		fbdefio->first_io(info);

	/*
	 * We want the page to remain locked from ->page_mkwrite until
	 * the PTE is marked dirty to avoid page_mkclean() being called
	 * before the PTE is updated, which would leave the page ignored
	 * by defio.
	 * Do this by locking the page here and informing the caller
	 * about it with VM_FAULT_LOCKED.
	 */
	lock_page(page);

	/* we loop through the pagelist before adding in order
	to keep the pagelist sorted */
	list_for_each_entry(cur, &fbdefio->pagelist, lru) {
		/* this check is to catch the case where a new
		process could start writing to the same page
		through a new pte. this new access can cause the
		mkwrite even when the original ps's pte is marked
		writable */
		if (unlikely(cur == page))
			goto page_already_added;
		else if (cur->index > page->index)
			break;
	}

	list_add_tail(&page->lru, &cur->lru);

page_already_added:
	mutex_unlock(&fbdefio_state->lock);

	/* come back after delay to process the deferred IO */
	schedule_delayed_work(&info->deferred_work, fbdefio->delay);
	return VM_FAULT_LOCKED;
}

static const struct vm_operations_struct fb_deferred_io_vm_ops = {
	.open		= fb_deferred_io_vm_open,
	.close		= fb_deferred_io_vm_close,
	.fault		= fb_deferred_io_fault,
	.page_mkwrite	= fb_deferred_io_mkwrite,
};

static int fb_deferred_io_set_page_dirty(struct page *page)
{
	if (!PageDirty(page))
		SetPageDirty(page);
	return 0;
}

static const struct address_space_operations fb_deferred_io_aops = {
	.set_page_dirty = fb_deferred_io_set_page_dirty,
};

int fb_deferred_io_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	vma->vm_ops = &fb_deferred_io_vm_ops;
	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	if (!(info->flags & FBINFO_VIRTFB))
		vma->vm_flags |= VM_IO;
	vma->vm_private_data = info->fbdefio_state;

	fb_deferred_io_state_get(info->fbdefio_state); /* released in vma->vm_ops->close() */

	return 0;
}
EXPORT_SYMBOL(fb_deferred_io_mmap);

/* workqueue callback */
static void fb_deferred_io_work(struct work_struct *work)
{
	struct fb_info *info = container_of(work, struct fb_info,
						deferred_work.work);
	struct list_head *node, *next;
	struct page *cur;
	struct fb_deferred_io *fbdefio = info->fbdefio;
	struct fb_deferred_io_state *fbdefio_state = info->fbdefio_state;

	/* here we mkclean the pages, then do all deferred IO */
	mutex_lock(&fbdefio_state->lock);
	list_for_each_entry(cur, &fbdefio->pagelist, lru) {
		lock_page(cur);
		page_mkclean(cur);
		unlock_page(cur);
	}

	/* driver's callback with pagelist */
	fbdefio->deferred_io(info, &fbdefio->pagelist);

	/* clear the list */
	list_for_each_safe(node, next, &fbdefio->pagelist) {
		list_del(node);
	}
	mutex_unlock(&fbdefio_state->lock);
}

void fb_deferred_io_init(struct fb_info *info)
{
	struct fb_deferred_io *fbdefio = info->fbdefio;
	struct fb_deferred_io_state *fbdefio_state;

	BUG_ON(!fbdefio);

	fbdefio_state = fb_deferred_io_state_alloc();
	if (!fbdefio_state)
		return;
	fbdefio_state->info = info;

	info->fbops->fb_mmap = fb_deferred_io_mmap;
	INIT_DELAYED_WORK(&info->deferred_work, fb_deferred_io_work);
	INIT_LIST_HEAD(&fbdefio->pagelist);
	if (fbdefio->delay == 0) /* set a default of 1 s */
		fbdefio->delay = HZ;

	info->fbdefio_state = fbdefio_state;
}
EXPORT_SYMBOL_GPL(fb_deferred_io_init);

void fb_deferred_io_open(struct fb_info *info,
			 struct inode *inode,
			 struct file *file)
{
	file->f_mapping->a_ops = &fb_deferred_io_aops;
}
EXPORT_SYMBOL_GPL(fb_deferred_io_open);

void fb_deferred_io_cleanup(struct fb_info *info)
{
	struct fb_deferred_io *fbdefio = info->fbdefio;
	struct fb_deferred_io_state *fbdefio_state = info->fbdefio_state;
	struct page *page;
	int i;

	BUG_ON(!fbdefio);
	cancel_delayed_work_sync(&info->deferred_work);

	/* clear out the mapping that we setup */
	for (i = 0 ; i < info->fix.smem_len; i += PAGE_SIZE) {
		page = fb_deferred_io_page(info, i);
		page->mapping = NULL;
	}

	info->fbdefio_state = NULL;
	info->fbops->fb_mmap = NULL;

	mutex_lock(&fbdefio_state->lock);
	fbdefio_state->info = NULL;
	mutex_unlock(&fbdefio_state->lock);

	fb_deferred_io_state_put(fbdefio_state);
}
EXPORT_SYMBOL_GPL(fb_deferred_io_cleanup);
