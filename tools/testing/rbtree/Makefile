# SPDX-License-Identifier: GPL-2.0

.PHONY: clean

TARGETS = rbtree_test interval_tree_test
OFILES = $(SHARED_OFILES) rbtree-shim.o interval_tree-shim.o maple-shim.o
DEPS = ../../../include/linux/rbtree.h \
	../../../include/linux/rbtree_types.h \
	../../../include/linux/rbtree_augmented.h \
	../../../include/linux/interval_tree.h \
	../../../include/linux/interval_tree_generic.h \
	../../../lib/rbtree.c \
	../../../lib/interval_tree.c

targets: $(TARGETS)

include ../shared/shared.mk

ifeq ($(DEBUG), 1)
	CFLAGS += -g
endif

$(TARGETS):	$(OFILES)

rbtree-shim.o: $(DEPS)
rbtree_test.o:  ../../../lib/rbtree_test.c
interval_tree-shim.o: $(DEPS)
interval_tree-shim.o: CFLAGS += -DCONFIG_INTERVAL_TREE_SPAN_ITER
interval_tree_test.o: 	../../../lib/interval_tree_test.c
interval_tree_test.o: CFLAGS += -DCONFIG_INTERVAL_TREE_SPAN_ITER

clean:
	$(RM) $(TARGETS) *.o radix-tree.c idr.c generated/*
