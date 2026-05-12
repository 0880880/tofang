#include "ast.h"
#include "decl.h"
#include "irctx.h"
#include "type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/raw_ostream.h"
#include <iostream>
#include <stdexcept>
#include <string>

static llvm::Value* cast(const IRContext& ir, llvm::Value* val, TypeThing* from,
    TypeThing* to)
{
    if (from == to || from->kind == to->kind) {
        return val;
    }
    if (isFloat(from) && isFloat(to)) {
        if (from->kind == TypeKind::F32) {
            return ir.builder.CreateFPExt(val, to->getLLVM(ir));
        } else {
            return ir.builder.CreateFPTrunc(val, to->getLLVM(ir));
        }
    }
    if (isInt(from) && isFloat(to)) {
        if (isSigned(from)) {
            return ir.builder.CreateSIToFP(val, to->getLLVM(ir));
        } else {
            return ir.builder.CreateUIToFP(val, to->getLLVM(ir));
        }
    }
    if (isInt(from) && isInt(to)) {
        if (isSigned(from)) {
            return ir.builder.CreateSExt(val, to->getLLVM(ir)); // FIXME idk
        } else {
            return ir.builder.CreateZExt(val, to->getLLVM(ir)); // FIXME idk
        }
    }
    throw std::runtime_error("Unhandled type: " + from->toString() + " -> " + to->toString());
}

llvm::Value* LiteralExpr::codegen(IRContext& ir)
{
    switch (t->kind) {
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
        return llvm::ConstantInt::get(t->getLLVM(ir), static_cast<uint64_t>(std::stol(value.value)),
            true);
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
        return llvm::ConstantInt::get(t->getLLVM(ir), static_cast<uint64_t>(std::stol(value.value)),
            false);
    case TypeKind::F32:
    case TypeKind::F64:
        return llvm::ConstantFP::get(t->getLLVM(ir), std::stod(value.value));
    case TypeKind::UNTYPED_INT:
        return llvm::ConstantInt::get(ir.builder.getInt64Ty(),
            static_cast<uint64_t>(std::stol(value.value)));
    case TypeKind::UNTYPED_FLOAT:
        return llvm::ConstantFP::get(ir.builder.getDoubleTy(),
            std::stod(value.value));
    case TypeKind::BOOL:
        return value.value == "true" ? llvm::ConstantInt::getTrue(ir.ctx)
                                     : llvm::ConstantInt::getFalse(ir.ctx);
    case TypeKind::I_NULL:
        return llvm::ConstantPointerNull::get(
            ir.builder.getPtrTy());
    }
    return nullptr;
}

llvm::Value* BinaryExpr::codegen(IRContext& ir)
{
    string v = op.value;
    auto lhs = cast(ir, left->codegen(ir), left->t, t);
    auto rhs = cast(ir, right->codegen(ir), right->t, t);
    if (v == "+") {
        if (isFloat(t)) {
            return ir.builder.CreateFAdd(lhs, rhs, "fp_add");
        } else {
            return ir.builder.CreateAdd(lhs, rhs, "i_add");
        }
    } else if (v == "-") {
        if (isFloat(t)) {
            return ir.builder.CreateFSub(lhs, rhs, "fp_sub");
        } else {
            return ir.builder.CreateSub(lhs, rhs, "i_sub");
        }
    } else if (v == "*") {
        if (isFloat(t)) {
            return ir.builder.CreateFMul(lhs, rhs, "fp_mul");
        } else {
            return ir.builder.CreateMul(lhs, rhs, "i_mul");
        }
    } else if (v == "/") {
        if (isFloat(t)) {
            return ir.builder.CreateFDiv(lhs, rhs, "fp_div");
        } else {
            return isSigned(t) ? ir.builder.CreateSDiv(lhs, rhs, "signed_div")
                               : ir.builder.CreateUDiv(lhs, rhs, "unsigned_div");
        }
    } else if (v == "&" || v == "&&") {
        return ir.builder.CreateAnd(lhs, rhs, "and");
    } else if (v == "|" || v == "||") {
        return ir.builder.CreateOr(lhs, rhs, "or");
    } else if (v == "^") {
        return ir.builder.CreateXor(lhs, rhs, "xor");
    } else if (v == "==") {
        if (isFloat(t)) {
            return ir.builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OEQ, lhs,
                rhs, "fcmp_eq");
        } else {
            return ir.builder.CreateICmp(llvm::CmpInst::Predicate::ICMP_EQ, lhs,
                rhs, "icmp_eq");
        }
    } else if (v == "!=") {
        if (isFloat(t)) {
            return ir.builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_ONE, lhs,
                rhs, "fcmp_neq");
        } else {
            return ir.builder.CreateICmp(llvm::CmpInst::Predicate::ICMP_NE, lhs,
                rhs, "icmp_neq");
        }
    } else if (v == ">") {
        if (isFloat(t)) {
            return ir.builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OGT, lhs,
                rhs, "fcmp_gt");
        } else {
            return ir.builder.CreateICmp(isSigned(t)
                    ? llvm::CmpInst::Predicate::ICMP_SGT
                    : llvm::CmpInst::Predicate::ICMP_UGT,
                lhs, rhs, "icmp_gt");
        }
    } else if (v == "<") {
        if (isFloat(t)) {
            return ir.builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OLT, lhs,
                rhs, "fcmp_lt");
        } else {
            return ir.builder.CreateICmp(isSigned(t)
                    ? llvm::CmpInst::Predicate::ICMP_SLT
                    : llvm::CmpInst::Predicate::ICMP_ULT,
                lhs, rhs, "icmp_lt");
        }
    } else if (v == ">=") {
        if (isFloat(t)) {
            return ir.builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OGE, lhs,
                rhs, "fcmp_ge");
        } else {
            return ir.builder.CreateICmp(isSigned(t)
                    ? llvm::CmpInst::Predicate::ICMP_SGE
                    : llvm::CmpInst::Predicate::ICMP_UGE,
                lhs, rhs, "icmp_ge");
        }
    } else if (v == "<=") {
        if (isFloat(t)) {
            return ir.builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OLE, lhs,
                rhs, "fcmp_le");
        } else {
            return ir.builder.CreateICmp(isSigned(t)
                    ? llvm::CmpInst::Predicate::ICMP_SLE
                    : llvm::CmpInst::Predicate::ICMP_ULE,
                lhs, rhs, "icmp_le");
        }
    }
    // TODO handle comparissons
    throw runtime_error("Unhandled operation " + v);
}

llvm::Value* UnaryExpr::codegen(IRContext& ir)
{
    string v = op.value;
    auto rhs = right->codegen(ir);
    if (v == "+") {
        return rhs;
    } else if (v == "-") {
        if (isInt(right->t)) {
            return ir.builder.CreateSub(llvm::ConstantInt::get(rhs->getType(), 0),
                rhs);
        } else if (isFloat(right->t)) {
            return ir.builder.CreateFSub(llvm::ConstantFP::get(rhs->getType(), 0),
                rhs);
        }
    } else if (v == "*") {
        if (ir.unpack_stored) {
            auto rhs = right->codegen(ir);
            return ir.builder.CreateLoad(std::get<PtrType>(right->t->data).pointee->getLLVM(ir), rhs, "deref");
        } else {
            llvm::Value* rhs = nullptr;
            {
                IRCOptions _(ir);
                _.unpackStored();
                rhs = right->codegen(ir);
            }
            return rhs;
        }
    }
    throw runtime_error("Unexpected error UnaryExpr");
}

llvm::Value* GroupingExpr::codegen(IRContext& ir)
{
    return expression->codegen(ir);
}

llvm::Value* VariableExpr::codegen(IRContext& ir)
{
    if (ir.unpack_stored) {
        if (decl->kind == DeclKind::FUNC) {
            const FuncDecl& fd = std::get<FuncDecl>(decl->data);
            return fd.llvm;
        }
        if (decl->func_param) {
            return ir.current_function->getArg(std::get<VarDecl>(decl->data).arg_index);
        }
        return ir.builder.CreateLoad(decl->toType()->getLLVM(ir), decl->alloca);
    } else {
        if (decl->kind == DeclKind::FUNC) {
            throw runtime_error("function has no address as variable lvalue");
        }
        if (decl->func_param) {
            return ir.current_function->getArg(std::get<VarDecl>(decl->data).arg_index);
        }
        return decl->alloca;
    }
}

llvm::Value* AttribExpr::codegen(IRContext& ir)
{
    llvm::Value* lhs = nullptr;
    {
        IRCOptions _(ir);
        _.packStored();
        lhs = foo->codegen(ir);
    }
    if (foo->t->kind == TypeKind::STRUCT) {
        auto* decl = std::get<StructType>(foo->t->data).str;
        auto& ddata = std::get<StructDecl>(decl->data);
        ir.attrib_struct = lhs;
        for (unsigned int i = 0; i < ddata.methods.size(); i++) {
            if (ddata.methods[i]->name.value == bar.value) {
                if (!ir.unpack_stored) {
                    throw runtime_error("Invalid");
                } else {
                    return std::get<FuncDecl>(ddata.methods[i]->data).llvm;
                }
            }
        }
        for (unsigned int i = 0; i < ddata.fieldNames.size(); i++) {
            if (ddata.fieldNames[i].value == bar.value) {
                llvm::Value* ptr = ir.builder.CreateStructGEP(ddata.llvm, lhs, i, "getl_" + std::to_string(i) + "_" + decl->name.value);

                if (ir.unpack_stored) {
                    llvm::Type* field_type = ddata.fieldTypes[i]->getLLVM(ir); // Assuming fieldDefs hold the types
                    return ir.builder.CreateLoad(field_type, ptr, "getr_" + std::to_string(i) + "_" + decl->name.value);
                } else {
                    return ptr;
                }
            }
        }
        throw runtime_error("Unexpected flow: Struct has no attribute " + bar.value);
    }
    throw runtime_error("I don't know attribute of stuff other than struct is not allowed");
}

llvm::Value* CallExpr::codegen(IRContext& ir)
{
    if (auto attr = dynamic_cast<AttribExpr*>(func)) {
        Expr* region_expr = attr->foo;
        if (region_expr->t->kind == TypeKind::REGION && attr->bar.value == "alloc") {

            assert(typeArgs.size() == 1 && "alloc<T> needs one type param");
            TypeThing* elem_type = typeArgs[0]->t;

            assert(args.size() <= 1 && "alloc<T>(n?) needs zero or one argument");
            llvm::Value* count = args.size() == 0
                ? llvm::ConstantInt::get(ir.builder.getInt32Ty(), 1)
                : args[0]->codegen(ir);

            llvm::Type* llvm_elem = elem_type->getLLVM(ir);
            uint64_t elem_size = ir.mod.getDataLayout().getTypeAllocSize(llvm_elem);

            llvm::Value* elem_size_val = llvm::ConstantInt::get(ir.builder.getInt64Ty(), elem_size);

            llvm::Value* total_size = ir.builder.CreateMul(
                elem_size_val,
                ir.builder.CreateIntCast(count, ir.builder.getInt64Ty(), false),
                "alloc_bytes");

            auto* malloc_fn = llvm::cast<llvm::Function>(
                ir.mod
                    .getOrInsertFunction(
                        "malloc",
                        llvm::FunctionType::get(llvm::PointerType::get(ir.ctx, 0),
                            { ir.builder.getInt64Ty() }, false))
                    .getCallee());

            llvm::Value* raw = ir.builder.CreateCall(malloc_fn, total_size, "malloc_call");

            llvm::Value* typed_ptr = ir.builder.CreateBitCast(
                raw, ir.builder.getPtrTy(), "typed_alloc");

            ir.allocations[std::get<RegionType>(region_expr->t->data).region]
                .push_back(typed_ptr);

            return typed_ptr;
        }
    }
    llvm::Value* l = nullptr;
    {
        IRCOptions _(ir);
        _.unpackStored();
        l = func->codegen(ir);
    }

    std::vector<llvm::Value*> llvm_args = {};
    if (ir.attrib_struct != nullptr) {
        llvm_args.reserve(args.size() + 1);
        llvm_args.push_back(ir.attrib_struct);
        ir.attrib_struct = nullptr;
    } else {
        llvm_args.reserve(args.size());
    }
    for (Expr* a : args) {
        llvm_args.push_back(a->codegen(ir));
    }

    auto* f = llvm::dyn_cast<llvm::Function>(l);
    return ir.builder.CreateCall(f, llvm_args);
}

llvm::Value* ArrayExpr::codegen(IRContext& ir)
{
    llvm::ArrayType* arrType = llvm::cast<llvm::ArrayType>(arr_type->getLLVM(ir));

    llvm::Type* i32t = ir.builder.getInt32Ty();

    auto idx = [&](unsigned i) -> llvm::Value* {
        return llvm::ConstantInt::get(i32t, i);
    };

    llvm::AllocaInst* arr_alloca = ir.builder.CreateAlloca(arrType, idx(elements.size()), "arr_init");

    for (size_t i = 0; i < elements.size(); ++i) {
        llvm::Value* ptr = ir.builder.CreateGEP(arrType, arr_alloca, { llvm::ConstantInt::get(i32t, 0), idx(i) }, "arr_init.ptr");
        ir.builder.CreateStore(elements[i]->codegen(ir), ptr);
    }
}

llvm::Value* IndexExpr::codegen(IRContext& ir)
{
    if (arr->t->kind == TypeKind::ARRAY) {
        auto* ep = ir.builder.CreateGEP(arr->t->getLLVM(ir), arr->codegen(ir), { 0, i->codegen(ir) }, "index_arr");
        if (ir.unpack_stored) {
            return ir.builder.CreateLoad(std::get<ArrType>(arr->t->data).element->getLLVM(ir), ep, "load_arr_index");
        }
        return ep;
    } else if (arr->t->kind == TypeKind::POINTER) {
        auto* ep = ir.builder.CreateGEP(std::get<PtrType>(arr->t->data).pointee->getLLVM(ir), arr->codegen(ir), i->codegen(ir), "index_ptr");
        if (ir.unpack_stored) {
            return ir.builder.CreateLoad(std::get<PtrType>(arr->t->data).pointee->getLLVM(ir), ep, "load_ptr_index");
        }
        return ep;
    }
    throw runtime_error("What this can't be!!  " + arr->t->toString());
}

llvm::Value* AssignStmt::codegen(IRContext& ir)
{
    llvm::AllocaInst* x = ir.fn_entry->CreateAlloca(type->getLLVM(ir), nullptr, name.value);
    {
        IRCOptions _(ir);
        _.unpackStored();
        ir.builder.CreateStore(value->codegen(ir), x);
    }
    decl->alloca = x;
    return nullptr;
}

llvm::Value* AssignExpr::codegen(IRContext& ir)
{
    llvm::Value* lhs = left->codegen(ir);
    assert(lhs != nullptr);
    {
        IRCOptions _(ir);
        _.unpackStored();
        ir.builder.CreateStore(right->codegen(ir), lhs);
    }
    return lhs;
}

llvm::Value* ExprStmt::codegen(IRContext& ir)
{
    return expression->codegen(ir);
}

void BlockStmt::finalize(IRContext& ir)
{
    for (auto& def : defer) {
        def(ir);
    }
    for (auto& c : cleanup) {
        c(ir);
    }
}

llvm::Value* BlockStmt::codegen(IRContext& ir)
{
    defer.clear();
    cleanup.clear();
    ir.blocks.push_back(this);
    for (auto& stmt : statements) {
        stmt->codegen(ir);
        if (ir.builder.GetInsertBlock()->getTerminator() != nullptr) {
            break;
        }
    }
    finalize(ir);
    ir.blocks.pop_back();
    return nullptr;
}

llvm::Value* FuncStmt::codegen(IRContext& ir)
{
    auto& func_decl = std::get<FuncDecl>(decl->data);
    std::vector<llvm::Type*> params;
    params.reserve(func_decl.params.size());
    for (auto* vd : func_decl.params) {
        auto& var = std::get<VarDecl>(vd->data);
        params.push_back(var.type->getLLVM(ir));
    }
    std::string fname = name.value;
    if (func_decl.parentStruct) {
        fname
            = func_decl.parentStruct->name.value + "_" + name.value;
    }
    llvm::FunctionType* fn_type = llvm::FunctionType::get(returnType->getLLVM(ir), params, false);
    llvm::Function* fn = llvm::Function::Create(
        fn_type, decl->visibility == Visibility::PUBLIC ? llvm::Function::ExternalLinkage : llvm::Function::InternalLinkage, fname, ir.mod);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(ir.ctx, "entry", fn);
    ir.current_function = fn;
    ir.fn_entry = new llvm::IRBuilder<>(entry);
    ir.builder.SetInsertPoint(entry);

    func_decl.llvm = fn;

    body.codegen(ir);

    return nullptr;
}

llvm::Value* ReturnStmt::codegen(IRContext& ir)
{
    llvm::Value* retVal = value ? value->codegen(ir) : nullptr;

    for (auto it = ir.blocks.rbegin(); it != ir.blocks.rend(); ++it) {
        (*it)->finalize(ir);
    }

    if (retVal) {
        ir.builder.CreateRet(retVal);
    } else {
        ir.builder.CreateRetVoid();
    }
    return nullptr;
}

llvm::Value* StructStmt::codegen(IRContext& ir)
{
    auto* s = llvm::StructType::create(ir.ctx, name.value);
    std::get<StructDecl>(decl->data).llvm = s;

    std::vector<llvm::Type*> lt = {};
    lt.reserve(types.size());
    for (TypeThing* t : types) {
        lt.push_back(t->getLLVM(ir));
    }
    s->setBody(
        lt,
        /*isPacked=*/false);

    for (auto* met : functions) {
        met->codegen(ir);
    }
    return nullptr;
}

llvm::Value* StructInitExpr::codegen(IRContext& ir)
{
    auto& ddata = std::get<StructDecl>(std::get<StructType>(struct_type->data).str->data);
    auto* ptr = ir.builder.CreateAlloca(ddata.llvm, nullptr, "obj");
    for (size_t i = 0; i < ddata.fieldNames.size(); ++i) {
        auto it = std::find_if(names.begin(), names.end(), [&ddata, i](Lexer::Token& t) { return t.value == ddata.fieldNames[i].value; });

        auto* field = ir.builder.CreateStructGEP(ddata.llvm, ptr, 0, "init_field_" + ddata.fieldNames[i].value);

        if (it != names.end()) {
            size_t sub_idx = static_cast<size_t>(std::distance(names.begin(), it));
            llvm::Value* raw_val = values[sub_idx]->codegen(ir);
            llvm::Value* casted_val = cast(ir, raw_val, values[sub_idx]->t, ddata.fieldDefs[i]->t);
            ir.builder.CreateStore(casted_val, field);
        } else {
            ir.builder.CreateStore(ddata.fieldDefs[i]->codegen(ir), field);
        }
    }
    return ptr;
}

llvm::Value* ElseStmt::codegen(IRContext& _) { return nullptr; }

llvm::Value* IfStmt::codegen(IRContext& ir)
{
    IfStmt* cur = this;
    llvm::BasicBlock* merge = llvm::BasicBlock::Create(ir.ctx, "merge", ir.current_function);
    while (cur->elseIf != nullptr) {
        llvm::Value* cond = cur->condition->codegen(ir);
        llvm::BasicBlock* then = llvm::BasicBlock::Create(ir.ctx, "then", ir.current_function);
        llvm::BasicBlock* else_if = llvm::BasicBlock::Create(ir.ctx, "elseif", ir.current_function);
        ir.builder.CreateCondBr(cond, then, else_if);
        ir.builder.SetInsertPoint(then);
        cur->body.codegen(ir);
        if (ir.builder.GetInsertBlock()->getTerminator() == nullptr) {
            ir.builder.CreateBr(merge);
        }
    }
    if (cur->elseStmt != nullptr) {
        llvm::BasicBlock* then = llvm::BasicBlock::Create(ir.ctx, "then", ir.current_function);
        llvm::BasicBlock* else_bb = llvm::BasicBlock::Create(ir.ctx, "else", ir.current_function);
        ir.builder.CreateCondBr(cur->condition->codegen(ir), then, else_bb);
        ir.builder.SetInsertPoint(then);
        cur->body.codegen(ir);
        if (ir.builder.GetInsertBlock()->getTerminator() == nullptr) {
            ir.builder.CreateBr(merge);
        }
        ir.builder.SetInsertPoint(else_bb);
        cur->elseStmt->body.codegen(ir);
        if (ir.builder.GetInsertBlock()->getTerminator() == nullptr) {
            ir.builder.CreateBr(merge);
        }
    }
    ir.builder.SetInsertPoint(merge);
    return nullptr;
}

llvm::Value* ForStmt::codegen(IRContext& ir)
{
    if (init != nullptr) {
        init->codegen(ir);
    }

    llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(ir.ctx, "cond", ir.current_function);
    llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(ir.ctx, "body", ir.current_function);
    llvm::BasicBlock* step_bb = llvm::BasicBlock::Create(ir.ctx, "step", ir.current_function);
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(ir.ctx, "endfor", ir.current_function);

    ir.builder.CreateBr(cond_bb);

    ir.builder.SetInsertPoint(cond_bb);
    llvm::Value* condition_value = nullptr;

    if (condition != nullptr) {
        condition_value = condition->codegen(ir);
    } else {
        condition_value = llvm::ConstantInt::getTrue(ir.ctx);
    }

    ir.builder.CreateCondBr(condition_value, body_bb, merge_bb);

    ir.builder.SetInsertPoint(body_bb);
    body.codegen(ir);
    if (ir.builder.GetInsertBlock()->getTerminator() == nullptr) {
        ir.builder.CreateBr(step_bb);
    }

    ir.builder.SetInsertPoint(step_bb);
    if (update != nullptr) {
        update->codegen(ir);
    }
    ir.builder.CreateBr(cond_bb);

    ir.builder.SetInsertPoint(merge_bb);

    return nullptr;
}

llvm::Value* RegionStmt::codegen(IRContext& ir)
{
    ir.blocks.push_back(&body);
    size_t allocations_size = 0;
    for (auto& stmt : body.statements) {
        stmt->codegen(ir);
        if (ir.allocations[decl].size() != allocations_size) {
            auto* free_fn = llvm::cast<llvm::Function>(
                ir.mod
                    .getOrInsertFunction(
                        "free", llvm::FunctionType::get(ir.builder.getVoidTy(), { llvm::PointerType::get(ir.ctx, 0) }, false))
                    .getCallee());
            size_t start = allocations_size;
            size_t end = ir.allocations[decl].size();

            body.cleanup.push_back([this, start, end, free_fn, &ir](IRContext& cir) {
                auto& allocas = ir.allocations[decl];
                size_t real_end = std::min(end, allocas.size());

                for (size_t i = start; i < real_end; ++i) {
                    cir.builder.CreateCall(free_fn, allocas[i]);
                }
            });
            allocations_size = ir.allocations[decl].size();
        }

        if (ir.builder.GetInsertBlock()->getTerminator() != nullptr) {
            break;
        }
    }
    if (ir.builder.GetInsertBlock()->getTerminator() == nullptr) {
        body.finalize(ir);
    }
    ir.blocks.pop_back();
    return nullptr;
}

llvm::Value* ImportStmt::codegen(IRContext& ir)
{
    return nullptr;
}

llvm::Value* Program::codegen(IRContext& ir)
{
    for (auto& s : statements) {
        s->codegen(ir);
    }
    return nullptr;
}