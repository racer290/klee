//===-- Passes.h ------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_PASSES_H
#define KLEE_PASSES_H

#include "KLEEIRMetaData.h"
#include "klee/Config/Version.h"
#include "klee/Support/CompilerWarning.h"
#include "klee/Support/ErrorHandling.h"

DISABLE_WARNING_PUSH
DISABLE_WARNING_DEPRECATED_DECLARATIONS
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Host.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/Scalarizer.h"
DISABLE_WARNING_POP

namespace llvm {
class Function;
class Instruction;
class Module;
class DataLayout;
class TargetLowering;
class Type;
} // namespace llvm

namespace klee {

/// RaiseAsmPass - This pass raises some common occurences of inline
/// asm which are used by glibc into normal LLVM IR.
class RaiseAsmPass : public llvm::PassInfoMixin<RaiseAsmPass> {
private:
  llvm::Function *getIntrinsic(llvm::Module &M, unsigned IID, llvm::Type **Tys,
                               unsigned NumTys);
  llvm::Function *getIntrinsic(llvm::Module &M, unsigned IID, llvm::Type *Ty0) {
    return getIntrinsic(M, IID, &Ty0, 1);
  }
  bool processInstruction(llvm::Triple& triple, const llvm::TargetLowering* TLI, llvm::Instruction* I);
public:
  llvm::PreservedAnalyses run(llvm::Function& F, llvm::FunctionAnalysisManager&);
};

// This is a module pass because it can add and delete module
// variables (via intrinsic lowering).
class IntrinsicCleanerPass : public llvm::PassInfoMixin<IntrinsicCleanerPass> {
private:
  bool runOnBasicBlock(const llvm::DataLayout& DataLayout, llvm::IntrinsicLowering* IL, llvm::BasicBlock &b, llvm::Module &M);
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
};

// performs two transformations which make interpretation
// easier and faster.
//
// 1) Ensure that all the PHI nodes in a basic block have
//    the incoming block list in the same order. Thus the
//    incoming block index only needs to be computed once
//    for each transfer.
//
// 2) Ensure that no PHI node result is used as an argument to
//    a subsequent PHI node in the same basic block. This allows
//    the transfer to execute the instructions in order instead
//    of in two passes.
class PhiCleanerPass : public llvm::PassInfoMixin<PhiCleanerPass> {
public:
  llvm::PreservedAnalyses run(llvm::Function &F, llvm::FunctionAnalysisManager &AM);
};

class DivCheckPass : public llvm::PassInfoMixin<DivCheckPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
};

/// This pass injects checks to check for overshifting.
///
/// Overshifting is where a Shl, LShr or AShr is performed
/// where the shift amount is greater than width of the bitvector
/// being shifted.
/// In LLVM (and in C/C++) this undefined behaviour!
///
/// Example:
/// \code
///     unsigned char x=15;
///     x << 4 ; // Defined behaviour
///     x << 8 ; // Undefined behaviour
///     x << 255 ; // Undefined behaviour
/// \endcode
class OvershiftCheckPass : public llvm::PassInfoMixin<OvershiftCheckPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
};

/// LowerSwitchPass - Replace all SwitchInst instructions with chained branch
/// instructions.  Note that this cannot be a BasicBlock pass because it
/// modifies the CFG!
class LowerSwitchPass : public llvm::PassInfoMixin<LowerSwitchPass> {
public:
  llvm::PreservedAnalyses run(llvm::Function &F, llvm::FunctionAnalysisManager &AM);

  struct SwitchCase {
    llvm ::Constant *value;
    llvm::BasicBlock *block;

    SwitchCase() : value(0), block(0) {}
    SwitchCase(llvm::Constant *v, llvm::BasicBlock *b) : value(v), block(b) {}
  };

  using CaseVector = std::vector<SwitchCase>;
  using CaseItr = std::vector<SwitchCase>::iterator;
private:
  void processSwitchInst(llvm::SwitchInst *SI);
  void switchConvert(CaseItr begin, CaseItr end, llvm::Value* value,
                     llvm::BasicBlock* origBlock,
                     llvm::BasicBlock* defaultBlock);
};

/// InstructionOperandTypeCheckPass - Type checks the types of instruction
/// operands to check that they conform to invariants expected by the Executor.
///
/// This is a ModulePass because other pass types are not meant to maintain
/// state between calls.
class InstructionOperandTypeCheckPass : public llvm::AnalysisInfoMixin<InstructionOperandTypeCheckPass> {
  friend llvm::AnalysisInfoMixin<InstructionOperandTypeCheckPass>;
  static llvm::AnalysisKey Key;
public:
  struct Result {
    bool me;
    Result(bool b) : me(b) {}
    operator bool () { return me; }
  };
  Result run(llvm::Function &F, llvm::FunctionAnalysisManager&);
  // required as per llvm/IR/PassManager.h
};

/// FunctionAliasPass - Enables a user of KLEE to specify aliases to functions
/// using -function-alias=<name|pattern>:<replacement> which are injected as
/// GlobalAliases into the module. The replaced function is removed.
class FunctionAliasPass : public llvm::PassInfoMixin<FunctionAliasPass> {
private:
  static const llvm::FunctionType *getFunctionType(const llvm::GlobalValue *gv);
  static bool checkType(const llvm::GlobalValue *match, const llvm::GlobalValue *replacement);
  static bool tryToReplace(llvm::GlobalValue *match, llvm::GlobalValue *replacement);
  static bool isFunctionOrGlobalFunctionAlias(const llvm::GlobalValue *gv);
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
};

/// Instruments every function that contains a KLEE function call as nonopt
class OptNonePass : public llvm::PassInfoMixin<OptNonePass> {
public:
  llvm::PreservedAnalyses run(llvm::Function &F, llvm::FunctionAnalysisManager &AM);
};
} // namespace klee

#endif /* KLEE_PASSES_H */
