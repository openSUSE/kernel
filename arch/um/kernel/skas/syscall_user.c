/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <signal.h>
#include "kern_util.h"
#include "syscall_user.h"
#include "sysdep/ptrace.h"
#include "sysdep/sigcontext.h"
#include "skas.h"

void handle_syscall(union uml_pt_regs *regs)
{
	long result;

	syscall_trace(regs, 0);
	result = execute_syscall_skas(regs);

	REGS_SET_SYSCALL_RETURN(regs->skas.regs, result);

	syscall_trace(regs, 1);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
