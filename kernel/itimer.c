/*
 * linux/kernel/itimer.c
 *
 * Copyright (C) 1992 Darren Senn
 */

/* These are all the functions necessary to implement itimers */

#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/syscalls.h>
#include <linux/time.h>

#include <asm/uaccess.h>

static unsigned long it_real_value(struct signal_struct *sig)
{
	unsigned long val = 0;
	if (timer_pending(&sig->real_timer)) {
		val = sig->real_timer.expires - jiffies;

		/* look out for negative/zero itimer.. */
		if ((long) val <= 0)
			val = 1;
	}
	return val;
}

int do_getitimer(int which, struct itimerval *value)
{
	unsigned long interval, val;

	switch (which) {
	case ITIMER_REAL:
		spin_lock_irq(&current->sighand->siglock);
		interval = current->signal->it_real_incr;
		val = it_real_value(current->signal);
		spin_unlock_irq(&current->sighand->siglock);
		jiffies_to_timeval(val, &value->it_value);
		jiffies_to_timeval(interval, &value->it_interval);
		break;
	case ITIMER_VIRTUAL:
		cputime_to_timeval(current->it_virt_value, &value->it_value);
		cputime_to_timeval(current->it_virt_incr, &value->it_interval);
		break;
	case ITIMER_PROF:
		cputime_to_timeval(current->it_prof_value, &value->it_value);
		cputime_to_timeval(current->it_prof_incr, &value->it_interval);
		break;
	default:
		return(-EINVAL);
	}
	return 0;
}

asmlinkage long sys_getitimer(int which, struct itimerval __user *value)
{
	int error = -EFAULT;
	struct itimerval get_buffer;

	if (value) {
		error = do_getitimer(which, &get_buffer);
		if (!error &&
		    copy_to_user(value, &get_buffer, sizeof(get_buffer)))
			error = -EFAULT;
	}
	return error;
}

/*
 * Called with P->sighand->siglock held and P->signal->real_timer inactive.
 * If interval is nonzero, arm the timer for interval ticks from now.
 */
static inline void it_real_arm(struct task_struct *p, unsigned long interval)
{
	p->signal->it_real_value = interval; /* XXX unnecessary field?? */
	if (interval == 0)
		return;
	if (interval > (unsigned long) LONG_MAX)
		interval = LONG_MAX;
	p->signal->real_timer.expires = jiffies + interval;
	add_timer(&p->signal->real_timer);
}

void it_real_fn(unsigned long __data)
{
	struct task_struct * p = (struct task_struct *) __data;

	send_group_sig_info(SIGALRM, SEND_SIG_PRIV, p);

	/*
	 * Now restart the timer if necessary.  We don't need any locking
	 * here because do_setitimer makes sure we have finished running
	 * before it touches anything.
	 */
	it_real_arm(p, p->signal->it_real_incr);
}

int do_setitimer(int which, struct itimerval *value, struct itimerval *ovalue)
{
	struct task_struct *tsk = current;
 	unsigned long val, interval;
	cputime_t cputime;

	switch (which) {
		case ITIMER_REAL:
 			spin_lock_irq(&tsk->sighand->siglock);
 			interval = tsk->signal->it_real_incr;
 			val = it_real_value(tsk->signal);
 			if (val)
 				del_timer_sync(&tsk->signal->real_timer);
 			tsk->signal->it_real_incr =
				timeval_to_jiffies(&value->it_interval);
 			it_real_arm(tsk, timeval_to_jiffies(&value->it_value));
 			spin_unlock_irq(&tsk->sighand->siglock);
			if (ovalue) {
				jiffies_to_timeval(val, &ovalue->it_value);
				jiffies_to_timeval(interval,
						   &ovalue->it_interval);
			}
			break;
		case ITIMER_VIRTUAL:
			if (ovalue) {
				cputime_to_timeval(tsk->it_virt_value,
						   &ovalue->it_value);
				cputime_to_timeval(tsk->it_virt_incr,
						   &ovalue->it_interval);
			}
			cputime = timeval_to_cputime(&value->it_value);
			if (cputime_gt(cputime, cputime_zero))
				cputime = cputime_add(cputime,
						      jiffies_to_cputime(1));
			tsk->it_virt_value = cputime;
			cputime = timeval_to_cputime(&value->it_interval);
			tsk->it_virt_incr = cputime;
			break;
		case ITIMER_PROF:
			if (ovalue) {
				cputime_to_timeval(tsk->it_prof_value,
						   &ovalue->it_value);
				cputime_to_timeval(tsk->it_prof_incr,
						   &ovalue->it_interval);
			}
			cputime = timeval_to_cputime(&value->it_value);
			if (cputime_gt(cputime, cputime_zero))
				cputime = cputime_add(cputime,
						      jiffies_to_cputime(1));
			tsk->it_prof_value = cputime;
			cputime = timeval_to_cputime(&value->it_interval);
			tsk->it_prof_incr = cputime;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

asmlinkage long sys_setitimer(int which,
			      struct itimerval __user *value,
			      struct itimerval __user *ovalue)
{
	struct itimerval set_buffer, get_buffer;
	int error;

	if (value) {
		if(copy_from_user(&set_buffer, value, sizeof(set_buffer)))
			return -EFAULT;
	} else
		memset((char *) &set_buffer, 0, sizeof(set_buffer));

	error = do_setitimer(which, &set_buffer, ovalue ? &get_buffer : NULL);
	if (error || !ovalue)
		return error;

	if (copy_to_user(ovalue, &get_buffer, sizeof(get_buffer)))
		return -EFAULT; 
	return 0;
}
