LIB = libcoatigcc

export LIBCOATIGCC_ENABLE_DIAGNOSTICS = 0
export LIBCOATIGCC_CONF_REPORT = 0

include ../Makefile.config

OBJECTS += \
  top_half.o \
	coati.o \
  tx.o \
  undo.o \
  filter.o\
  event.o \
  hash.o \


DEPS += \
  libmspware \
	libmsp \

override SRC_ROOT = ../../src

override CFLAGS += \
	-I$(SRC_ROOT) \
	-I$(SRC_ROOT)/include/libcoatigcc \

