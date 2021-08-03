#ifndef SOUPER_Z3_DRIVER_H
#define SOUPER_Z3_DRIVER_H
#include "llvm/ADT/StringExtras.h"
#include "souper/Infer/Verification.h"
#include "souper/Infer/Z3Expr.h"
#include "souper/Inst/Inst.h"

namespace souper {

class Z3Driver {
public:
  Z3Driver(Inst *LHS_, Inst *PreCondition_, InstContext &IC_,
           std::vector<Inst *> ExtraInputs = {})
    : LHS(LHS_), Precondition(PreCondition_), IC(IC_), TranslatedExprs(ctx), Solver(ctx){
    // TODO: Preprocessing, solver keepalive, variables in solver for reuse

    InstNumbers = 201;
    //201 is chosen arbitrarily.

    Translate(LHS);
    if (Precondition) AddConstraint(Precondition);
  }
  bool verify(Inst *RHS, Inst *RHSAssumptions = nullptr) {
    Translate(RHS);
    if (RHSAssumptions) AddConstraint(RHSAssumptions);

    if (LHS->DemandedBits != 0) {
      auto Mask = ctx.bv_val(toString(LHS->DemandedBits, 10, false).c_str(), LHS->Width);
      Put(LHS, Get(LHS) & Mask);
      Put(RHS, Get(RHS) & Mask);
    }
    Solver.add(Get(LHS) != Get(RHS));

    if (Solver.check() == z3::unsat) {
      return true;
    } else {
      // TODO: Model
      return false;
    }

  }
private:
  Inst *LHS;
  Inst *Precondition;
  InstContext &IC;
  std::map<const Inst *, std::string> NamesCache;
  std::map<Inst *, size_t> ExprCache;
  z3::context ctx;
  z3::expr_vector TranslatedExprs;
  z3::solver Solver;

  bool isCached(Inst *I) {
    return ExprCache.find(I) != ExprCache.end();
  }

  z3::expr Get(Inst *I) {
    return TranslatedExprs[ExprCache[I]];
  }
  void Put(Inst *I, z3::expr E) {
    TranslatedExprs.push_back(E);
    ExprCache[I] = TranslatedExprs.size() - 1;
  }

  void AddConstraint(Inst *I) {
    Translate(I);
    Solver.add(Get(I) == ctx.bv_val(1, 1));
  }

  int InstNumbers;

  void addExtraPreds(souper::Inst *I) {
    if (I->K == Inst::Kind::UDiv || I->K == Inst::Kind::SDiv
        || I->K == Inst::Kind::SDivExact || I->K == Inst::Kind::UDivExact
        || I->K == Inst::Kind::URem || I->K == Inst::Kind::SRem) {
      Solver.add(Get(I->Ops[1]) != ctx.bv_val(0, I->Width));
    }

    if (I->K == Inst::Kind::Shl || I->K == Inst::Kind::LShr
        || I->K == Inst::Kind::AShr || I->K == Inst::Kind::AShrExact
        || I->K == Inst::Kind::LShrExact) {
      Solver.add(z3::ult(Get(I->Ops[1]), ctx.bv_val(I->Width, I->Width)));
    }

    if (I->K == Inst::Kind::AddNSW || I->K == Inst::Kind::AddNW) {
      Solver.add(z3expr::add_no_soverflow(Get(I->Ops[0]), Get(I->Ops[1])));
    }

    if (I->K == Inst::Kind::AddNUW || I->K == Inst::Kind::AddNW) {
      Solver.add(z3expr::add_no_uoverflow(Get(I->Ops[0]), Get(I->Ops[1])));
    }

    if (I->K == Inst::Kind::SubNSW || I->K == Inst::Kind::SubNW) {
      Solver.add(z3expr::sub_no_soverflow(Get(I->Ops[0]), Get(I->Ops[1])));
    }
    if (I->K == Inst::Kind::SubNUW || I->K == Inst::Kind::SubNW) {
      Solver.add(z3expr::sub_no_uoverflow(Get(I->Ops[0]), Get(I->Ops[1])));
    }

    if (I->K == Inst::Kind::MulNSW || I->K == Inst::Kind::MulNW) {
      Solver.add(z3expr::mul_no_soverflow(Get(I->Ops[0]), Get(I->Ops[1])));
    }
    if (I->K == Inst::Kind::MulNUW || I->K == Inst::Kind::MulNW) {
      Solver.add(z3expr::mul_no_uoverflow(Get(I->Ops[0]), Get(I->Ops[1])));
    }

    if (I->K == Inst::Kind::ShlNSW || I->K == Inst::Kind::ShlNW) {
      Solver.add(z3expr::shl_no_soverflow(Get(I->Ops[0]), Get(I->Ops[1])));
    }
    if (I->K == Inst::Kind::ShlNUW || I->K == Inst::Kind::ShlNW) {
      Solver.add(z3expr::shl_no_uoverflow(Get(I->Ops[0]), Get(I->Ops[1])));
    }

    if (I->K == Inst::Kind::UDivExact) {
      Solver.add(z3expr::udiv_exact(Get(I->Ops[0]), Get(I->Ops[1])));
    }
    if (I->K == Inst::Kind::SDivExact) {
      Solver.add(z3expr::sdiv_exact(Get(I->Ops[0]), Get(I->Ops[1])));
    }

    if (I->K == Inst::Kind::AShrExact) {
      Solver.add(z3expr::ashr_exact(Get(I->Ops[0]), Get(I->Ops[1])));
    }
    if (I->K == Inst::Kind::LShrExact) {
      Solver.add(z3expr::lshr_exact(Get(I->Ops[0]), Get(I->Ops[1])));
    }
  }

  bool Translate(souper::Inst *I) {
    if (!I) return true;
    // unused translation; this is souper's internal instruction to represent overflow instructions
    if (souper::Inst::isOverflowIntrinsicSub(I->K)) {
      return true;
    }

    if (isCached(I)) {
      return true;
    }

    auto Ops = I->Ops;
    if (souper::Inst::isOverflowIntrinsicMain(I->K)) {
      Ops = Ops[0]->Ops;
    }

    for (auto &&Op : Ops) {
      if (!Translate(Op)) {
        return false;
      }
      if (I->K == Inst::ExtractValue) {
        break; // Break after translating main arg, idx is handled separately.
      }

    }

    std::string Name = "";

    if (NamesCache.find(I) != NamesCache.end()) {
      Name = NamesCache[I];
    } else if (I->Name != "") {
      if (I->SynthesisConstID != 0) {
        Name = "%" + souper::ReservedConstPrefix + std::to_string(I->SynthesisConstID);
      } else {
        Name = "%var_" + I->Name;
      }
    } else {
      Name = "%" + std::to_string(InstNumbers++);
    }
    if (I->K == Inst::Var) {
      NamesCache[I] = Name;
    }

    auto W = I->Width;
    addExtraPreds(I);
    switch (I->K) {
      case souper::Inst::Var: {
        Put(I, ctx.bv_const(Name.c_str(), W));
        auto DFC = getDataflowConditions(I, IC);
        if (DFC) AddConstraint(DFC);
        return true;
      }
      case souper::Inst::Hole: {
        llvm::report_fatal_error("Holes unimplemented in Z3Driver.");
      }
      case souper::Inst::Const: {
        Put(I, ctx.bv_val(toString(I->Val, 10, false).c_str(), W));
        // inefficient?
        return true;
      }

      case souper::Inst::Phi: {
        if (Ops.size() != 1) {
          assert(false && "Phi with muliple arguments unimplemented");
          return false;
        }
        Put(I, Get(Ops[0]));
        return true;
      }

      case souper::Inst::ExtractValue: {
        unsigned idx = I->Ops[1]->Val.getLimitedValue();
        assert(idx <= 1 && "Only extractvalue with overflow instructions are supported.");
        Put(I, z3expr::ExtractValue(Get(I->Ops[0]), idx));
        return true;
      }

      #define UNOP(SOUPER, Z3) case souper::Inst::SOUPER: {       \
        Put(I, z3expr::Z3(Get(Ops[0])));                          \
        return true;                                              \
      }
      #define UNOPC(SOUPER, Z3) case souper::Inst::SOUPER: {      \
        Put(I, z3expr::Z3(Get(Ops[0]), I->Width));                \
        return true;                                              \
      }
      #define BINOP(SOUPER, Z3) case souper::Inst::SOUPER: {      \
        Put(I, z3expr::Z3(Get(Ops[0]), Get(Ops[1])));             \
        return true;                                              \
      }
      #define TERNOP(SOUPER, Z3) case souper::Inst::SOUPER: {     \
        Put(I, z3expr::Z3(Get(Ops[0]), Get(Ops[1]), Get(Ops[2])));\
        return true;                                              \
      }

      UNOP(Freeze, Freeze); UNOP(CtPop, CtPop); UNOP(BSwap, BSwap);
      UNOP(BitReverse, BitReverse); UNOP(Cttz, Cttz); UNOP(Ctlz, Ctlz);

      UNOPC(ZExt, ZExt); UNOPC(SExt, SExt); UNOPC(Trunc, Trunc);

      BINOP(Add, Add); BINOP(AddNSW, Add); BINOP(AddNUW, Add); BINOP(AddNW, Add);
      BINOP(Sub, Sub); BINOP(SubNSW, Sub); BINOP(SubNUW, Sub); BINOP(SubNW, Sub);
      BINOP(Mul, Mul); BINOP(MulNSW, Mul); BINOP(MulNUW, Mul); BINOP(MulNW, Mul);
      BINOP(Shl, Shl); BINOP(ShlNSW, Shl); BINOP(ShlNUW, Shl); BINOP(ShlNW, Shl);
      BINOP(And, And); BINOP(Or, Or); BINOP(Xor, Xor);
      BINOP(LShr, LShr); BINOP(LShrExact, LShr); BINOP(AShr, AShr); BINOP(AShrExact, AShr);
      BINOP(URem, URem); BINOP(SRem, SRem); BINOP(UDiv, UDiv); BINOP(UDivExact, UDiv);
      BINOP(SDiv, SDiv); BINOP(SDivExact, SDiv); BINOP(SAddSat, SAddSat);
      BINOP(UAddSat, SAddSat); BINOP(SSubSat, SSubSat); BINOP(USubSat, USubSat);
      BINOP(SAddWithOverflow, SAddWithOverflow); BINOP(UAddWithOverflow, UAddWithOverflow);
      BINOP(SSubWithOverflow, SSubWithOverflow); BINOP(USubWithOverflow, USubWithOverflow);
      BINOP(SMulWithOverflow, SMulWithOverflow); BINOP(UMulWithOverflow, UMulWithOverflow);
      BINOP(Eq, Eq); BINOP(Ne, Ne); BINOP(Ule, Ule);
      BINOP(Ult, Ult); BINOP(Sle, Sle); BINOP(Slt, Slt);

      TERNOP(Select, Select); TERNOP(FShl, FShl); TERNOP(FShr, FShr);

      default: llvm::report_fatal_error("Unimplemented instruction.");
    }
  }
};


bool isTransformationValidZ3(souper::Inst *LHS, souper::Inst *RHS,
                           const std::vector<InstMapping> &PCs,
                           const souper::BlockPCs &BPCs,
                           InstContext &IC);

}

#endif
