#
# Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
# Licensed under the GPL
#

hostfs-objs := hostfs_kern.o hostfs_user.o

obj-y =
obj-$(CONFIG_HOSTFS) += hostfs.o

SINGLE_OBJS = $(foreach f,$(patsubst %.o,%,$(obj-y) $(obj-m)),$($(f)-objs))

USER_OBJS := $(filter %_user.o,$(obj-y) $(obj-m) $(SINGLE_OBJS))
USER_OBJS := $(foreach file,$(USER_OBJS),$(obj)/$(file))

$(USER_OBJS) : %.o: %.c
	$(CC) $(CFLAGS_$(notdir $@)) $(USER_CFLAGS) -c -o $@ $<
