#ifndef _LINUX_STRING_H_
#define _LINUX_STRING_H_


#include <linux/compiler.h>	/* for inline */
#include <linux/types.h>	/* for size_t */
#include <linux/stddef.h>	/* for NULL */
#include <stdarg.h>
#include <uapi/linux/string.h>

extern char *strndup_user(const char __user *, long);
extern void *memdup_user(const void __user *, size_t);
extern void *vmemdup_user(const void __user *, size_t);
extern void *memdup_user_nul(const void __user *, size_t);

/*
 * Include machine specific inline routines
 */
#include <asm/string.h>

#ifndef __HAVE_ARCH_STRCPY
extern char * strcpy(char *,const char *);
#endif
#ifndef __HAVE_ARCH_STRNCPY
extern char * strncpy(char *,const char *, __kernel_size_t);
#endif
#ifndef __HAVE_ARCH_STRLCPY
size_t strlcpy(char *, const char *, size_t);
#endif
#ifndef __HAVE_ARCH_STRSCPY
ssize_t strscpy(char *, const char *, size_t);
#endif

/* Wraps calls to strscpy()/memset(), no arch specific code required */
ssize_t strscpy_pad(char *dest, const char *src, size_t count);

#ifndef __HAVE_ARCH_STRCAT
extern char * strcat(char *, const char *);
#endif
#ifndef __HAVE_ARCH_STRNCAT
extern char * strncat(char *, const char *, __kernel_size_t);
#endif
#ifndef __HAVE_ARCH_STRLCAT
extern size_t strlcat(char *, const char *, __kernel_size_t);
#endif
#ifndef __HAVE_ARCH_STRCMP
extern int strcmp(const char *,const char *);
#endif
#ifndef __HAVE_ARCH_STRNCMP
extern int strncmp(const char *,const char *,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_STRCASECMP
extern int strcasecmp(const char *s1, const char *s2);
#endif
#ifndef __HAVE_ARCH_STRNCASECMP
extern int strncasecmp(const char *s1, const char *s2, size_t n);
#endif
#ifndef __HAVE_ARCH_STRCHR
extern char * strchr(const char *,int);
#endif
#ifndef __HAVE_ARCH_STRCHRNUL
extern char * strchrnul(const char *,int);
#endif
#ifndef __HAVE_ARCH_STRNCHR
extern char * strnchr(const char *, size_t, int);
#endif
#ifndef __HAVE_ARCH_STRRCHR
extern char * strrchr(const char *,int);
#endif
extern char * __must_check skip_spaces(const char *);

extern char *strim(char *);

static inline __must_check char *strstrip(char *str)
{
	return strim(str);
}

#ifndef __HAVE_ARCH_STRSTR
extern char * strstr(const char *, const char *);
#endif
#ifndef __HAVE_ARCH_STRNSTR
extern char * strnstr(const char *, const char *, size_t);
#endif
#ifndef __HAVE_ARCH_STRLEN
extern __kernel_size_t strlen(const char *);
#endif
#ifndef __HAVE_ARCH_STRNLEN
extern __kernel_size_t strnlen(const char *,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_STRPBRK
extern char * strpbrk(const char *,const char *);
#endif
#ifndef __HAVE_ARCH_STRSEP
extern char * strsep(char **,const char *);
#endif
#ifndef __HAVE_ARCH_STRSPN
extern __kernel_size_t strspn(const char *,const char *);
#endif
#ifndef __HAVE_ARCH_STRCSPN
extern __kernel_size_t strcspn(const char *,const char *);
#endif

#ifndef __HAVE_ARCH_MEMSET
extern void * memset(void *,int,__kernel_size_t);
#endif

#ifndef __HAVE_ARCH_MEMSET16
extern void *memset16(uint16_t *, uint16_t, __kernel_size_t);
#endif

#ifndef __HAVE_ARCH_MEMSET32
extern void *memset32(uint32_t *, uint32_t, __kernel_size_t);
#endif

#ifndef __HAVE_ARCH_MEMSET64
extern void *memset64(uint64_t *, uint64_t, __kernel_size_t);
#endif

static inline void *memset_l(unsigned long *p, unsigned long v,
		__kernel_size_t n)
{
	if (BITS_PER_LONG == 32)
		return memset32((uint32_t *)p, v, n);
	else
		return memset64((uint64_t *)p, v, n);
}

static inline void *memset_p(void **p, void *v, __kernel_size_t n)
{
	if (BITS_PER_LONG == 32)
		return memset32((uint32_t *)p, (uintptr_t)v, n);
	else
		return memset64((uint64_t *)p, (uintptr_t)v, n);
}

#ifndef __HAVE_ARCH_MEMCPY
extern void * memcpy(void *,const void *,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_MEMMOVE
extern void * memmove(void *,const void *,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_MEMSCAN
extern void * memscan(void *,int,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_MEMCMP
extern int memcmp(const void *,const void *,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_MEMCHR
extern void * memchr(const void *,int,__kernel_size_t);
#endif
#ifndef __HAVE_ARCH_MEMCPY_MCSAFE
static inline __must_check unsigned long memcpy_mcsafe(void *dst,
		const void *src, size_t cnt)
{
	memcpy(dst, src, cnt);
	return 0;
}
#endif
#ifndef __HAVE_ARCH_MEMCPY_FLUSHCACHE
static inline void memcpy_flushcache(void *dst, const void *src, size_t cnt)
{
	memcpy(dst, src, cnt);
}
#endif
void *memchr_inv(const void *s, int c, size_t n);
char *strreplace(char *s, char old, char new);

extern void kfree_const(const void *x);

extern char *kstrdup(const char *s, gfp_t gfp) __malloc;
extern const char *kstrdup_const(const char *s, gfp_t gfp);
extern char *kstrndup(const char *s, size_t len, gfp_t gfp);
extern void *kmemdup(const void *src, size_t len, gfp_t gfp);

extern char **argv_split(gfp_t gfp, const char *str, int *argcp);
extern void argv_free(char **argv);

extern bool sysfs_streq(const char *s1, const char *s2);
extern int kstrtobool(const char *s, bool *res);
static inline int strtobool(const char *s, bool *res)
{
	return kstrtobool(s, res);
}

int match_string(const char * const *array, size_t n, const char *string);
int __sysfs_match_string(const char * const *array, size_t n, const char *s);

/**
 * sysfs_match_string - matches given string in an array
 * @_a: array of strings
 * @_s: string to match with
 *
 * Helper for __sysfs_match_string(). Calculates the size of @a automatically.
 */
#define sysfs_match_string(_a, _s) __sysfs_match_string(_a, ARRAY_SIZE(_a), _s)

#ifdef CONFIG_BINARY_PRINTF
int vbin_printf(u32 *bin_buf, size_t size, const char *fmt, va_list args);
int bstr_printf(char *buf, size_t size, const char *fmt, const u32 *bin_buf);
int bprintf(u32 *bin_buf, size_t size, const char *fmt, ...) __printf(3, 4);
#endif

extern ssize_t memory_read_from_buffer(void *to, size_t count, loff_t *ppos,
				       const void *from, size_t available);

/**
 * strstarts - does @str start with @prefix?
 * @str: string to examine
 * @prefix: prefix to look for.
 */
static inline bool strstarts(const char *str, const char *prefix)
{
	return strncmp(str, prefix, strlen(prefix)) == 0;
}

size_t memweight(const void *ptr, size_t bytes);
void memzero_explicit(void *s, size_t count);

/**
 * kbasename - return the last part of a pathname.
 *
 * @path: path to extract the filename from.
 */
static inline const char *kbasename(const char *path)
{
	const char *tail = strrchr(path, '/');
	return tail ? tail + 1 : path;
}

void __read_overflow3(void) __compiletime_error("detected read beyond size of object passed as 3rd parameter");

/**
 * memset_after - Set a value after a struct member to the end of a struct
 *
 * @obj: Address of target struct instance
 * @v: Byte value to repeatedly write
 * @member: after which struct member to start writing bytes
 *
 * This is good for clearing padding following the given member.
 */
#define memset_after(obj, v, member)					\
({									\
	u8 *__ptr = (u8 *)(obj);					\
	typeof(v) __val = (v);						\
	memset(__ptr + offsetofend(typeof(*(obj)), member), __val,	\
	       sizeof(*(obj)) - offsetofend(typeof(*(obj)), member));	\
})

/**
 * memcpy_and_pad - Copy one buffer to another with padding
 * @dest: Where to copy to
 * @dest_len: The destination buffer size
 * @src: Where to copy from
 * @count: The number of bytes to copy
 * @pad: Character to use for padding if space is left in destination.
 */
static inline void memcpy_and_pad(void *dest, size_t dest_len,
				  const void *src, size_t count, int pad)
{
	if (dest_len > count) {
		memcpy(dest, src, count);
		memset(dest + count, pad,  dest_len - count);
	} else
		memcpy(dest, src, dest_len);
}

/**
 * memtostr - Copy a possibly non-NUL-term string to a NUL-term string
 * @dest: Pointer to destination NUL-terminates string
 * @src: Pointer to character array (likely marked as __nonstring)
 *
 * This is a replacement for strncpy() uses where the source is not
 * a NUL-terminated string.
 *
 * Note that sizes of @dest and @src must be known at compile-time.
 */
#define memtostr(dest, src)	do {					\
	const size_t _dest_len = __builtin_object_size(dest, 1);	\
	const size_t _src_len = __builtin_object_size(src, 1);		\
	const size_t _src_chars = strnlen(src, _src_len);		\
	const size_t _copy_len = min(_dest_len - 1, _src_chars);	\
									\
	BUILD_BUG_ON(!__builtin_constant_p(_dest_len) ||		\
		     !__builtin_constant_p(_src_len) ||			\
		     _dest_len == 0 || _dest_len == (size_t)-1 ||	\
		     _src_len == 0 || _src_len == (size_t)-1);		\
	memcpy(dest, src, _copy_len);					\
	dest[_copy_len] = '\0';						\
} while (0)

/**
 * memtostr_pad - Copy a possibly non-NUL-term string to a NUL-term string
 *                with NUL padding in the destination
 * @dest: Pointer to destination NUL-terminates string
 * @src: Pointer to character array (likely marked as __nonstring)
 *
 * This is a replacement for strncpy() uses where the source is not
 * a NUL-terminated string.
 *
 * Note that sizes of @dest and @src must be known at compile-time.
 */
#define memtostr_pad(dest, src)		do {				\
	const size_t _dest_len = __builtin_object_size(dest, 1);	\
	const size_t _src_len = __builtin_object_size(src, 1);		\
	const size_t _src_chars = strnlen(src, _src_len);		\
	const size_t _copy_len = min(_dest_len - 1, _src_chars);	\
									\
	BUILD_BUG_ON(!__builtin_constant_p(_dest_len) ||		\
		     !__builtin_constant_p(_src_len) ||			\
		     _dest_len == 0 || _dest_len == (size_t)-1 ||	\
		     _src_len == 0 || _src_len == (size_t)-1);		\
	memcpy(dest, src, _copy_len);					\
	memset(&dest[_copy_len], 0, _dest_len - _copy_len);		\
} while (0)

#endif /* _LINUX_STRING_H_ */
