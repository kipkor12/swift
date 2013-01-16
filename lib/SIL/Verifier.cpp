//===--- Verifier.cpp - Verification of Swift SIL Code --------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/SIL/Function.h"
#include "swift/SIL/SILVisitor.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Types.h"
#include "swift/AST/Decl.h"
#include "llvm/Support/Debug.h"

using namespace swift;

namespace {
/// SILVerifier class - This class implements the SIL verifier, which walks over
/// SIL, checking and enforcing its invariants.
class SILVerifier : public SILVisitor<SILVerifier> {
  Function const &F;
public:
  SILVerifier(Function const &F) : F(F) {}
  
  void visit(Instruction *I) {
    
    const BasicBlock *BB = I->getParent();
    (void)BB;
    // Check that non-terminators look ok.
    if (!isa<TermInst>(I)) {
      assert(!BB->empty() &&
             "Can't be in a parent block if it is empty");
      assert(&*BB->getInsts().rbegin() != I &&
             "Non-terminators cannot be the last in a block");
    } else {
      assert(&*BB->getInsts().rbegin() == I &&
             "Terminator must be the last in block");
    }

    
    // Dispatch to our more-specialized instances below.
    ((SILVisitor<SILVerifier>*)this)->visit(I);
  }

  void visitAllocVarInst(AllocVarInst *AI) {
    assert(AI->getType().isAddress() && "alloc_var must return address");
  }
  
  void visitAllocRefInst(AllocRefInst *AI) {
    assert(AI->getType().hasReferenceSemantics() && !AI->getType().isAddress()
           && "alloc_ref must return reference type value");
  }
  
  void visitApplyInst(ApplyInst *AI) {
    
    DEBUG(llvm::dbgs() << "verifying";
          AI->print(llvm::dbgs()));
    SILType calleeTy = AI->getCallee().getType();
    DEBUG(llvm::dbgs() << "callee type: ";
          AI->getCallee().getType().print(llvm::dbgs());
          llvm::dbgs() << '\n');
    assert(!calleeTy.isAddress() && "callee of apply cannot be an address");
    assert(calleeTy.is<FunctionType>() &&
           "callee of apply must have concrete function type");
    SILFunctionTypeInfo *ti = F.getModule().getFunctionTypeInfo(calleeTy);
    (void)ti;
    
    DEBUG(llvm::dbgs() << "function input types:\n";
          for (SILType t : ti->getInputTypes()) {
            llvm::dbgs() << "  ";
            t.print(llvm::dbgs());
            llvm::dbgs() << '\n';
          });
    DEBUG(llvm::dbgs() << "function result type ";
          ti->getResultType().print(llvm::dbgs());
          llvm::dbgs() << '\n');

    DEBUG(llvm::dbgs() << "argument types:\n";
          for (Value arg : AI->getArguments()) {
            llvm::dbgs() << "  ";
            arg.getType().print(llvm::dbgs());
            llvm::dbgs() << '\n';
          });
    
    // Check that the arguments and result match.
    assert(AI->getArguments().size() == ti->getInputTypes().size() &&
           "apply doesn't have right number of arguments for function");
    for (size_t i = 0, size = AI->getArguments().size(); i < size; ++i) {
      DEBUG(llvm::dbgs() << "  argument type ";
            AI->getArguments()[i].getType().print(llvm::dbgs());
            llvm::dbgs() << " for input type ";
            ti->getInputTypes()[i].print(llvm::dbgs());
            llvm::dbgs() << '\n');
      assert(AI->getArguments()[i].getType() == ti->getInputTypes()[i] &&
             "input types to apply don't match function input types");
    }
    assert(AI->getType() == ti->getResultType() &&
           "type of apply instruction doesn't match function result type");
  }

  void visitConstantRefInst(ConstantRefInst *CRI) {
    assert(CRI->getType().is<AnyFunctionType>() &&
           "constant_ref should have a function result");
  }

  void visitIntegerLiteralInst(IntegerLiteralInst *ILI) {
    assert(ILI->getType().is<BuiltinIntegerType>() &&
           "invalid integer literal type");
  }
  void visitLoadInst(LoadInst *LI) {
    assert(!LI->getType().isAddress() && "Can't load an address");
    assert(LI->getLValue().getType().isAddress() &&
           "Load operand must be an address");
    assert(LI->getLValue().getType().getObjectType() == LI->getType() &&
           "Load operand type and result type mismatch");
  }

  void visitStoreInst(StoreInst *SI) {
    assert(!SI->getSrc().getType().isAddress() &&
           "Can't store from an address source");
    assert(SI->getDest().getType().isAddress() &&
           "Must store to an address dest");
    assert(SI->getDest().getType().getObjectType() == SI->getSrc().getType() &&
           "Store operand type and dest type mismatch");
  }

  void visitCopyAddrInst(CopyAddrInst *SI) {
    assert(SI->getSrc().getType().isAddress() &&
           "Src value should be lvalue");
    assert(SI->getDest().getType().isAddress() &&
           "Dest address should be lvalue");
    assert(SI->getDest().getType() == SI->getSrc().getType() &&
           "Store operand type and dest type mismatch");
  }
  
  void visitZeroAddrInst(ZeroAddrInst *ZI) {
    assert(ZI->getDest().getType().isAddress() &&
           "Dest address should be lvalue");
  }
  
  void visitZeroValueInst(ZeroValueInst *ZVI) {
    assert(!ZVI->getType().isAddress() &&
           "zero_value cannot create an address");
  }
  
  void visitSpecializeInst(SpecializeInst *SI) {
    assert(SI->getType().is<FunctionType>() &&
           "Specialize dest should be a function type");
    assert(SI->getOperand().getType().is<PolymorphicFunctionType>() &&
           "Specialize source should be a polymorphic function type");
  }

  void visitTupleInst(TupleInst *TI) {
    assert(TI->getType().is<TupleType>() && "TupleInst should return a tuple");
    TupleType *ResTy = TI->getType().castTo<TupleType>(); (void)ResTy;

    assert(TI->getElements().size() == ResTy->getFields().size() &&
           "Tuple field count mismatch!");
  }
  void visitMetatypeInst(MetatypeInst *MI) {
    assert(MI->getType(0).is<MetaTypeType>() &&
           "metatype instruction must be of metatype type");
  }
  void visitAssociatedMetatypeInst(AssociatedMetatypeInst *MI) {
    assert(MI->getType(0).is<MetaTypeType>() &&
           "associated_metatype instruction must be of metatype type");
    assert(MI->getSourceMetatype().getType().is<MetaTypeType>() &&
           "associated_metatype operand must be of metatype type");
  }
  
  void visitRetainInst(RetainInst *RI) {
    assert(!RI->getOperand().getType().isAddress() &&
           "Operand of retain must not be address");
    assert(RI->getOperand().getType().hasReferenceSemantics() &&
           "Operand of dealloc_ref must be reference type");
  }
  void visitReleaseInst(ReleaseInst *RI) {
    assert(!RI->getOperand().getType().isAddress() &&
           "Operand of release must not be address");
    assert(RI->getOperand().getType().hasReferenceSemantics() &&
           "Operand of dealloc_ref must be reference type");
  }
  void visitDeallocVarInst(DeallocVarInst *DI) {
    assert(DI->getOperand().getType().isAddress() &&
           "Operand of dealloc_var must be address");
  }
  void visitDeallocRefInst(DeallocRefInst *DI) {
    assert(!DI->getOperand().getType().isAddress() &&
           "Operand of dealloc_ref must not be address");
    assert(DI->getOperand().getType().hasReferenceSemantics() &&
           "Operand of dealloc_ref must be reference type");
  }
  void visitDestroyAddrInst(DestroyAddrInst *DI) {
    assert(DI->getOperand().getType().isAddressOnly() &&
           "Operand of destroy_addr must be address-only");
  }

  void visitIndexAddrInst(IndexAddrInst *IAI) {
    assert(IAI->getType().isAddress() &&
           IAI->getType() == IAI->getOperand().getType() &&
           "invalid IndexAddrInst");
  }
  
  void visitExtractInst(ExtractInst *EI) {
#ifndef NDEBUG
    SILType operandTy = EI->getOperand().getType();
    assert(!operandTy.isAddress() &&
           "cannot extract from address");
    assert(!operandTy.hasReferenceSemantics() &&
           "cannot extract from reference type");
    assert(!EI->getType(0).isAddress() &&
           "result of extract cannot be address");
#endif
  }

  void visitElementAddrInst(ElementAddrInst *EI) {
#ifndef NDEBUG
    SILType operandTy = EI->getOperand().getType();
    assert(operandTy.isAddress() &&
           "must derive element_addr from address");
    assert(!operandTy.hasReferenceSemantics() &&
           "cannot derive element_addr from reference type");
    assert(EI->getType(0).isAddress() &&
           "result of element_addr must be lvalue");
#endif
  }
  
  void visitRefElementAddrInst(RefElementAddrInst *EI) {
#ifndef NDEBUG
    SILType operandTy = EI->getOperand().getType();
    assert(!operandTy.isAddress() &&
           "must derive ref_element_addr from non-address");
    assert(operandTy.hasReferenceSemantics() &&
           "must derive ref_element_addr from reference type");
    assert(EI->getType(0).isAddress() &&
           "result of ref_element_addr must be lvalue");
#endif
  }
  
  void visitArchetypeMethodInst(ArchetypeMethodInst *AMI) {
#ifndef NDEBUG
    DEBUG(llvm::dbgs() << "verifying";
          AMI->print(llvm::dbgs()));
    FunctionType *methodType = AMI->getType(0).getAs<FunctionType>();
    DEBUG(llvm::dbgs() << "method type ";
          methodType->print(llvm::dbgs());
          llvm::dbgs() << "\n");
    assert(methodType &&
           "result method must be of a concrete function type");
    SILType operandType = AMI->getOperand().getType();
    DEBUG(llvm::dbgs() << "operand type ";
          operandType.print(llvm::dbgs());
          llvm::dbgs() << "\n");
    assert(methodType->getInput()->isEqual(operandType.getSwiftType()) &&
           "result must be a method of the operand");
    assert(methodType->getResult()->is<FunctionType>() &&
           "result must be a method");
    if (operandType.isAddress()) {
      assert(operandType.is<ArchetypeType>() &&
             "archetype_method must apply to an archetype address");
    } else if (MetaTypeType *mt = operandType.getAs<MetaTypeType>()) {
      assert(mt->getInstanceType()->is<ArchetypeType>() &&
             "archetype_method must apply to an archetype metatype");
    } else
      llvm_unreachable("method must apply to an address or metatype");
#endif
  }
  
  void visitProtocolMethodInst(ProtocolMethodInst *EMI) {
#ifndef NDEBUG
    FunctionType *methodType = EMI->getType(0).getAs<FunctionType>();
    assert(methodType &&
           "result method must be of a concrete function type");
    SILType operandType = EMI->getOperand().getType();
    assert(methodType->getInput()->isEqual(
                            operandType.getASTContext().TheRawPointerType) &&
           "result must be a method of raw pointer");
    assert(methodType->getResult()->is<FunctionType>() &&
           "result must be a method");
    assert(operandType.isAddress() &&
           "protocol_method must apply to an existential address");
    assert(operandType.isExistentialType() &&
           "protocol_method must apply to an existential address");    
#endif
  }
  
  void visitProjectExistentialInst(ProjectExistentialInst *PEI) {
#ifndef NDEBUG
    SILType operandType = PEI->getOperand().getType();
    assert(operandType.isAddress() && "project_existential must be applied to address");
    assert(operandType.isExistentialType() &&
           "project_existential must be applied to address of existential");
#endif
  }
  
  void visitInitExistentialInst(InitExistentialInst *AEI) {
#ifndef NDEBUG
    SILType exType = AEI->getExistential().getType();
    assert(exType.isAddress() &&
           "init_existential must be applied to an address");
    assert(exType.isExistentialType() &&
           "init_existential must be applied to address of existential");
#endif
  }
  
  void visitDeinitExistentialInst(DeinitExistentialInst *DEI) {
#ifndef NDEBUG
    SILType exType = DEI->getExistential().getType();
    assert(exType.isAddress() &&
           "deinit_existential must be applied to an address");
    assert(exType.isExistentialType() &&
           "deinit_existential must be applied to address of existential");
#endif
  }
  
  void visitArchetypeToSuperInst(ArchetypeToSuperInst *ASI) {
    assert(ASI->getOperand().getType().isAddressOnly() &&
           "archetype_to_super operand must be an address");
    assert(ASI->getOperand().getType().is<ArchetypeType>() &&
           "archetype_to_super operand must be archetype");
    assert(ASI->getType().hasReferenceSemantics() &&
           "archetype_to_super must convert to a reference type");
  }
  
  void visitSuperToArchetypeInst(SuperToArchetypeInst *SAI) {
    assert(SAI->getSrcBase().getType().hasReferenceSemantics() &&
           "super_to_archetype source must be a reference type");
    assert(SAI->getDestArchetypeAddress().getType().is<ArchetypeType>() &&
           "super_to_archetype dest must be an archetype address");
  }
  
  void visitDowncastInst(DowncastInst *DI) {
    assert(DI->getOperand().getType().hasReferenceSemantics() &&
           "downcast operand must be a reference type");
    assert(DI->getType().hasReferenceSemantics() &&
           "downcast must convert to a reference type");
  }
  
  void visitIntegerValueInst(IntegerValueInst *IVI) {
    assert(IVI->getType().is<BuiltinIntegerType>() &&
           "invalid integer value type");
  }

  void visitReturnInst(ReturnInst *RI) {
    DEBUG(RI->print(llvm::dbgs()));
    assert(RI->getReturnValue() && "Return of null value is invalid");
    
    // FIXME: when curry entry points are typed properly, verify return type
    // here.
    /*
    SILFunctionTypeInfo *ti =
      F.getModule().getFunctionTypeInfo(F.getLoweredType());
    DEBUG(llvm::dbgs() << "  operand type ";
          RI->getReturnValue().getType().print(llvm::dbgs());
          llvm::dbgs() << " for return type ";
          ti->getResultType().print(llvm::dbgs());
          llvm::dbgs() << '\n');
    assert(RI->getReturnValue().getType() == ti->getResultType() &&
           "return value type does not match return type of function");
     */
  }
  
  void visitBranchInst(BranchInst *BI) {
  }
  
  void visitCondBranchInst(CondBranchInst *CBI) {
    assert(CBI->getCondition() &&
           "Condition of conditional branch can't be missing");
  }
  
  void verify() {
    visitFunction(const_cast<Function*>(&F));
  }
};
} // end anonymous namespace


/// verify - Run the IR verifier to make sure that the Function follows
/// invariants.
void Function::verify() const {
  SILVerifier(*this).verify();
}
