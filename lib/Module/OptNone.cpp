//===-- OptNone.cpp -------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Config/Version.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(17, 0)
#include "PassesNew.h"
#else
#include "Passes.h"
#endif

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"

namespace klee {
#if LLVM_VERSION_CODE >= LLVM_VERSION(17, 0)

using namespace llvm;

PreservedAnalyses OptNonePass::run(Function &F, FunctionAnalysisManager &AM) {
  // Skip if already annotated
  if (F.hasFnAttribute(Attribute::OptimizeNone))
    goto is_clean;

  for (BasicBlock& B : F) {
    for (Instruction& instr : B) {
      if (isa<CastInst>(instr) || isa<InvokeInst>(instr)) {
        CallBase* ci = dyn_cast<CallBase>(&instr);
        Function* callee = ci->getCalledFunction();

        if (callee != nullptr
         && callee->hasName()
         && callee->getName().startswith("klee_")) {

          F.addFnAttr(Attribute::OptimizeNone);
          F.addFnAttr(Attribute::NoInline);
          return PreservedAnalyses::none();
        }
      }
    }
  }

is_clean:
    return PreservedAnalyses::all();
}

#else

char OptNonePass::ID;

bool OptNonePass::runOnModule(llvm::Module &M) {
  // Find list of functions that start with `klee_`
  // and mark all functions that contain such call or invoke as optnone
  llvm::SmallPtrSet<llvm::Function *,16> CallingFunctions;
  for (auto &F : M) {
    if (!F.hasName() || !F.getName().startswith("klee_"))
      continue;
    for (auto *U : F.users()) {
      // skip non-calls and non-invokes
      if (!llvm::isa<llvm::CallInst>(U) && !llvm::isa<llvm::InvokeInst>(U))
        continue;
      auto *Inst = llvm::cast<llvm::Instruction>(U);
      CallingFunctions.insert(Inst->getParent()->getParent());
    }
  }

  bool changed = false;
  for (auto F : CallingFunctions) {
    // Skip if already annotated
    if (F->hasFnAttribute(llvm::Attribute::OptimizeNone))
      continue;
    F->addFnAttr(llvm::Attribute::OptimizeNone);
    F->addFnAttr(llvm::Attribute::NoInline);
    changed = true;
  }

  return changed;
}

#endif
} // namespace klee
