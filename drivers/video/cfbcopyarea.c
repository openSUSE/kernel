/*
 *  Generic function for frame buffer with packed pixels of any depth.
 *
 *      Copyright (C)  June 1999 James Simmons
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 * NOTES:
 *
 *  This is for cfb packed pixels. Iplan and such are incorporated in the
 *  drivers that need them.
 *
 *  FIXME
 *
 *  Also need to add code to deal with cards endians that are different than
 *  the native cpu endians. I also need to deal with MSB position in the word.
 *
 *  The two functions or copying forward and backward could be split up like
 *  the ones for filling, i.e. in aligned and unaligned versions. This would
 *  help moving some redundant computations and branches out of the loop, too.
 */



#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <linux/slab.h>
#include <asm/types.h>
#include <asm/io.h>

#define LONG_MASK  (BITS_PER_LONG - 1)

#if BITS_PER_LONG == 32
#  define FB_WRITEL fb_writel
#  define FB_READL  fb_readl
#  define SHIFT_PER_LONG 5
#  define BYTES_PER_LONG 4
#else
#  define FB_WRITEL fb_writeq
#  define FB_READL  fb_readq
#  define SHIFT_PER_LONG 6
#  define BYTES_PER_LONG 8
#endif

    /*
     *  Compose two values, using a bitmask as decision value
     *  This is equivalent to (a & mask) | (b & ~mask)
     */

static inline unsigned long
comp(unsigned long a, unsigned long b, unsigned long mask)
{
    return ((a ^ b) & mask) ^ b;
}

    /*
     *  Generic bitwise copy algorithm
     */

static void
bitcpy(unsigned long __iomem *dst, int dst_idx,
       const unsigned long __iomem *src, int src_idx,
       unsigned n)
{
	unsigned long first, last;
	int const shift = dst_idx-src_idx;
	int left, right;

	first = ~0UL >> dst_idx;
	last = ~(~0UL >> ((dst_idx+n) % BITS_PER_LONG));

	if (!shift) {
		// Same alignment for source and dest

		if (dst_idx+n <= BITS_PER_LONG) {
			// Single word
			if (last)
				first &= last;
			FB_WRITEL( comp( FB_READL(src), FB_READL(dst), first), dst);
		} else {
			// Multiple destination words

			// Leading bits
			if (first != ~0UL) {
				FB_WRITEL( comp( FB_READL(src), FB_READL(dst), first), dst);
				dst++;
				src++;
				n -= BITS_PER_LONG-dst_idx;
			}

			// Main chunk
			n /= BITS_PER_LONG;
			while (n >= 8) {
				FB_WRITEL(FB_READL(src++), dst++);
				FB_WRITEL(FB_READL(src++), dst++);
				FB_WRITEL(FB_READL(src++), dst++);
				FB_WRITEL(FB_READL(src++), dst++);
				FB_WRITEL(FB_READL(src++), dst++);
				FB_WRITEL(FB_READL(src++), dst++);
				FB_WRITEL(FB_READL(src++), dst++);
				FB_WRITEL(FB_READL(src++), dst++);
				n -= 8;
			}
			while (n--)
				FB_WRITEL(FB_READL(src++), dst++);

			// Trailing bits
			if (last)
				FB_WRITEL( comp( FB_READL(src), FB_READL(dst), last), dst);
		}
	} else {
		unsigned long d0, d1;
		int m;
		// Different alignment for source and dest

		right = shift & (BITS_PER_LONG-1);
		left = -shift & (BITS_PER_LONG-1);

		if (dst_idx+n <= BITS_PER_LONG) {
			// Single destination word
			if (last)
				first &= last;
			if (shift > 0) {
				// Single source word
				FB_WRITEL( comp( FB_READL(src) >> right, FB_READL(dst), first), dst);
			} else if (src_idx+n <= BITS_PER_LONG) {
				// Single source word
				FB_WRITEL( comp(FB_READL(src) << left, FB_READL(dst), first), dst);
			} else {
				// 2 source words
				d0 = FB_READL(src++);
				d1 = FB_READL(src);
				FB_WRITEL( comp(d0<<left | d1>>right, FB_READL(dst), first), dst);
			}
		} else {
			// Multiple destination words
			/** We must always remember the last value read, because in case
			SRC and DST overlap bitwise (e.g. when moving just one pixel in
			1bpp), we always collect one full long for DST and that might
			overlap with the current long from SRC. We store this value in
			'd0'. */
			d0 = FB_READL(src++);
			// Leading bits
			if (shift > 0) {
				// Single source word
				FB_WRITEL( comp(d0 >> right, FB_READL(dst), first), dst);
				dst++;
				n -= BITS_PER_LONG-dst_idx;
			} else {
				// 2 source words
				d1 = FB_READL(src++);
				FB_WRITEL( comp(d0<<left | d1>>right, FB_READL(dst), first), dst);
				d0 = d1;
				dst++;
				n -= BITS_PER_LONG-dst_idx;
			}

			// Main chunk
			m = n % BITS_PER_LONG;
			n /= BITS_PER_LONG;
			while (n >= 4) {
				d1 = FB_READL(src++);
				FB_WRITEL(d0 << left | d1 >> right, dst++);
				d0 = d1;
				d1 = FB_READL(src++);
				FB_WRITEL(d0 << left | d1 >> right, dst++);
				d0 = d1;
				d1 = FB_READL(src++);
				FB_WRITEL(d0 << left | d1 >> right, dst++);
				d0 = d1;
				d1 = FB_READL(src++);
				FB_WRITEL(d0 << left | d1 >> right, dst++);
				d0 = d1;
				n -= 4;
			}
			while (n--) {
				d1 = FB_READL(src++);
				FB_WRITEL(d0 << left | d1 >> right, dst++);
				d0 = d1;
			}

			// Trailing bits
			if (last) {
				if (m <= right) {
					// Single source word
					FB_WRITEL( comp(d0 << left, FB_READL(dst), last), dst);
				} else {
					// 2 source words
					d1 = FB_READL(src);
					FB_WRITEL( comp(d0<<left | d1>>right, FB_READL(dst), last), dst);
				}
			}
		}
	}
}

    /*
     *  Generic bitwise copy algorithm, operating backward
     */

static void
bitcpy_rev(unsigned long __iomem *dst, int dst_idx,
           const unsigned long __iomem *src, int src_idx,
           unsigned n)
{
	unsigned long first, last;
	int shift;

	dst += (n-1)/BITS_PER_LONG;
	src += (n-1)/BITS_PER_LONG;
	if ((n-1) % BITS_PER_LONG) {
		dst_idx += (n-1) % BITS_PER_LONG;
		dst += dst_idx >> SHIFT_PER_LONG;
		dst_idx &= BITS_PER_LONG-1;
		src_idx += (n-1) % BITS_PER_LONG;
		src += src_idx >> SHIFT_PER_LONG;
		src_idx &= BITS_PER_LONG-1;
	}

	shift = dst_idx-src_idx;

	first = ~0UL << (BITS_PER_LONG-1-dst_idx);
	last = ~(~0UL << (BITS_PER_LONG-1-((dst_idx-n) % BITS_PER_LONG)));

	if (!shift) {
		// Same alignment for source and dest

		if ((unsigned long)dst_idx+1 >= n) {
			// Single word
			if (last)
				first &= last;
			FB_WRITEL( comp( FB_READL(src), FB_READL(dst), first), dst);
		} else {
			// Multiple destination words

			// Leading bits
			if (first != ~0UL) {
				FB_WRITEL( comp( FB_READL(src), FB_READL(dst), first), dst);
				dst--;
				src--;
				n -= dst_idx+1;
			}

			// Main chunk
			n /= BITS_PER_LONG;
			while (n >= 8) {
				FB_WRITEL(FB_READL(src--), dst--);
				FB_WRITEL(FB_READL(src--), dst--);
				FB_WRITEL(FB_READL(src--), dst--);
				FB_WRITEL(FB_READL(src--), dst--);
				FB_WRITEL(FB_READL(src--), dst--);
				FB_WRITEL(FB_READL(src--), dst--);
				FB_WRITEL(FB_READL(src--), dst--);
				FB_WRITEL(FB_READL(src--), dst--);
				n -= 8;
			}
			while (n--)
				FB_WRITEL(FB_READL(src--), dst--);

			// Trailing bits
			if (last)
				FB_WRITEL( comp( FB_READL(src), FB_READL(dst), last), dst);
		}
	} else {
		// Different alignment for source and dest

		int const left = -shift & (BITS_PER_LONG-1);
		int const right = shift & (BITS_PER_LONG-1);

		if ((unsigned long)dst_idx+1 >= n) {
			// Single destination word
			if (last)
				first &= last;
			if (shift < 0) {
				// Single source word
				FB_WRITEL( comp( FB_READL(src)<<left, FB_READL(dst), first), dst);
			} else if (1+(unsigned long)src_idx >= n) {
				// Single source word
				FB_WRITEL( comp( FB_READL(src)>>right, FB_READL(dst), first), dst);
			} else {
				// 2 source words
				FB_WRITEL( comp( (FB_READL(src)>>right | FB_READL(src-1)<<left), FB_READL(dst), first), dst);
			}
		} else {
			// Multiple destination words
			/** We must always remember the last value read, because in case
			SRC and DST overlap bitwise (e.g. when moving just one pixel in
			1bpp), we always collect one full long for DST and that might
			overlap with the current long from SRC. We store this value in
			'd0'. */
			unsigned long d0, d1;
			int m;

			d0 = FB_READL(src--);
			// Leading bits
			if (shift < 0) {
				// Single source word
				FB_WRITEL( comp( (d0 << left), FB_READL(dst), first), dst);
			} else {
				// 2 source words
				d1 = FB_READL(src--);
				FB_WRITEL( comp( (d0>>right | d1<<left), FB_READL(dst), first), dst);
				d0 = d1;
			}
			dst--;
			n -= dst_idx+1;

			// Main chunk
			m = n % BITS_PER_LONG;
			n /= BITS_PER_LONG;
			while (n >= 4) {
				d1 = FB_READL(src--);
				FB_WRITEL(d0 >> right | d1 << left, dst--);
				d0 = d1;
				d1 = FB_READL(src--);
				FB_WRITEL(d0 >> right | d1 << left, dst--);
				d0 = d1;
				d1 = FB_READL(src--);
				FB_WRITEL(d0 >> right | d1 << left, dst--);
				d0 = d1;
				d1 = FB_READL(src--);
				FB_WRITEL(d0 >> right | d1 << left, dst--);
				d0 = d1;
				n -= 4;
			}
			while (n--) {
				d1 = FB_READL(src--);
				FB_WRITEL(d0 >> right | d1 << left, dst--);
				d0 = d1;
			}

			// Trailing bits
			if (last) {
				if (m <= left) {
					// Single source word
					FB_WRITEL( comp(d0 >> right, FB_READL(dst), last), dst);
				} else {
					// 2 source words
					d1 = FB_READL(src);
					FB_WRITEL( comp(d0>>right | d1<<left, FB_READL(dst), last), dst);
				}
			}
		}
	}
}

void cfb_copyarea(struct fb_info *p, const struct fb_copyarea *area)
{
	u32 dx = area->dx, dy = area->dy, sx = area->sx, sy = area->sy;
	u32 height = area->height, width = area->width;
	int x2, y2, vxres, vyres;
	unsigned long const bits_per_line = p->fix.line_length*8u;
	int dst_idx = 0, src_idx = 0, rev_copy = 0;
	unsigned long __iomem *dst = NULL, *src = NULL;

	if (p->state != FBINFO_STATE_RUNNING)
		return;

	/* We want rotation but lack hardware to do it for us. */
	if (!p->fbops->fb_rotate && p->var.rotate) {
	}

	vxres = p->var.xres_virtual;
	vyres = p->var.yres_virtual;

	if (area->dx > vxres || area->sx > vxres ||
	    area->dy > vyres || area->sy > vyres)
		return;

	/* clip the destination
	 * We could use hardware clipping but on many cards you get around
	 * hardware clipping by writing to framebuffer directly.
	 */
	x2 = area->dx + area->width;
	y2 = area->dy + area->height;
	dx = area->dx > 0 ? area->dx : 0;
	dy = area->dy > 0 ? area->dy : 0;
	x2 = x2 < vxres ? x2 : vxres;
	y2 = y2 < vyres ? y2 : vyres;
	width = x2 - dx;
	height = y2 - dy;

	if ((width==0)
	  ||(height==0))
		return;

	/* update sx1,sy1 */
	sx += (dx - area->dx);
	sy += (dy - area->dy);

	/* the source must be completely inside the virtual screen */
	if (sx < 0 || sy < 0 ||
	    (sx + width) > vxres ||
	    (sy + height) > vyres)
		return;

	/* if the beginning of the target area might overlap with the end of
	the source area, be have to copy the area reverse. */
	if ((dy == sy && dx > sx) ||
	    (dy > sy)) {
		dy += height;
		sy += height;
		rev_copy = 1;
	}

	// split the base of the framebuffer into a long-aligned address and the
	// index of the first bit
	dst = src = (unsigned long __iomem *)((unsigned long)p->screen_base &
				      ~(BYTES_PER_LONG-1));
	dst_idx = src_idx = 8*((unsigned long)p->screen_base & (BYTES_PER_LONG-1));
	// add offset of source and target area
	dst_idx += dy*bits_per_line + dx*p->var.bits_per_pixel;
	src_idx += sy*bits_per_line + sx*p->var.bits_per_pixel;

	if (p->fbops->fb_sync)
		p->fbops->fb_sync(p);

	if (rev_copy) {
		while (height--) {
			dst_idx -= bits_per_line;
			src_idx -= bits_per_line;
			dst += dst_idx >> SHIFT_PER_LONG;
			dst_idx &= LONG_MASK;
			src += src_idx >> SHIFT_PER_LONG;
			src_idx &= LONG_MASK;
			bitcpy_rev(dst, dst_idx, src, src_idx,
				   width*p->var.bits_per_pixel);
		}
	} else {
		while (height--) {
			dst += dst_idx >> SHIFT_PER_LONG;
			dst_idx &= LONG_MASK;
			src += src_idx >> SHIFT_PER_LONG;
			src_idx &= LONG_MASK;
			bitcpy(dst, dst_idx, src, src_idx,
			       width*p->var.bits_per_pixel);
			dst_idx += bits_per_line;
			src_idx += bits_per_line;
		}
	}
}
#undef CFB_DEBUG
#ifdef CFB_DEBUG
/** all this init-function does is to perform a few unittests.
The idea it always to invoke the function to test on a predefined bitmap and
compare the results to the expected output.
TODO:
 - this currently only tests bitcpy_rev, as that was the only one giving me trouble
 - this assumes 32 bit longs
 - not sure about endianess, I only tested this on a 32 bit MIPS little endian system
 - could reuse testcases to test forward copying, too, just reverse the operation
*/
int __init cfb_copyarea_init(void)
{
	char const* comment = 0;
	printk( KERN_INFO "cfb_copyarea_init()\n");
	{
		comment = "copy a single u32, source and target u32-aligned";
		u32 tmp[] =          { 0xaaaaaaaau, 0x55555555u, 0xffffffffu, 0x00000000u };
		u32 const expect[] = { 0xaaaaaaaau, 0xaaaaaaaau, 0xffffffffu, 0x00000000u };

		bitcpy_rev( tmp, 0, tmp+1, 0, 32);

		if( 0!=memcmp( expect, tmp, sizeof tmp))
			goto error;
	}

	{
		comment = "copy a single u32, source u32-aligned";
		u32 tmp[] =          { 0x11112222u, 0x33334444u, 0x55556666u, 0x77778888u };
		u32 const expect[] = { 0x11112222u, 0x22224444u, 0x55551111u, 0x77778888u };

		bitcpy_rev( tmp, 0, tmp+1, 16, 32);

		if( 0!=memcmp( expect, tmp, sizeof tmp))
			goto error;
	}

	{
		comment = "copy a single u32, target u32-aligned";
		u32 tmp[] =          { 0x11112222u, 0x33334444u, 0x55556666u, 0x77778888u };
		u32 const expect[] = { 0x11112222u, 0x33334444u, 0x44441111u, 0x77778888u };

		bitcpy_rev( tmp, 16, tmp+2, 0, 32);

		if( 0!=memcmp( expect, tmp, sizeof tmp))
			goto error;
	}

	{
		comment = "copy two u32, source and target u32-aligned";
		u32 tmp[] =          { 0xaaaaaaaau, 0x55555555u, 0xffffffffu, 0x00000000u };
		u32 const expect[] = { 0xaaaaaaaau, 0xaaaaaaaau, 0x55555555u, 0x00000000u };

		bitcpy_rev( tmp, 0, tmp+1, 0, 64);

		if( 0!=memcmp( expect, tmp, sizeof tmp))
			goto error;
	}

	return 0;

error:
	printk( KERN_ERR " framebuffer self-test(%s) failed\n", comment);
	return -1;
}
module_init(cfb_copyarea_init);
#endif

EXPORT_SYMBOL(cfb_copyarea);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software accelerated copyarea");
MODULE_LICENSE("GPL");

