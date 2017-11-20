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

/** @brief List of instructions to be delete in the current BB.
 *         We use this because deleting while traversing gives
 *         NULL pointer dereferences
 */
static std::vector<Instruction *> instsToDelete;

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
      instsToDelete.push_back(I);
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
      instsToDelete.push_back(I);
    }

    CoatiModulePass() : ModulePass(ID) {}
    /**
     * @brief Body of pass - replace reads and writes with function calls
     */
    virtual bool runOnModule(Module &M){
      unsigned acc_type;

      declareFuncs(&M);
      prepFuncAttributes(&M);
      annotateTransactFuncs(&M);
      annotateEventFuncs(&M);
      for (auto &F : M) {
        if (F.hasFnAttribute("tx")) {
          acc_type = 1;
        } else if (F.hasFnAttribute("event")) {
        acc_type = 2;
        } else {
          acc_type = 0;
        }
        errs() << F.getName() << ": " << acc_type << "\n";

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
              }
            } else if (auto *op = dyn_cast<LoadInst>(&I)) {
              if (isGlobal(M, op->getPointerOperand())) {
#ifdef _COATI_PASS_DEBUG
                errs() << "Load Replacement on var: " <<
                  op->getPointerOperand()->getName() << "\n";
#endif
                replaceRead(op, acc_type);
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
