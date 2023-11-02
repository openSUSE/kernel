# SPDX-License-Identifier: GPL-2.0
#
# Makefile for drivers/platform/x86/intel
# Intel x86 Platform-Specific Drivers
#

obj-$(CONFIG_INTEL_CHT_INT33FE)		+= int33fe/
obj-$(CONFIG_INTEL_SKL_INT3472)		+= int3472/
obj-$(CONFIG_INTEL_PMC_CORE)		+= pmc/
obj-$(CONFIG_INTEL_PMT_CLASS)		+= pmt/
obj-$(CONFIG_INTEL_UNCORE_FREQ_CONTROL)	+= uncore-frequency/


# TPMI drivers
intel_vsec_tpmi-y			:= tpmi.o
obj-$(CONFIG_INTEL_TPMI)		+= intel_vsec_tpmi.o


intel_sdsi-y				:= sdsi.o
obj-$(CONFIG_INTEL_SDSI)		+= intel_sdsi.o
intel_vsec-y				:= vsec.o
obj-$(CONFIG_INTEL_VSEC)		+= intel_vsec.o
