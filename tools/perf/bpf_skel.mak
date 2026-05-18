# SPDX-License-Identifier: GPL-2.0
# Shared BPF Skeleton Generator Rules

include $(srctree)/tools/scripts/Makefile.include

# Shared foundational tooling always lives in util/bpf_skel
SKEL_TOOL_OUT := $(abspath $(OUTPUT)util/bpf_skel)
SKEL_TOOL_TMP_OUT := $(abspath $(SKEL_TOOL_OUT)/.tmp)

# Component specific output lives in $(dir)/bpf_skel
SKEL_OUT := $(abspath $(OUTPUT)$(dir)/bpf_skel)
SKEL_TMP_OUT := $(abspath $(SKEL_OUT)/.tmp)

ifeq ($(CONFIG_PERF_BPF_SKEL),y)
BPFTOOL := $(SKEL_TOOL_TMP_OUT)/bootstrap/bpftool
VMLINUX_H := $(SKEL_TOOL_OUT)/vmlinux.h

define get_sys_includes
$(shell $(1) $(2) -v -E - </dev/null 2>&1 \
       | sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }') \
$(shell $(1) $(2) -dM -E - </dev/null | grep '__riscv_xlen ' | awk '{printf("-D__riscv_xlen=%d -D__BITS_PER_LONG=%d", $$3, $$3)}')
endef

ifneq ($(CROSS_COMPILE),)
CLANG_TARGET_ARCH = --target=$(notdir $(CROSS_COMPILE:%-=%))
endif

CLANG_OPTIONS = -Wall
CLANG_SYS_INCLUDES = $(call get_sys_includes,$(CLANG),$(CLANG_TARGET_ARCH))
LIBBPF_INCLUDE := $(abspath $(or $(OUTPUT),.))/libbpf/include
BPF_INCLUDE := -I$(SKEL_TMP_OUT)/.. -I$(SKEL_TOOL_OUT) -I$(LIBBPF_INCLUDE) $(CLANG_SYS_INCLUDES)
TOOLS_UAPI_INCLUDE := -I$(srctree)/tools/include/uapi

ifneq ($(WERROR),0)
  CLANG_OPTIONS += -Werror
endif

# Consolidated Pattern rule for $(dir)/bpf_skel/
$(SKEL_TMP_OUT)/%.bpf.o: $(srctree)/tools/perf/$(dir)/bpf_skel/%.bpf.c $(LIBBPF) $(VMLINUX_H) $(OUTPUT)PERF-VERSION-FILE util/bpf_skel/perf_version.h
	$(call rule_mkdir)
	$(QUIET_CLANG)
	$(Q)$(CLANG) -g -O2 -fno-stack-protector --target=bpf \
	  $(CLANG_OPTIONS) $(EXTRA_BPF_FLAGS) $(BPF_INCLUDE) $(TOOLS_UAPI_INCLUDE) \
	  -include $(OUTPUT)PERF-VERSION-FILE -include util/bpf_skel/perf_version.h \
	  -fms-extensions -Wno-microsoft-anon-tag \
	  -c $< -o $@

$(SKEL_OUT)/%.skel.h: $(SKEL_TMP_OUT)/%.bpf.o $(BPFTOOL)
	$(call rule_mkdir)
	$(QUIET_GENSKEL)
	$(Q)$(BPFTOOL) gen skeleton $< > $@

.PRECIOUS: $(SKEL_TMP_OUT)/%.bpf.o
endif # CONFIG_PERF_BPF_SKEL
