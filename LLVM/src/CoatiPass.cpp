#include "include/CoatiPass.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h" //ReplaceInstWithInst
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"

Function *read_byte_func;
Function *read_word_func;
Function *write_byte_func;
Function *write_word_func;

/** Alignment - size of access
 *  getPointerOperand()
 *  Value::replaceAllUsesWith(Value *v) - could be useful for replacement
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

      //void write(void * addr, void * value, size_t size);
      Constant *wbc = M.getOrInsertFunction("write_byte",
          FunctionType::getVoidTy(getGlobalContext()),
          llvm::Type::getInt8PtrTy(getGlobalContext()),
          llvm::Type::getInt8Ty(getGlobalContext()), NULL);
      write_byte_func = cast<Function>(wbc);
      //
      //void write(void * addr, void * value, size_t size);
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
      call = llvm::CallInst::Create(func, ArrayRef<Value *>(args),
          I->getName());
      I->dump();
      call->dump();
      errs() << I->getType()->getTypeID()
        << "\n" << call->getType()->getTypeID() << "\n";
      ReplaceInstWithInst(I, call);
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
      I->dump();
      func->dump();
      I->getPointerOperand()->dump();
      I->getValueOperand()->dump();
      args.push_back(I->getPointerOperand());
      args.push_back(I->getValueOperand());
      IRBuilder<> builder(I);
      Value *callVal = builder.CreateCall(func, ArrayRef<Value *>(args));
      //call = llvm::CallInst::Create(func, ArrayRef<Value *>(args));
      //call->dump();
      //ReplaceInstWithInst(I, call);
      for (auto& U : I->uses()) {
        User *user = U.getUser();
        user->setOperand(U.getOperandNo(), callVal);
      }
      I->dump();
      I->eraseFromParent();
    }

    CoatiModulePass() : ModulePass(ID) {}
    /**
     * @brief Body of pass
     */
    virtual bool runOnModule(Module &M){
      declareFuncs(M);
      for (auto &F : M) {
        //errs() << "\n" << F.getName() << "\n";
        for (auto &B : F) {
          for (auto &I : B) {
            if (auto *op = dyn_cast<StoreInst>(&I)) {
              insertWrite(op);
            } else if (auto *op = dyn_cast<LoadInst>(&I)) {
              insertRead(op);
            }
          }
        }
      }
    }
  };
}

char CoatiModulePass::ID = 0;

RegisterPass<CoatiModulePass> X("coati", "Coati Pass");
