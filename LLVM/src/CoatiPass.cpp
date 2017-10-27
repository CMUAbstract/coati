#include "include/CoatiPass.h"
#include <algorithm>

namespace {
    /**
     * @brief Alpaca Pass
     */
    struct CoatiModulePass : public ModulePass {
        static char ID;
        CoatiModulePass() : ModulePass(ID) {}
        /**
         * @brief Body of pass
         */
        virtual bool runOnModule(Module &M){
        }
    };
}

char CoatiModulePass::ID = 0;

RegisterPass<CoatiModulePass> X("coati", "Coati Pass");
