/*
 * drivers/base/cpu.c - basic CPU class support
 */

#include <linux/sysdev.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/device.h>
#include <linux/node.h>
#include <linux/gfp.h>

#include "base.h"

static struct sysdev_class_attribute *cpu_sysdev_class_attrs[];

struct sysdev_class cpu_sysdev_class = {
	.name = "cpu",
	.attrs = cpu_sysdev_class_attrs,
};
EXPORT_SYMBOL(cpu_sysdev_class);

static DEFINE_PER_CPU(struct sys_device *, cpu_sys_devices);

#ifdef CONFIG_HOTPLUG_CPU
static ssize_t show_online(struct sys_device *dev, struct sysdev_attribute *attr,
			   char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, sysdev);

	return sprintf(buf, "%u\n", !!cpu_online(cpu->sysdev.id));
}

static ssize_t __ref store_online(struct sys_device *dev, struct sysdev_attribute *attr,
				 const char *buf, size_t count)
{
	struct cpu *cpu = container_of(dev, struct cpu, sysdev);
	ssize_t ret;

	cpu_hotplug_driver_lock();
	switch (buf[0]) {
	case '0':
		ret = cpu_down(cpu->sysdev.id);
		if (!ret)
			kobject_uevent(&dev->kobj, KOBJ_OFFLINE);
		break;
	case '1':
		ret = cpu_up(cpu->sysdev.id);
		if (!ret)
			kobject_uevent(&dev->kobj, KOBJ_ONLINE);
		break;
	default:
		ret = -EINVAL;
	}
	cpu_hotplug_driver_unlock();

	if (ret >= 0)
		ret = count;
	return ret;
}
static SYSDEV_ATTR(online, 0644, show_online, store_online);

static void __cpuinit register_cpu_control(struct cpu *cpu)
{
	sysdev_create_file(&cpu->sysdev, &attr_online);
}
void unregister_cpu(struct cpu *cpu)
{
	int logical_cpu = cpu->sysdev.id;

	unregister_cpu_under_node(logical_cpu, cpu_to_node(logical_cpu));

	sysdev_remove_file(&cpu->sysdev, &attr_online);

	sysdev_unregister(&cpu->sysdev);
	per_cpu(cpu_sys_devices, logical_cpu) = NULL;
	return;
}

#ifdef CONFIG_ARCH_CPU_PROBE_RELEASE
static ssize_t cpu_probe_store(struct sysdev_class *class,
			       struct sysdev_class_attribute *attr,
			       const char *buf,
			       size_t count)
{
	return arch_cpu_probe(buf, count);
}

static ssize_t cpu_release_store(struct sysdev_class *class,
				 struct sysdev_class_attribute *attr,
				 const char *buf,
				 size_t count)
{
	return arch_cpu_release(buf, count);
}

static SYSDEV_CLASS_ATTR(probe, S_IWUSR, NULL, cpu_probe_store);
static SYSDEV_CLASS_ATTR(release, S_IWUSR, NULL, cpu_release_store);
#endif /* CONFIG_ARCH_CPU_PROBE_RELEASE */

#else /* ... !CONFIG_HOTPLUG_CPU */
static inline void register_cpu_control(struct cpu *cpu)
{
}
#endif /* CONFIG_HOTPLUG_CPU */

#if defined(CONFIG_KEXEC) && !defined(CONFIG_XEN)
#include <linux/kexec.h>

static ssize_t show_crash_notes(struct sys_device *dev, struct sysdev_attribute *attr,
				char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, sysdev);
	ssize_t rc;
	unsigned long long addr;
	int cpunum;

	cpunum = cpu->sysdev.id;

	/*
	 * Might be reading other cpu's data based on which cpu read thread
	 * has been scheduled. But cpu data (memory) is allocated once during
	 * boot up and this data does not change there after. Hence this
	 * operation should be safe. No locking required.
	 */
	addr = per_cpu_ptr_to_phys(per_cpu_ptr(crash_notes, cpunum));
	rc = sprintf(buf, "%Lx\n", addr);
	return rc;
}
static SYSDEV_ATTR(crash_notes, 0400, show_crash_notes, NULL);
#endif

/*
 * Print cpu online, possible, present, and system maps
 */

struct cpu_attr {
	struct sysdev_class_attribute attr;
	const struct cpumask *const * const map;
};

static ssize_t show_cpus_attr(struct sysdev_class *class,
			      struct sysdev_class_attribute *attr,
			      char *buf)
{
	struct cpu_attr *ca = container_of(attr, struct cpu_attr, attr);
	int n = cpulist_scnprintf(buf, PAGE_SIZE-2, *(ca->map));

	buf[n++] = '\n';
	buf[n] = '\0';
	return n;
}

#define _CPU_ATTR(name, map)						\
	{ _SYSDEV_CLASS_ATTR(name, 0444, show_cpus_attr, NULL), map }

/* Keep in sync with cpu_sysdev_class_attrs */
static struct cpu_attr cpu_attrs[] = {
	_CPU_ATTR(online, &cpu_online_mask),
	_CPU_ATTR(possible, &cpu_possible_mask),
	_CPU_ATTR(present, &cpu_present_mask),
};

/*
 * Print values for NR_CPUS and offlined cpus
 */
static ssize_t print_cpus_kernel_max(struct sysdev_class *class,
				     struct sysdev_class_attribute *attr, char *buf)
{
	int n = snprintf(buf, PAGE_SIZE-2, "%d\n", NR_CPUS - 1);
	return n;
}
static SYSDEV_CLASS_ATTR(kernel_max, 0444, print_cpus_kernel_max, NULL);

/* arch-optional setting to enable display of offline cpus >= nr_cpu_ids */
unsigned int total_cpus;

static ssize_t print_cpus_offline(struct sysdev_class *class,
				  struct sysdev_class_attribute *attr, char *buf)
{
	int len = 0;
	cpumask_var_t offline;

	/* display offline cpus < nr_cpu_ids */
	if (!alloc_cpumask_var(&offline, GFP_KERNEL))
		return -ENOMEM;
	cpumask_andnot(offline, cpu_possible_mask, cpu_online_mask);
	len = cpulist_scnprintf(buf, PAGE_SIZE, offline);
	free_cpumask_var(offline);

	/* display offline cpus >= nr_cpu_ids */
	if (total_cpus && nr_cpu_ids < total_cpus) {
		len += sysfs_emit_at(buf, len, ",");

		if (nr_cpu_ids == total_cpus-1)
			len += sysfs_emit_at(buf, len, "%d", nr_cpu_ids);
		else
			len += sysfs_emit_at(buf, len, "%d-%d",
						      nr_cpu_ids, total_cpus-1);
	}

	len += sysfs_emit_at(buf, len, "\n");

	return len;
}
static SYSDEV_CLASS_ATTR(offline, 0444, print_cpus_offline, NULL);

/*
 * register_cpu - Setup a sysfs device for a CPU.
 * @cpu - cpu->hotpluggable field set to 1 will generate a control file in
 *	  sysfs for this CPU.
 * @num - CPU number to use when creating the device.
 *
 * Initialize and register the CPU device.
 */
int __cpuinit register_cpu(struct cpu *cpu, int num)
{
	int error;
	cpu->node_id = cpu_to_node(num);
	cpu->sysdev.id = num;
	cpu->sysdev.cls = &cpu_sysdev_class;

	error = sysdev_register(&cpu->sysdev);

	if (!error && cpu->hotpluggable)
		register_cpu_control(cpu);
	if (!error)
		per_cpu(cpu_sys_devices, num) = &cpu->sysdev;
	if (!error)
		register_cpu_under_node(num, cpu_to_node(num));

#if defined(CONFIG_KEXEC) && !defined(CONFIG_XEN)
	if (!error)
		error = sysdev_create_file(&cpu->sysdev, &attr_crash_notes);
#endif
	return error;
}

#ifdef CONFIG_GENERIC_CPU_VULNERABILITIES

ssize_t __weak cpu_show_meltdown(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Not affected\n");
}

ssize_t __weak cpu_show_spectre_v1(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Not affected\n");
}

ssize_t __weak cpu_show_spectre_v2(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Not affected\n");
}

ssize_t __weak cpu_show_spec_store_bypass(struct device *dev,
					  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Not affected\n");
}

ssize_t __weak cpu_show_l1tf(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Not affected\n");
}

ssize_t __weak cpu_show_mds(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Not affected\n");
}

ssize_t __weak cpu_show_tsx_async_abort(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return sprintf(buf, "Not affected\n");
}

ssize_t __weak cpu_show_itlb_multihit(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Not affected\n");
}

ssize_t __weak cpu_show_srbds(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Not affected\n");
}

ssize_t __weak cpu_show_retbleed(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Not affected\n");
}

ssize_t __weak cpu_show_mmio_stale_data(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Not affected\n");
}

ssize_t __weak cpu_show_gds(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Not affected\n");
}

static DEVICE_ATTR(meltdown, 0444, cpu_show_meltdown, NULL);
static DEVICE_ATTR(spectre_v1, 0444, cpu_show_spectre_v1, NULL);
static DEVICE_ATTR(spectre_v2, 0444, cpu_show_spectre_v2, NULL);
static DEVICE_ATTR(spec_store_bypass, 0444, cpu_show_spec_store_bypass, NULL);
static DEVICE_ATTR(l1tf, 0444, cpu_show_l1tf, NULL);
static DEVICE_ATTR(mds, 0444, cpu_show_mds, NULL);
static DEVICE_ATTR(tsx_async_abort, 0444, cpu_show_tsx_async_abort, NULL);
static DEVICE_ATTR(itlb_multihit, 0444, cpu_show_itlb_multihit, NULL);
static DEVICE_ATTR(srbds, 0444, cpu_show_srbds, NULL);
static DEVICE_ATTR(retbleed, 0444, cpu_show_retbleed, NULL);
static DEVICE_ATTR(mmio_stale_data, 0444, cpu_show_mmio_stale_data, NULL);
static DEVICE_ATTR(gather_data_sampling, 0444, cpu_show_gds, NULL);

static struct attribute *cpu_root_vulnerabilities_attrs[] = {
	&dev_attr_meltdown.attr,
	&dev_attr_spectre_v1.attr,
	&dev_attr_spectre_v2.attr,
	&dev_attr_spec_store_bypass.attr,
	&dev_attr_l1tf.attr,
	&dev_attr_mds.attr,
	&dev_attr_tsx_async_abort.attr,
	&dev_attr_itlb_multihit.attr,
	&dev_attr_srbds.attr,
	&dev_attr_retbleed.attr,
	&dev_attr_mmio_stale_data.attr,
	&dev_attr_gather_data_sampling.attr,
	NULL
};

static const struct attribute_group cpu_root_vulnerabilities_group = {
	.name  = "vulnerabilities",
	.attrs = cpu_root_vulnerabilities_attrs,
};

static void __init cpu_register_vulnerabilities(void)
{
	if (sysfs_create_group(&cpu_sysdev_class.kset.kobj,
			       &cpu_root_vulnerabilities_group))
		pr_err("Unable to register CPU vulnerabilities\n");
}

#else
static inline void cpu_register_vulnerabilities(void) { }
#endif

struct sys_device *get_cpu_sysdev(unsigned cpu)
{
	if (cpu < nr_cpu_ids && cpu_possible(cpu))
		return per_cpu(cpu_sys_devices, cpu);
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(get_cpu_sysdev);

int __init cpu_dev_init(void)
{
	int err;

	err = sysdev_class_register(&cpu_sysdev_class);
#if defined(CONFIG_SCHED_MC) || defined(CONFIG_SCHED_SMT)
	if (!err)
		err = sched_create_sysfs_power_savings_entries(&cpu_sysdev_class);
#endif
	cpu_register_vulnerabilities();
	return err;
}

static struct sysdev_class_attribute *cpu_sysdev_class_attrs[] = {
#ifdef CONFIG_ARCH_CPU_PROBE_RELEASE
	&attr_probe,
	&attr_release,
#endif
	&cpu_attrs[0].attr,
	&cpu_attrs[1].attr,
	&cpu_attrs[2].attr,
	&attr_kernel_max,
	&attr_offline,
	NULL
};
