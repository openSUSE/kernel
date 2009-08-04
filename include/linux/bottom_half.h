#ifndef _LINUX_BH_H
#define _LINUX_BH_H

#ifdef CONFIG_PREEMPT_HARDIRQS
# define local_bh_disable()		do { } while (0)
# define __local_bh_disable(ip)		do { } while (0)
# define _local_bh_enable()		do { } while (0)
# define local_bh_enable()		do { } while (0)
# define local_bh_enable_ip(ip)		do { } while (0)
#else
extern void local_bh_disable(void);
extern void _local_bh_enable(void);
extern void local_bh_enable(void);
extern void local_bh_enable_ip(unsigned long ip);
#endif

#endif /* _LINUX_BH_H */
