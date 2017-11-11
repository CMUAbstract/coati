#include "include/CoatiPass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"

//#define _COATI_PASS_DEBUG
using namespace llvm;

static Function *read_byte_func;
static Function *read_word_func;
static Function *write_byte_func;
static Function *write_word_func;

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
      Constant *rbc = M.getOrInsertFunction("read_byte",
          FunctionType::getInt8Ty(getGlobalContext()),
          llvm::Type::getInt8PtrTy(getGlobalContext()), NULL);
      read_byte_func = cast<Function>(rbc);

      Constant *rwc = M.getOrInsertFunction("read_word",
          llvm::Type::getInt16Ty(getGlobalContext()),
          llvm::Type::getInt16PtrTy(getGlobalContext()), NULL);
      read_word_func = cast<Function>(rwc);

      Constant *wbc = M.getOrInsertFunction("write_byte",
          FunctionType::getVoidTy(getGlobalContext()),
          llvm::Type::getInt8PtrTy(getGlobalContext()),
          llvm::Type::getInt8Ty(getGlobalContext()), NULL);
      write_byte_func = cast<Function>(wbc);

      Constant *wwc = M.getOrInsertFunction("write_word",
          FunctionType::getVoidTy(getGlobalContext()),
          llvm::Type::getInt16PtrTy(getGlobalContext()),
          llvm::Type::getInt16Ty(getGlobalContext()), NULL);
      write_word_func = cast<Function>(wwc);
    }

    /** @brief Replace I with the relevant call to read()
     *         read_byte if alignment = 1, read_word otherwise).
     */
    void replaceRead(LoadInst *I) {
      Value *func;
      std::vector<Value *> args;
      if (I->getAlignment() == 1) {
        func = read_byte_func;
        args.push_back(new BitCastInst(I->getPointerOperand(),
            llvm::Type::getInt8PtrTy(getGlobalContext()), "", I));
      } else {
        func = read_word_func;
        args.push_back(new BitCastInst(I->getPointerOperand(),
            llvm::Type::getInt16PtrTy(getGlobalContext()), "", I));
      }
      IRBuilder<> builder(I);
      CallInst *call = builder.CreateCall(func, ArrayRef<Value *>(args));
      // LoadInst could be returning any type, so insert a cast instruction
      Value *cast = builder.CreateBitOrPointerCast(call, I->getType());

      I->replaceAllUsesWith(cast);
      I->eraseFromParent();
    }

    /** @brief Replace I with the relevant call to write()
     *         write_byte if alignment = 1, write_word otherwise).
     */
    void replaceWrite(StoreInst *I) {
      IRBuilder<> builder(I);
      Function *func;
      std::vector<Value *> args;
      if (I->getAlignment() == 1) {
        func = write_byte_func;
      } else {
        func = write_word_func;
      }

      args.push_back(I->getPointerOperand());
      args.push_back(I->getValueOperand());
      Value *call = builder.CreateCall(func, ArrayRef<Value *>(args));
      I->replaceAllUsesWith(call);
      I->eraseFromParent();
    }

    CoatiModulePass() : ModulePass(ID) {}
    /**
     * @brief Body of pass - replace reads and writes with function calls
     */
    virtual bool runOnModule(Module &M){
      std::vector<Instruction *> instsToDelete;
      declareFuncs(M);
      for (auto &F : M) {
        for (auto &B : F) {
#ifdef _COATI_PASS_DEBUG
          errs() << "Pre Dumping insts\n";
          for (auto &I : B) { I.dump();}
#endif
          // Loop is strangly structured because traversing a list
          // of instructions doesn't work after one is erased
          while (1) {
            BasicBlock::iterator I, IE;
            for (I = B.begin(), IE = B.end(); I != IE; ++I) {
              if (auto *op = dyn_cast<StoreInst>(I)) {
                if (isGlobal(M, op->getPointerOperand())) {
#ifdef _COATI_PASS_DEBUG
                  errs() << "Store Replacement on var: " <<
                    op->getPointerOperand()->getName() << "\n";
#endif
                  replaceWrite(op);
                  break;
                }
              } else if (auto *op = dyn_cast<LoadInst>(I)) {
                if (isGlobal(M, op->getPointerOperand())) {
#ifdef _COATI_PASS_DEBUG
                  errs() << "Load Replacement on var: " <<
                    op->getPointerOperand()->getName() << "\n";
#endif
                  replaceRead(op);
                  break;
                }
              }
            }
            if (IE == I) {
              break;
            }
          }
#ifdef _COATI_PASS_DEBUG
          errs() << "\nPost Dumping insts\n";
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
