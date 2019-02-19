LIB = libcoatigcc

export LIBCOATIGCC_ENABLE_DIAGNOSTICS = 0
export LIBCOATIGCC_CONF_REPORT = 0

OBJECTS += \
  top_half.o \
	coati.o \
  tx.o \
  filter.o\
  event.o \


DEPS += \
  libmspware \
	libmsp \

override SRC_ROOT = ../../src

override CFLAGS += \
	-I$(SRC_ROOT)/include \
	-I$(SRC_ROOT)/include/$(LIB) \

include $(MAKER_ROOT)/Makefile.$(TOOLCHAIN)
