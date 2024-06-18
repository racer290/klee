//===-- Instrument.cpp ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PassesNew.h"
#include "ModuleHelper.h"

#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Scalar/Scalarizer.h"
#include "llvm/Transforms/Scalar/LowerAtomicPass.h"

#include "klee/Support/ErrorHandling.h"

#include <iostream>
#include <llvm/Support/raw_os_ostream.h>

using namespace llvm;
using namespace klee;

void klee::instrument(bool CheckDivZero, bool CheckOvershift,
                      llvm::Module *module) {

  LoopAnalysisManager loopan;
  FunctionAnalysisManager funcan;
  CGSCCAnalysisManager callan;
  ModuleAnalysisManager modan;
  PassBuilder builder;
  builder.registerLoopAnalyses(loopan);
  builder.registerFunctionAnalyses(funcan);
  builder.registerCGSCCAnalyses(callan);
  builder.registerModuleAnalyses(modan);
  builder.crossRegisterProxies(loopan, funcan, callan, modan);
  ModulePassManager modman;// = builder.buildPerModuleDefaultPipeline(OptimizationLevel::O0);

  FunctionPassManager klee_funcpasses;

  // Inject checks prior to optimization... we also perform the
  // invariant transformations that we will end up doing later so that
  // optimize is seeing what is as close as possible to the final
  // module.
  klee_funcpasses.addPass(RaiseAsmPass());

  // This pass will scalarize as much code as possible so that the Executor
  // does not need to handle operands of vector type for most instructions
  // other than InsertElementInst and ExtractElementInst.
  //
  // NOTE: Must come before division/overshift checks because those passes
  // don't know how to handle vector instructions.
  klee_funcpasses.addPass(llvm::ScalarizerPass());

  modman.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(klee_funcpasses)));

  // This pass will replace atomic instructions with non-atomic operations
  klee_funcpasses.addPass(llvm::LowerAtomicPass());
  if (CheckDivZero)
    modman.addPass(DivCheckPass());
  if (CheckOvershift)
    modman.addPass(OvershiftCheckPass());

  modman.addPass(IntrinsicCleanerPass());
  std::string s;
  raw_string_ostream ss(s);
  modman.printPipeline(ss, [](auto x) {return x;});
  std::cout << "Pipeline is: " << ss.str() << std::endl;
  modman.run(*module, modan);
}

void klee::checkModule(bool DontVerify, llvm::Module *module) {
  LoopAnalysisManager loopan;
  FunctionAnalysisManager funcan;
  CGSCCAnalysisManager callan;
  ModuleAnalysisManager modan;
  PassBuilder builder;
  builder.registerLoopAnalyses(loopan);
  builder.registerFunctionAnalyses(funcan);
  builder.registerCGSCCAnalyses(callan);
  builder.registerModuleAnalyses(modan);
  builder.crossRegisterProxies(loopan, funcan, callan, modan);
  ModulePassManager modman = builder.buildPerModuleDefaultPipeline(OptimizationLevel::O0);

  if (!DontVerify)
    modan.registerPass([] { return llvm::VerifierAnalysis(); });
  funcan.registerPass([] { std::cout << "registerino" << std::endl; return InstructionOperandTypeCheckPass(); });
  modman.run(*module, modan);

  // Enforce the operand type invariants that the Executor expects.  This
  // implicitly depends on the "Scalarizer" pass to be run in order to succeed
  // in the presence of vector instructions.
  for (Function& f : *module) {
    if (!funcan.getResult<InstructionOperandTypeCheckPass>(f)) {
      klee_error("Unexpected instruction operand types detected in function %s", f.getName().data());
    }
  }
}
