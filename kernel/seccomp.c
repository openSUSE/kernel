/*
 * linux/kernel/seccomp.c
 *
 * Copyright 2004-2005  Andrea Arcangeli <andrea@cpushare.com>
 *
 * This defines a simple but solid secure-computing mode.
 */

#include <linux/seccomp.h>
#include <linux/sched.h>
#include <asm/unistd.h>
#ifdef TIF_IA32
#include <asm/ia32_unistd.h>
#endif

/* #define SECCOMP_DEBUG 1 */

/*
 * Secure computing mode 1 allows only read/write/exit/sigreturn.
 * To be fully secure this must be combined with rlimit
 * to limit the stack allocations too.
 */
static int mode1_syscalls[] = {
	__NR_read, __NR_write, __NR_exit,
	/*
	 * Allow either sigreturn or rt_sigreturn, newer archs
	 * like x86-64 only defines __NR_rt_sigreturn.
	 */
#ifdef __NR_sigreturn
	__NR_sigreturn,
#else
	__NR_rt_sigreturn,
#endif
	0, /* null terminated */
};

#ifdef TIF_IA32
static int mode1_syscalls_32bit[] = {
	__NR_ia32_read, __NR_ia32_write, __NR_ia32_exit,
	/*
	 * Allow either sigreturn or rt_sigreturn, newer archs
	 * like x86-64 only defines __NR_rt_sigreturn.
	 */
	__NR_ia32_sigreturn,
	0, /* null terminated */
};
#endif

void __secure_computing(int this_syscall)
{
	int mode = current->seccomp.mode;
	int * syscall;

	switch (mode) {
	case 1:
		syscall = mode1_syscalls;
#ifdef TIF_IA32
		if (test_thread_flag(TIF_IA32))
			syscall = mode1_syscalls_32bit;
#endif
		do {
			if (*syscall == this_syscall)
				return;
		} while (*++syscall);
		break;
	default:
		BUG();
	}

#ifdef SECCOMP_DEBUG
	dump_stack();
#endif
	do_exit(SIGKILL);
}
