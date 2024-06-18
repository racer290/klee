// FIXME: This file is a bastard child of opt.cpp and llvm-ld's
// Optimize.cpp. This stuff should live in common code.


//===- Optimize.cpp - Optimize a complete program -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements all optimization of the linked module for llvm-ld.
//
//===----------------------------------------------------------------------===//

#include "ModuleHelper.h"
#include "PassesNew.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Transforms/Scalar/Scalarizer.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"

#include "klee/Support/OptionCategories.h"

namespace {
using namespace llvm;
static cl::opt<bool>
    DisableInline("disable-inlining",
                  cl::desc("Do not run the inliner pass (default=false)"),
                  cl::init(false), cl::cat(klee::ModuleCat));

static cl::opt<bool> DisableInternalize(
    "disable-internalize",
    cl::desc("Do not mark all symbols as internal (default=false)"),
    cl::init(false), cl::cat(klee::ModuleCat));

static cl::opt<bool> VerifyEach("verify-each",
                                cl::desc("Verify intermediate results of all "
                                         "optimization passes (default=false)"),
                                cl::init(false), cl::cat(klee::ModuleCat));

static cl::opt<bool>
    Strip("strip-all", cl::desc("Strip all symbol information from executable (default=false)"),
          cl::init(false), cl::cat(klee::ModuleCat));

static cl::opt<bool>
    StripDebug("strip-debug",
               cl::desc("Strip debugger symbol info from executable (default=false)"),
               cl::init(false), cl::cat(klee::ModuleCat));

// A utility function that adds a pass to the pass manager but will also add
// a verifier pass after if we're supposed to verify.
static inline void addPass(legacy::PassManager &PM, Pass *P) {
  // Add the pass to the pass manager...
  PM.add(P);

  // If we are verifying all of the intermediate steps, add the verifier...
  if (VerifyEach)
    PM.add(createVerifierPass());
}
} // namespace

using namespace klee;
void klee::optimiseAndPrepare(bool OptimiseKLEECall, bool Optimize,
                              SwitchImplType SwitchType, std::string EntryPoint,
                              llvm::ArrayRef<const char *> preservedFunctions,
                              llvm::Module *module) {
  llvm::LoopAnalysisManager loopan;
  llvm::FunctionAnalysisManager funcan;
  llvm::CGSCCAnalysisManager callan;
  llvm::ModuleAnalysisManager modan;
  llvm::PassBuilder builder;
  builder.registerLoopAnalyses(loopan);
  builder.registerFunctionAnalyses(funcan);
  builder.registerCGSCCAnalyses(callan);
  builder.registerModuleAnalyses(modan);
  builder.crossRegisterProxies(loopan, funcan, callan, modan);
  llvm::ModulePassManager modman = builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O1);
  // Preserve all functions containing klee-related function calls from being
  // optimised around
  if (!OptimiseKLEECall) {
    // probably doesn't hurt to keep this for later
    modman.addPass(llvm::createModuleToFunctionPassAdaptor(OptNonePass()));
    modman.run(*module, modan);
  }

  if (Optimize) {
    //optimizeModule(module, preservedFunctions);
    llvm::LoopAnalysisManager loopan;
    llvm::FunctionAnalysisManager funcan;
    llvm::CGSCCAnalysisManager callan;
    llvm::ModuleAnalysisManager modan;
    llvm::PassBuilder builder;
    builder.registerLoopAnalyses(loopan);
    builder.registerFunctionAnalyses(funcan);
    builder.registerCGSCCAnalyses(callan);
    builder.registerModuleAnalyses(modan);
    builder.crossRegisterProxies(loopan, funcan, callan, modan);
    llvm::ModulePassManager modman = builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2, /* LTOPreLink = */true);
  }

  // Needs to happen after linking (since ctors/dtors can be modified)
  // and optimization (since global optimization can rewrite lists).
  injectStaticConstructorsAndDestructors(module, EntryPoint);

  llvm::FunctionPassManager klee_funcpasses;
  // Finally, run the passes that maintain invariants we expect during
  // interpretation. We run the intrinsic cleaner just in case we
  // linked in something with intrinsics but any external calls are
  // going to be unresolved. We really need to handle the intrinsics
  // directly I think?
  //modman.addPass(createCFGSimplificationPass());
  switch (SwitchType) {
  case SwitchImplType::eSwitchTypeInternal:
    break;
  case SwitchImplType::eSwitchTypeSimple:
    klee_funcpasses.addPass(LowerSwitchPass());
    break;
  case SwitchImplType::eSwitchTypeLLVM:
    klee_funcpasses.addPass(llvm::LowerSwitchPass());
    break;
  }

  modman.addPass(IntrinsicCleanerPass());
  klee_funcpasses.addPass(llvm::ScalarizerPass());
  klee_funcpasses.addPass(PhiCleanerPass());
  modman.addPass(FunctionAliasPass());
  modman.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(klee_funcpasses)));
  modman.run(*module, modan);
}
