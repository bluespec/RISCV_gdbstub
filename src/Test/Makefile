# ================================================================

SRCS_H = \
    ../gdbstub_be.h \
    ../gdbstub_dmi.h \
    ../gdbstub_fe.h \
    ../gdbstub.h \
    ../RVDM.h \
    ../Elf_read.h \


SRCS_C = \
    main_smoke_test.c \
    ../gdbstub_be.c \
    ../gdbstub.c \
    ../gdbstub_dmi_stub.c \
    ../gdbstub_fe.c \
    ../RVDM.c \
    ../Elf_read.c \

# ================================================================

CFLAGS += -g -Wall -Werror
CFLAGS += -I ..

LDLIBS = -lpthread -lelf

exe_smoke_test: $(SRCS_H) $(SRCS_C)
	gcc $(CFLAGS) -o exe_smoke_test  $(SRCS_C) $(LDLIBS)

# ================================================================

.PHONY: rmlogs
rmlogs:
	rm -f log*.txt

.PHONY: clean
clean:
	rm -f *~  *.o

.PHONY: full_clean
full_clean:
	rm -f *~  *.o  exe_smoke_test
