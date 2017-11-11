#include "include/CoatiPass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"
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
    void declareFuncs(Module &M) {
      // void * read(void * addr);
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

    void insertRead(LoadInst *I) {
      Value *func;
      CallInst *call;
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
      call = builder.CreateCall(func, ArrayRef<Value *>(args));
      Value *cast = builder.CreateBitOrPointerCast(call, I->getType());

      errs() << "Performing Read Replacement\n";
      I->replaceAllUsesWith(cast);
      I->eraseFromParent();
    }

    void insertWrite(StoreInst *I) {
      Function *func;
      CallInst *call;
      std::vector<Value *> args;
      if (I->getAlignment() == 1) {
        func = write_byte_func;
      } else {
        func = write_word_func;
      }

      errs() << "\nPerforming Store replacement!\n";
      args.push_back(I->getPointerOperand());
      args.push_back(I->getValueOperand());
      IRBuilder<> builder(I);
      Value *callVal = builder.CreateCall(func, ArrayRef<Value *>(args));
      I->replaceAllUsesWith(callVal);
      I->eraseFromParent();
    }

    CoatiModulePass() : ModulePass(ID) {}
    /**
     * @brief Body of pass
     */
    virtual bool runOnModule(Module &M){
      declareFuncs(M);
      for (auto &F : M) {
        for (auto &B : F) {
          errs() << "Pre Dumping insts\n";
          for (auto &I : B) { I.dump();}
          while (1) {
            Instruction *nextI = NULL;
            BasicBlock::iterator I, IE;
            for (I = B.begin(), IE = B.end(); I != IE; ++I) {
              if (auto *op = dyn_cast<StoreInst>(I)) {
                insertWrite(op);
                break;
              } else if (auto *op = dyn_cast<LoadInst>(I)) {
                insertRead(op);
                break;
              }
            }
            if (IE == I) {
              break;
            }
          }
          errs() << "\nPost Dumping insts\n";
          for (auto &I : B) { I.dump();}
          errs() << "\n\n";
        }
      }
    }
  };
}

char CoatiModulePass::ID = 0;

RegisterPass<CoatiModulePass> X("coati", "Coati Pass");
