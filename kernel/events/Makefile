# SPDX-License-Identifier: GPL-2.0
ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_core.o = $(CC_FLAGS_FTRACE)
endif

obj-y := \
	core.o \
	ring_buffer.o \
	callchain.o \
	tintin_sched_event_handler.o \
	tintin_interface.o \
	tintin_scheduler.o \
	tintin_uncertainty.o \
	tintin_elastic.o \
	tintin_vector.o \
	tintin_utils.o \
	tintin_estimation.o \
	tintin_profiling_context.o \
	tintin_syscalls.o 

obj-$(CONFIG_HAVE_HW_BREAKPOINT) += hw_breakpoint.o
obj-$(CONFIG_UPROBES) += uprobes.o

