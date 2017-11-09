LIB = libcoati

include ../Makefile.config

OBJECTS += \
	coati.o \
  tx.o \
  filter.o\
  event.o \


DEPS += \
	libmsp \

override SRC_ROOT = ../../src

override CFLAGS += \
	-I$(SRC_ROOT) \
	-I$(SRC_ROOT)/include/libcoati \

