LLVM_DIR = LLVM

ifneq ($(COATI_DEBUG_LOG),)
  CMAKE_VARS += -DDEBUG_LOG=$(COATI_DEBUG_LOG)
endif

include $(MAKER_ROOT)/Makefile.llvm
