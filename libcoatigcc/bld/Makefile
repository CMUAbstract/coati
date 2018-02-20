LIB = libcoatigcc

export LIBCOATIGCC_ENABLE_DIAGNOSTICS = 0
export LIBCOATIGCC_CONF_REPORT = 0

include ../Makefile.config

OBJECTS += \
	coati.o \
  tx.o \
  filter.o\
  event.o \


DEPS += \
  libmspware \
	libmsp \

override SRC_ROOT = ../../src

override CFLAGS += \
	-I$(SRC_ROOT) \
	-I$(SRC_ROOT)/include/libcoatigcc \

