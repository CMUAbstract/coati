/** @file classify.h
 *  @brief TODO
 */
#ifndef __CLASSIFY_H
#define __CLASSIFY_H

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
using namespace llvm;

void annotateTransactFuncs(Module *M);
void annotateEventFuncs(Module *M);
void prepFuncAttributes(Module *M);

#endif // __CLASSIFY_H
