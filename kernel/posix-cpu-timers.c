/*
 * Implement CPU time clocks for the POSIX clock interface.
 */

#include <linux/sched.h>
#include <linux/posix-timers.h>
#include <asm/uaccess.h>
#include <linux/errno.h>

union cpu_time_count {
	cputime_t cpu;
	unsigned long long sched;
};

static int check_clock(clockid_t which_clock)
{
	int error = 0;
	struct task_struct *p;
	const pid_t pid = CPUCLOCK_PID(which_clock);

	if (CPUCLOCK_WHICH(which_clock) >= CPUCLOCK_MAX)
		return -EINVAL;

	if (pid == 0)
		return 0;

	read_lock(&tasklist_lock);
	p = find_task_by_pid(pid);
	if (!p || (CPUCLOCK_PERTHREAD(which_clock) ?
		   p->tgid != current->tgid : p->tgid != pid)) {
		error = -EINVAL;
	}
	read_unlock(&tasklist_lock);

	return error;
}

static void sample_to_timespec(clockid_t which_clock,
			       union cpu_time_count cpu,
			       struct timespec *tp)
{
	if (CPUCLOCK_WHICH(which_clock) == CPUCLOCK_SCHED) {
		tp->tv_sec = div_long_long_rem(cpu.sched,
					       NSEC_PER_SEC, &tp->tv_nsec);
	} else {
		cputime_to_timespec(cpu.cpu, tp);
	}
}

static inline cputime_t prof_ticks(struct task_struct *p)
{
	return cputime_add(p->utime, p->stime);
}
static inline cputime_t virt_ticks(struct task_struct *p)
{
	return p->utime;
}
static inline unsigned long long sched_ns(struct task_struct *p)
{
	return (p == current) ? current_sched_time(p) : p->sched_time;
}

int posix_cpu_clock_getres(clockid_t which_clock, struct timespec *tp)
{
	int error = check_clock(which_clock);
	if (!error) {
		tp->tv_sec = 0;
		tp->tv_nsec = ((NSEC_PER_SEC + HZ - 1) / HZ);
		if (CPUCLOCK_WHICH(which_clock) == CPUCLOCK_SCHED) {
			/*
			 * If sched_clock is using a cycle counter, we
			 * don't have any idea of its true resolution
			 * exported, but it is much more than 1s/HZ.
			 */
			tp->tv_nsec = 1;
		}
	}
	return error;
}

int posix_cpu_clock_set(clockid_t which_clock, const struct timespec *tp)
{
	/*
	 * You can never reset a CPU clock, but we check for other errors
	 * in the call before failing with EPERM.
	 */
	int error = check_clock(which_clock);
	if (error == 0) {
		error = -EPERM;
	}
	return error;
}


/*
 * Sample a per-thread clock for the given task.
 */
static int cpu_clock_sample(clockid_t which_clock, struct task_struct *p,
			    union cpu_time_count *cpu)
{
	switch (CPUCLOCK_WHICH(which_clock)) {
	default:
		return -EINVAL;
	case CPUCLOCK_PROF:
		cpu->cpu = prof_ticks(p);
		break;
	case CPUCLOCK_VIRT:
		cpu->cpu = virt_ticks(p);
		break;
	case CPUCLOCK_SCHED:
		cpu->sched = sched_ns(p);
		break;
	}
	return 0;
}

/*
 * Sample a process (thread group) clock for the given group_leader task.
 * Must be called with tasklist_lock held for reading.
 */
static int cpu_clock_sample_group(clockid_t which_clock,
				  struct task_struct *p,
				  union cpu_time_count *cpu)
{
	struct task_struct *t = p;
	unsigned long flags;
	switch (CPUCLOCK_WHICH(which_clock)) {
	default:
		return -EINVAL;
	case CPUCLOCK_PROF:
		spin_lock_irqsave(&p->sighand->siglock, flags);
		cpu->cpu = cputime_add(p->signal->utime, p->signal->stime);
		do {
			cpu->cpu = cputime_add(cpu->cpu, prof_ticks(t));
			t = next_thread(t);
		} while (t != p);
		spin_unlock_irqrestore(&p->sighand->siglock, flags);
		break;
	case CPUCLOCK_VIRT:
		spin_lock_irqsave(&p->sighand->siglock, flags);
		cpu->cpu = p->signal->utime;
		do {
			cpu->cpu = cputime_add(cpu->cpu, virt_ticks(t));
			t = next_thread(t);
		} while (t != p);
		spin_unlock_irqrestore(&p->sighand->siglock, flags);
		break;
	case CPUCLOCK_SCHED:
		spin_lock_irqsave(&p->sighand->siglock, flags);
		cpu->sched = p->signal->sched_time;
		/* Add in each other live thread.  */
		while ((t = next_thread(t)) != p) {
			cpu->sched += t->sched_time;
		}
		if (p->tgid == current->tgid) {
			/*
			 * We're sampling ourselves, so include the
			 * cycles not yet banked.  We still omit
			 * other threads running on other CPUs,
			 * so the total can always be behind as
			 * much as max(nthreads-1,ncpus) * (NSEC_PER_SEC/HZ).
			 */
			cpu->sched += current_sched_time(current);
		} else {
			cpu->sched += p->sched_time;
		}
		spin_unlock_irqrestore(&p->sighand->siglock, flags);
		break;
	}
	return 0;
}


int posix_cpu_clock_get(clockid_t which_clock, struct timespec *tp)
{
	const pid_t pid = CPUCLOCK_PID(which_clock);
	int error = -EINVAL;
	union cpu_time_count rtn;

	if (pid == 0) {
		/*
		 * Special case constant value for our own clocks.
		 * We don't have to do any lookup to find ourselves.
		 */
		if (CPUCLOCK_PERTHREAD(which_clock)) {
			/*
			 * Sampling just ourselves we can do with no locking.
			 */
			error = cpu_clock_sample(which_clock,
						 current, &rtn);
		} else {
			read_lock(&tasklist_lock);
			error = cpu_clock_sample_group(which_clock,
						       current, &rtn);
			read_unlock(&tasklist_lock);
		}
	} else {
		/*
		 * Find the given PID, and validate that the caller
		 * should be able to see it.
		 */
		struct task_struct *p;
		read_lock(&tasklist_lock);
		p = find_task_by_pid(pid);
		if (p) {
			if (CPUCLOCK_PERTHREAD(which_clock)) {
				if (p->tgid == current->tgid) {
					error = cpu_clock_sample(which_clock,
								 p, &rtn);
				}
			} else if (p->tgid == pid && p->signal) {
				error = cpu_clock_sample_group(which_clock,
							       p, &rtn);
			}
		}
		read_unlock(&tasklist_lock);
	}

	if (error)
		return error;
	sample_to_timespec(which_clock, rtn, tp);
	return 0;
}

/*
 * These can't be called, since timer_create never works.
 */
int posix_cpu_timer_set(struct k_itimer *timer, int flags,
			struct itimerspec *old, struct itimerspec *new)
{
	BUG();
	return -EINVAL;
}
int posix_cpu_timer_del(struct k_itimer *timer)
{
	BUG();
	return -EINVAL;
}
void posix_cpu_timer_get(struct k_itimer *timer, struct itimerspec *spec)
{
	BUG();
}


#define PROCESS_CLOCK	MAKE_PROCESS_CPUCLOCK(0, CPUCLOCK_SCHED)
#define THREAD_CLOCK	MAKE_THREAD_CPUCLOCK(0, CPUCLOCK_SCHED)

static int process_cpu_clock_getres(clockid_t which_clock, struct timespec *tp)
{
	return posix_cpu_clock_getres(PROCESS_CLOCK, tp);
}
static int process_cpu_clock_get(clockid_t which_clock, struct timespec *tp)
{
	return posix_cpu_clock_get(PROCESS_CLOCK, tp);
}
static int thread_cpu_clock_getres(clockid_t which_clock, struct timespec *tp)
{
	return posix_cpu_clock_getres(THREAD_CLOCK, tp);
}
static int thread_cpu_clock_get(clockid_t which_clock, struct timespec *tp)
{
	return posix_cpu_clock_get(THREAD_CLOCK, tp);
}


static __init int init_posix_cpu_timers(void)
{
	struct k_clock process = {
		.clock_getres = process_cpu_clock_getres,
		.clock_get = process_cpu_clock_get,
		.clock_set = do_posix_clock_nosettime,
		.timer_create = do_posix_clock_notimer_create,
		.nsleep = do_posix_clock_nonanosleep,
	};
	struct k_clock thread = {
		.clock_getres = thread_cpu_clock_getres,
		.clock_get = thread_cpu_clock_get,
		.clock_set = do_posix_clock_nosettime,
		.timer_create = do_posix_clock_notimer_create,
		.nsleep = do_posix_clock_nonanosleep,
	};

	register_posix_clock(CLOCK_PROCESS_CPUTIME_ID, &process);
	register_posix_clock(CLOCK_THREAD_CPUTIME_ID, &thread);

	return 0;
}
__initcall(init_posix_cpu_timers);
