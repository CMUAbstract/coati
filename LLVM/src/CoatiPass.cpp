/** @file CoatiPass.cpp
 *  @brief TODO
 */

#include "include/CoatiPass.h"
#include "include/classify.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/raw_ostream.h"

//#define _COATI_PASS_DEBUG
using namespace llvm;

static Function *read_func;
static Function *write_func;

static Function *tx_memcpy_func;
static Function *event_memcpy_func;
static Function *internal_memcpy_func;

/** @brief Replace a memcpy() call to one of our memcpy functions */
void replaceMemcpy(CallInst *I, acc_type_t acc_type) {
#ifdef _COATI_PASS_DEBUG
  errs() << "Replacing Memcpy: " << acc_type << "\n";
  I->dump();
#endif
  Function *func;
  if (acc_type == EVENT) {
    func = event_memcpy_func;
  } else if (acc_type == TX) {
    func = tx_memcpy_func;
  } else {
    func = internal_memcpy_func;
  }

  IRBuilder<> builder(I);
  std::vector<Value *> args;
  args.push_back(new BitCastInst(I->getArgOperand(0),
        Type::getInt16PtrTy(getGlobalContext()), "dest", I));
  args.push_back(new BitCastInst(I->getArgOperand(1),
        Type::getInt16PtrTy(getGlobalContext()), "src", I));
  args.push_back(new BitCastInst(I->getArgOperand(2),
        Type::getInt16Ty(getGlobalContext()), "num", I));

  Value *call = builder.CreateCall(func, ArrayRef<Value *>(args));
  I->replaceAllUsesWith(call);
}


/**
 */
namespace {
  /**
   * @brief Coati Pass
   */
  struct CoatiModulePass : public ModulePass {
    static char ID;

    /** @brief Check if V is a global variable in module M */
    bool isGlobal(Module &M, Value *V) {
      for (auto &G : M.getGlobalList()) {
        if (V == &G) { return true;}
      }
      return false;
    }

    /** @brief Declare references to (write|read)_(byte|word)
     *         in module M and initialize static variables
     */
    void declareFuncs(Module *M) {
      Constant *r = M->getOrInsertFunction("read",
          FunctionType::getInt16PtrTy(getGlobalContext()), // Returns void *
          Type::getInt16PtrTy(getGlobalContext()), // void *addr
          Type::getInt16Ty(getGlobalContext()), // unsigned size
          Type::getInt16Ty(getGlobalContext()), // acc_type acc
          NULL);
      read_func = cast<Function>(r);

      Constant *w = M->getOrInsertFunction("write",
          FunctionType::getVoidTy(getGlobalContext()), // returns Void
          Type::getInt16PtrTy(getGlobalContext()), // void *addr
          Type::getInt16Ty(getGlobalContext()), // unsigned size
          Type::getInt16Ty(getGlobalContext()), // acc_type acc
          Type::getInt16Ty(getGlobalContext()), // unsigned value
          NULL);
      write_func = cast<Function>(w);

      Constant *tx = M->getOrInsertFunction("tx_memcpy",
          FunctionType::getVoidTy(getGlobalContext()), // returns void
          Type::getInt16PtrTy(getGlobalContext()), // void *dest
          Type::getInt16PtrTy(getGlobalContext()), // void *src
          Type::getInt16Ty(getGlobalContext()), // size_t num
          NULL);
      tx_memcpy_func = cast<Function>(tx);

      Constant *event = M->getOrInsertFunction("event_memcpy",
          FunctionType::getVoidTy(getGlobalContext()), // returns void
          Type::getInt16PtrTy(getGlobalContext()), // void *dest
          Type::getInt16PtrTy(getGlobalContext()), // void *src
          Type::getInt16Ty(getGlobalContext()), // size_t num
          NULL);
      event_memcpy_func = cast<Function>(event);

      Constant *intern = M->getOrInsertFunction("internal_memcpy",
          FunctionType::getVoidTy(getGlobalContext()), // returns void
          Type::getInt16PtrTy(getGlobalContext()), // void *dest
          Type::getInt16PtrTy(getGlobalContext()), // void *src
          Type::getInt16Ty(getGlobalContext()), // size_t num
          NULL);
      internal_memcpy_func = cast<Function>(intern);
    }

    /** @brief Replace I with the relevant call to read()
     *         read_byte if alignment = 1, read_word otherwise).
     */
    void replaceRead(LoadInst *I, unsigned acc_type) {
      std::vector<Value *> args;

      Value *size = ConstantInt::get(
          Type::getInt16Ty(getGlobalContext()),
          I->getAlignment(), false);

      Value *acc = ConstantInt::get(
          Type::getInt16Ty(getGlobalContext()), acc_type, false);

      // Assemble arguments to read()
      args.push_back(new BitCastInst(I->getPointerOperand(),
          Type::getInt16PtrTy(getGlobalContext()), "addr", I));
      args.push_back(new BitCastInst(size,
            Type::getInt16Ty(getGlobalContext()), "size", I));
      args.push_back(new BitCastInst(acc,
            Type::getInt16Ty(getGlobalContext()), "acc", I));

      IRBuilder<> builder(I);
      CallInst *call = builder.CreateCall(read_func, ArrayRef<Value *>(args));
      // Cast to the pointer type we were passed in
      Value *cast = builder.CreatePointerCast(call,
          I->getPointerOperand()->getType());
      // Insert a load of pointer value
      Value *load = builder.CreateAlignedLoad(cast, I->getAlignment(), "val");
      I->replaceAllUsesWith(load);
    }

    /** @brief Replace I with the relevant call to write()
     *         write_byte if alignment = 1, write_word otherwise).
     */
    void replaceWrite(StoreInst *I, unsigned acc_type) {
      IRBuilder<> builder(I);
      std::vector<Value *> args;

      Value *size = ConstantInt::get(
          Type::getInt16Ty(getGlobalContext()), I->getAlignment(), false);
      Value *acc = ConstantInt::get(
          Type::getInt16Ty(getGlobalContext()), acc_type, false);

      // Assemble arguements to write()
      args.push_back(new BitCastInst(I->getPointerOperand(),
          Type::getInt16PtrTy(getGlobalContext()), "addr", I));
      args.push_back(new BitCastInst(size,
          Type::getInt16Ty(getGlobalContext()), "size", I));
      args.push_back(new BitCastInst(acc,
          Type::getInt16Ty(getGlobalContext()), "acc", I));
      args.push_back(CastInst::CreateIntegerCast(I->getValueOperand(),
          Type::getInt16Ty(getGlobalContext()), false, "val", I));

      Value *call = builder.CreateCall(write_func, ArrayRef<Value *>(args));
      I->replaceAllUsesWith(call);
    }

    CoatiModulePass() : ModulePass(ID) {}
    /**
     * @brief Body of pass - replace reads and writes with function calls
     */
    virtual bool runOnModule(Module &M){
      acc_type_t acc_type;
      /** @brief List of instructions to be delete in the current BB.
      *         We use this because deleting while traversing gives
      *         NULL pointer dereferences
      */
      std::vector<Instruction *> instsToDelete;

      declareFuncs(&M);
      prepFuncAttributes(&M);
      annotateTransactFuncs(&M);
      annotateEventFuncs(&M);
      for (auto &F : M) {
        // TODO Change to use programmer task annotations
        if (F.hasFnAttribute("tx")) {
          acc_type = TX;
        } else if (F.hasFnAttribute("event")) {
        acc_type = EVENT;
        } else {
          acc_type = NORMAL;
        }
        //errs() << F.getName() << ": " << acc_type << "\n";
        // TODO - does this replace for called function in normal code?
        if (acc_type == EVENT && !F.getName().startswith("task")) { continue;}

        for (auto &B : F) {
#ifdef _COATI_PASS_DEBUG
          errs() << "BB instructions before pass\n";
          for (auto &I : B) { I.dump();}
#endif
          instsToDelete.clear();
          for (auto &I : B) {
            if (auto *op = dyn_cast<StoreInst>(&I)) {
              if (isGlobal(M, op->getPointerOperand())) {
#ifdef _COATI_PASS_DEBUG
                errs() << "Store Replacement on var: " <<
                  op->getPointerOperand()->getName() << "\n";
#endif
                replaceWrite(op, acc_type);
                instsToDelete.push_back(op);
              }
            } else if (auto *op = dyn_cast<LoadInst>(&I)) {
              if (isGlobal(M, op->getPointerOperand())) {
#ifdef _COATI_PASS_DEBUG
                errs() << "Load Replacement on var: " <<
                  op->getPointerOperand()->getName() << "\n";
#endif
                replaceRead(op, acc_type);
                instsToDelete.push_back(op);
              }
            } else if (auto *op = dyn_cast<CallInst>(&I)) {
              if (!op->getCalledFunction()) { continue;}
              if (acc_type == NORMAL &&
                  op->getCalledFunction()->getName().find("memcpy") != std::string::npos) {
                replaceMemcpy(op, NORMAL);
                instsToDelete.push_back(op);
              }
            }
          }
          for (auto &I : instsToDelete) {
            I->eraseFromParent();
          }
#ifdef _COATI_PASS_DEBUG
          errs() << "\nBB instructions after pass\n";
          for (auto &I : B) { I.dump();}
          errs() << "\n\n";
#endif
        }
      }
    }
  };
}

char CoatiModulePass::ID = 0;

RegisterPass<CoatiModulePass> X("coati", "Coati Pass");
