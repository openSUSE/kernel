/*
 * lib/kernel_lock.c
 *
 * This is the traditional BKL - big kernel lock. Largely
 * relegated to obsolescence, but used by various less
 * important (or lazy) subsystems.
 */
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/mutex.h>
#include <linux/smp_lock.h>

#define CREATE_TRACE_POINTS
#include <trace/events/bkl.h>

/*
 * The 'big kernel semaphore'
 *
 * This mutex is taken and released recursively by lock_kernel()
 * and unlock_kernel().  It is transparently dropped and reacquired
 * over schedule().  It is used to protect legacy code that hasn't
 * been migrated to a proper locking design yet.
 *
 * Note: code locked by this semaphore will only be serialized against
 * other code using the same locking facility. The code guarantees that
 * the task remains on the same CPU.
 *
 * Don't use in new code.
 */
DEFINE_MUTEX(kernel_sem);

/*
 * Re-acquire the kernel semaphore.
 *
 * This function is called with preemption off.
 *
 * We are executing in schedule() so the code must be extremely careful
 * about recursion, both due to the down() and due to the enabling of
 * preemption. schedule() will re-check the preemption flag after
 * reacquiring the semaphore.
 *
 * Called with interrupts disabled.
 */
int __lockfunc __reacquire_kernel_lock(void)
{
	int saved_lock_depth = current->lock_depth;

	BUG_ON(saved_lock_depth < 0);

	current->lock_depth = -1;
	local_irq_enable();

	mutex_lock(&kernel_sem);

	local_irq_disable();
	current->lock_depth = saved_lock_depth;

	return 0;
}

void __lockfunc __release_kernel_lock(void)
{
	mutex_unlock(&kernel_sem);
}

/*
 * Getting the big kernel semaphore.
 */
void __lockfunc _lock_kernel(const char *func, const char *file, int line)
{
	int depth = current->lock_depth + 1;

	trace_lock_kernel(func, file, line);

	if (likely(!depth)) {
		might_sleep();
		/*
		 * No recursion worries - we set up lock_depth _after_
		 */
		mutex_lock(&kernel_sem);
#ifdef CONFIG_DEBUG_RT_MUTEXES
		current->last_kernel_lock = __builtin_return_address(0);
#endif
	}

	current->lock_depth = depth;
}

void __lockfunc _unlock_kernel(const char *func, const char *file, int line)
{
	BUG_ON(current->lock_depth < 0);

	if (likely(--current->lock_depth < 0)) {
#ifdef CONFIG_DEBUG_RT_MUTEXES
		current->last_kernel_lock = NULL;
#endif
		mutex_unlock(&kernel_sem);
	}
	trace_unlock_kernel(func, file, line);
}

EXPORT_SYMBOL(_lock_kernel);
EXPORT_SYMBOL(_unlock_kernel);

