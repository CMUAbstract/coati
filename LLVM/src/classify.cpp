/** @file classify.cpp
 *  @brief TODO
 */

#include "include/classify.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"


void getTransactFuncsHelper(Module *M, Function *F, std::vector<Function *> L) {
  L.push_back(F);
  F->addFnAttr("tx");
  for (auto &B : *F) {
    for (auto &I : B) {
      if (auto *op = dyn_cast<CallInst>(&I)) {
        Function *calledFn = op->getCalledFunction();
        StringRef fnName = calledFn->getName();
        if (fnName.equals(StringRef("transition_to"))) {
          if (!F->hasFnAttribute("tx_end")) {
            // TODO avoid drop_front(6) or add #define
            StringRef taskName = op->getArgOperand(0)->getName().drop_front(6);
            Function *nextTask = M->getFunction(taskName);

            if (std::find(L.begin(), L.end(), nextTask) == L.end()) {
              getTransactFuncsHelper(M, nextTask, L);
            }
          }
        } else if (!fnName.startswith(StringRef("llvm")) &&
            !fnName.equals(StringRef("printf"))) {
          if (std::find(L.begin(), L.end(), calledFn) == L.end()) {
            getTransactFuncsHelper(M, calledFn, L);
          }
        }
      }
    }
  }
}


void getEventFuncsHelper(Module *M, Function *F, std::vector<Function *> L) {
  L.push_back(F);
  F->addFnAttr("event");
  for (auto &B : *F) {
    for (auto &I : B) {
      if (auto *op = dyn_cast<CallInst>(&I)) {
        // Avoid Library linked functions (printf)
        if (op->getCalledFunction() == NULL) { continue;}
        StringRef fnName = op->getCalledFunction()->getName();
        if (fnName.equals(StringRef("transition_to"))) {
          if (!F->hasFnAttribute("event_end")) {
            // TODO avoid drop_front(6) or add #define
            StringRef taskName = op->getArgOperand(0)->getName().drop_front(6);
            Function *nextTask = M->getFunction(taskName);

            if (std::find(L.begin(), L.end(), nextTask) == L.end()) {
              getEventFuncsHelper(M, nextTask, L);
            }
          }
        } else if (!fnName.equals(StringRef("event_return")) &&
            !fnName.startswith(StringRef("llvm")) &&
            !fnName.equals(StringRef("printf"))) {
          if (std::find(L.begin(), L.end(), op->getCalledFunction()) == L.end()) {
            getEventFuncsHelper(M, op->getCalledFunction(), L);
          }
        }
      }
    }
  }
}


void annotateTransactFuncs(Module *M) {
  std::vector<Function *> L;
  for (auto &F : *M) {
    L.clear();
    if (F.hasFnAttribute("tx_begin")) {
      getTransactFuncsHelper(M, &F, L);
    }
  }
}


void annotateEventFuncs(Module *M) {
  std::vector<Function *> L;
  for (auto &F : *M) {
    L.clear();
    if (F.hasFnAttribute("event_begin")) {
      getEventFuncsHelper(M, &F, L);
    }
  }
}


/** @brief Make __attribute__((annotate())) declarations in source
  *         file visible in function attributes
  *  Taken from http://bholt.org/posts/llvm-quick-tricks.html
  */
void prepFuncAttributes(Module *M) {
  auto global_annos = M->getNamedGlobal("llvm.global.annotations");
  if (global_annos) {
    auto a = cast<ConstantArray>(global_annos->getOperand(0));
    for (int i = 0; i < a->getNumOperands(); i++) {
      auto e = cast<ConstantStruct>(a->getOperand(i));

      if (auto fn = dyn_cast<Function>(e->getOperand(0)->getOperand(0))) {
        auto anno = cast<ConstantDataArray>(cast<GlobalVariable>(
              e->getOperand(1)->getOperand(0))->getOperand(0))->getAsCString();
        fn->addFnAttr(anno);
      }
    }
  }
}
