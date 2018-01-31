TEST_PROGS := ptrace-gpr ptrace-tm-gpr ptrace-tm-spd-gpr \
              ptrace-tar ptrace-tm-tar ptrace-tm-spd-tar ptrace-vsx ptrace-tm-vsx \
              ptrace-tm-spd-vsx ptrace-tm-spr ptrace-pkey core-pkey

include ../../lib.mk

all: $(TEST_PROGS)

CFLAGS += -m64 -I../../../../../usr/include -I../tm -mhtm

ptrace-pkey: ../harness.c ../utils.c ../lib/reg.S ptrace.h child.h ptrace-pkey.c
	$(LINK.c) $^ $(LDLIBS) -pthread -o $@

core-pkey: ../harness.c ../utils.c ../lib/reg.S ptrace.h child.h core-pkey.c
	$(LINK.c) $^ $(LDLIBS) -pthread -o $@

$(TEST_PROGS): ../harness.c ../utils.c ../lib/reg.S ptrace.h

clean:
	rm -f $(TEST_PROGS) *.o
