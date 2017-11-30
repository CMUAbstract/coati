#ifndef __COATIPASS_H
#define __COATIPASS_H

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"

using namespace llvm;

typedef enum {
  NORMAL=0,
  TX=1,
  EVENT=2,
} acc_type_t;

void replaceMemcpy(CallInst *I, acc_type_t acc_type);

#endif // __COATIPASS_H
