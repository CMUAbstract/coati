LIB = libcoatigcc

OBJECTS += \
  top_half.o \
  coati.o \
  tx.o \
  filter.o\
  event.o \

override SRC_ROOT = ../../src

override CFLAGS += \
	-I$(SRC_ROOT) \
	-I$(SRC_ROOT)/include/$(LIB) \

include $(MAKER_ROOT)/Makefile.$(TOOLCHAIN)
