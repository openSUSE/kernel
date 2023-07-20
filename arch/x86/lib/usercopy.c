/*
 * User address space access functions.
 *
 *  For licencing details see kernel-base/COPYING
 */

#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/nospec.h>

/*
 * best effort, GUP based copy_from_user() that is NMI-safe
 */
unsigned long
copy_from_user_nmi(void *to, const void __user *from, unsigned long n)
{
	unsigned long offset, addr = (unsigned long)from;
	unsigned long size, len = 0;
	struct page *page;
	void *map;
	int ret;

	do {
		ret = __get_user_pages_fast(addr, 1, 0, &page);
		if (!ret)
			break;

		offset = addr & (PAGE_SIZE - 1);
		size = min(PAGE_SIZE - offset, n - len);

		map = kmap_atomic(page);
		memcpy(to, map+offset, size);
		kunmap_atomic(map);
		put_page(page);

		len  += size;
		to   += size;
		addr += size;

	} while (len < n);

	return len;
}
EXPORT_SYMBOL_GPL(copy_from_user_nmi);

/**
 * copy_to_user: - Copy a block of data into user space.
 * @to:   Destination address, in user space.
 * @from: Source address, in kernel space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Copy data from kernel space to user space.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 */
unsigned long _copy_to_user(void __user *to, const void *from, unsigned n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		n = __copy_to_user(to, from, n);
	return n;
}
EXPORT_SYMBOL(_copy_to_user);

/**
 * copy_from_user: - Copy a block of data from user space.
 * @to:   Destination address, in kernel space.
 * @from: Source address, in user space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Copy data from user space to kernel space.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 *
 * If some data could not be copied, this function will pad the copied
 * data to the requested size using zero bytes.
 */
unsigned long _copy_from_user(void *to, const void __user *from, unsigned n)
{
	if (access_ok(VERIFY_READ, from, n)) {
		/*
		 * Ensure that bad access_ok() speculation will not
		 * lead to nasty side effects *after* the copy is
		 * finished:
		 */
		barrier_nospec();
		n = __copy_from_user(to, from, n);
	} else
		memset(to, 0, n);
	return n;
}
EXPORT_SYMBOL(_copy_from_user);
