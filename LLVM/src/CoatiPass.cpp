#include "include/CoatiPass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"

#define _COATI_PASS_DEBUG
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
   * @brief Alpaca Pass
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
    void declareFuncs(Module &M) {
      Constant *r = M.getOrInsertFunction("read",
          FunctionType::getInt16PtrTy(getGlobalContext()), // Returns void *
          llvm::Type::getInt16PtrTy(getGlobalContext()), // void *addr
          llvm::Type::getInt16Ty(getGlobalContext()), // unsigned size
          llvm::Type::getInt16Ty(getGlobalContext()), // acc_type acc
          NULL);
      read_func = cast<Function>(r);

      Constant *w = M.getOrInsertFunction("write",
          FunctionType::getVoidTy(getGlobalContext()), // returns Void
          llvm::Type::getInt16PtrTy(getGlobalContext()), // void *addr
          llvm::Type::getInt16Ty(getGlobalContext()), // unsigned size
          llvm::Type::getInt16Ty(getGlobalContext()), // acc_type acc
          llvm::Type::getInt16Ty(getGlobalContext()), // unsigned value
          NULL);
      write_func = cast<Function>(w);
    }

    /** @brief Make __attribute__((annotate())) declarations in source
     *         file visible in function attributes
     *  Taken from http://bholt.org/posts/llvm-quick-tricks.html
     */
    void prepFuncAttributes(Module &M) {
      auto global_annos = M.getNamedGlobal("llvm.global.annotations");
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

    /** @brief Replace I with the relevant call to read()
     *         read_byte if alignment = 1, read_word otherwise).
     */
    void replaceRead(LoadInst *I) {
      std::vector<Value *> args;

      Value *size = ConstantInt::get(
          llvm::Type::getInt16Ty(getGlobalContext()),
          I->getAlignment(), false);
      // TODO modify to check if in transaction, event, or neither
      Value *acc = ConstantInt::get(
          llvm::Type::getInt16Ty(getGlobalContext()), 0, false);

      // Assemble arguments to read()
      args.push_back(new BitCastInst(I->getPointerOperand(),
          llvm::Type::getInt16PtrTy(getGlobalContext()), "addr", I));
      args.push_back(new BitCastInst(size,
            llvm::Type::getInt16Ty(getGlobalContext()), "size", I));
      args.push_back(new BitCastInst(acc,
            llvm::Type::getInt16Ty(getGlobalContext()), "acc", I));

      IRBuilder<> builder(I);
      CallInst *call = builder.CreateCall(read_func, ArrayRef<Value *>(args));
      // LoadInst could be returning any type, so insert a cast instruction
      // TODO cast to pointer of size <alignment> and deref
      Value *cast = builder.CreateBitOrPointerCast(call, I->getType());
      I->replaceAllUsesWith(cast);
      instsToDelete.push_back(I);
    }

    /** @brief Replace I with the relevant call to write()
     *         write_byte if alignment = 1, write_word otherwise).
     */
    void replaceWrite(StoreInst *I) {
      IRBuilder<> builder(I);
      std::vector<Value *> args;

      Value *size = ConstantInt::get(
          llvm::Type::getInt16Ty(getGlobalContext()), I->getAlignment(), false);
      // TODO modify to check if in transaction, event, or neither
      Value *acc = ConstantInt::get(
          llvm::Type::getInt16Ty(getGlobalContext()), 0, false);

      // Assemble arguements to write()
      args.push_back(I->getPointerOperand());
      args.push_back(new BitCastInst(size,
          llvm::Type::getInt16Ty(getGlobalContext()), "size", I));
      args.push_back(new BitCastInst(acc,
          llvm::Type::getInt16Ty(getGlobalContext()), "acc", I));
      args.push_back(I->getValueOperand());

      Value *call = builder.CreateCall(write_func, ArrayRef<Value *>(args));
      I->replaceAllUsesWith(call);
      instsToDelete.push_back(I);
    }

    CoatiModulePass() : ModulePass(ID) {}
    /**
     * @brief Body of pass - replace reads and writes with function calls
     */
    virtual bool runOnModule(Module &M){
      declareFuncs(M);
      prepFuncAttributes(M);
      for (auto &F : M) {
        if (F.hasFnAttribute("foobar")) {
          errs() << "Attribute foobar on " << F.getName() << "\n";
        }
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
                replaceWrite(op);
              }
            } else if (auto *op = dyn_cast<LoadInst>(&I)) {
              if (isGlobal(M, op->getPointerOperand())) {
#ifdef _COATI_PASS_DEBUG
                errs() << "Load Replacement on var: " <<
                  op->getPointerOperand()->getName() << "\n";
#endif
                replaceRead(op);
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
