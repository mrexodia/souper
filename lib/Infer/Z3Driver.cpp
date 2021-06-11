#include "souper/Infer/Z3Driver.h"
#include "souper/Infer/Z3Expr.h"

extern unsigned DebugLevel;

namespace souper {
bool isTransformationValidZ3(souper::Inst *LHS, souper::Inst *RHS,
                           const std::vector<InstMapping> &PCs,
                           const souper::BlockPCs &BPCs,
                           InstContext &IC) {
  Inst *Ante = IC.getConst(llvm::APInt(1, true));
  for (auto PC : PCs ) {
    Inst *Eq = IC.getInst(Inst::Eq, 1, {PC.LHS, PC.RHS});
    Ante = IC.getInst(Inst::And, 1, {Ante, Eq});
  }

  auto Goals = explodePhis(IC, {LHS, RHS, Ante, BPCs});
  // ^ Explanation in AliveDriver.cpp

  if (DebugLevel > 3)
    llvm::errs() << "Number of sub-goals : " << Goals.size() << "\n";
  for (auto Goal : Goals) {
    if (DebugLevel > 3) {
      llvm::errs() << "Goal:\n";
      ReplacementContext RC;
      RC.printInst(Goal.LHS, llvm::errs(), true);
      llvm::errs() << "\n------\n";
    }
    std::vector<Inst *> Vars;
    findVars(Goal.RHS, Vars);
    Z3Driver Verifier(Goal.LHS, Goal.Pre, IC, Vars);
    if (!Verifier.verify(Goal.RHS, Goal.Pre))
      return false;
  }
  return true;
}
}
