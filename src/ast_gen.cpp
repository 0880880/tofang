#include "ast.h"
#include "decl.h"
#include "irctx.h"
#include "type.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DerivedTypes.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

static llvm::Value* cast(const IRContext& ir, llvm::Value* val, TypeThing* from,
                         TypeThing* to)
{
    if (from->kind == TypeKind::NULLABLE && to->kind == TypeKind::BOOL)
    {
        llvm::Value* is_null = ir.builder.CreateExtractValue(val, {0}, "is_null");
        return is_null;
    }
    if (from->kind == TypeKind::NULLABLE && to == std::get<NullableType>(from->data).base)
    {
        llvm::Value* n_obj = ir.builder.CreateExtractValue(val, {1}, "nullable_obj");
        return n_obj;
    }
    if (from == to || from->kind == to->kind || (from->kind == TypeKind::I_NULL && to->kind == TypeKind::NULLABLE))
    {
        return val;
    }
    if (isFloat(from) && isFloat(to))
    {
        if (from->kind == TypeKind::F32)
        {
            return ir.builder.CreateFPExt(val, to->getLLVM(ir));
        }
        return ir.builder.CreateFPTrunc(val, to->getLLVM(ir));
    }
    if (isInt(from) && isFloat(to))
    {
        if (isSigned(from))
        {
            return ir.builder.CreateSIToFP(val, to->getLLVM(ir));
        }
        return ir.builder.CreateUIToFP(val, to->getLLVM(ir));
    }
    if (isInt(from) && isInt(to))
    {
        if (isSigned(from))
        {
            return ir.builder.CreateSExt(val, to->getLLVM(ir)); // FIXME idk
        }
        return ir.builder.CreateZExt(val, to->getLLVM(ir)); // FIXME idk
    }
    throw std::runtime_error("Unhandled type: " + from->toString() + " -> " + to->toString());
}

IRValue wrapNullable(const IRContext& ir, llvm::Constant* value)
{
    if (ir.infer_type != nullptr && ir.infer_type->kind == TypeKind::NULLABLE)
    {
        auto *null_struct_type = llvm::dyn_cast<llvm::StructType>(ir.infer_type->getLLVM(ir));
        assert(null_struct_type != nullptr);
        llvm::Constant* constant_struct = llvm::ConstantStruct::get(null_struct_type, {
                                                                       llvm::ConstantInt::get(
                                                                           type_bool->getLLVM(ir), 1),
                                                                       value
                                                                   });
        return constant_struct;
    }
    return value;
}

IRValue LiteralExpr::codegen(IRContext& ir)
{
    if (type == String)
    {
        return wrapNullable(ir,ir.builder.CreateGlobalString(value.value, "str_global", 0, nullptr, false));
    }
    switch (t->kind)
    {
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
        return wrapNullable(ir,llvm::ConstantInt::get(t->getLLVM(ir), static_cast<uint64_t>(std::stol(value.value)),
                                      true));
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
        return wrapNullable(ir,llvm::ConstantInt::get(t->getLLVM(ir), static_cast<uint64_t>(std::stol(value.value)),
                                      false));
    case TypeKind::F32:
    case TypeKind::F64:
        return wrapNullable(ir,llvm::ConstantFP::get(t->getLLVM(ir), std::stod(value.value)));
    case TypeKind::UNTYPED_INT:
        return wrapNullable(ir,llvm::ConstantInt::get(ir.builder.getInt64Ty(),
                                      static_cast<uint64_t>(std::stol(value.value))));
    case TypeKind::UNTYPED_FLOAT:
        return wrapNullable(ir,llvm::ConstantFP::get(ir.builder.getDoubleTy(),
                                     std::stod(value.value)));
    case TypeKind::BOOL:
        return wrapNullable(ir,value.value == "true"
                   ? llvm::ConstantInt::getTrue(ir.ctx)
                   : llvm::ConstantInt::getFalse(ir.ctx));
    case TypeKind::I_NULL:
        assert(ir.infer_type != nullptr);
        assert(ir.infer_type->kind == TypeKind::NULLABLE);
        auto *null_struct_type = llvm::dyn_cast<llvm::StructType>(ir.infer_type->getLLVM(ir));
        assert(null_struct_type != nullptr);
        llvm::Constant* constant_struct = llvm::ConstantStruct::get(null_struct_type, {
                                                                       llvm::ConstantInt::get(
                                                                           type_bool->getLLVM(ir), 0),
                                                                       llvm::UndefValue::get(
                                                                           std::get<NullableType>(ir.infer_type->data).
                                                                           base->getLLVM(ir))
                                                                   });
        return constant_struct;
    }
    return nullptr;
}

IRValue BinaryExpr::codegen(IRContext& ir)
{
    string v = op.value;
    auto lhs = cast(ir, left->codegen(ir), left->t, t);
    auto rhs = cast(ir, right->codegen(ir), right->t, t);
    if (v == "+")
    {
        if (isFloat(t))
        {
            return {ir.builder.CreateFAdd(lhs, rhs, "fp_add"), true};
        }
        else
        {
            return {ir.builder.CreateAdd(lhs, rhs, "i_add"), true};
        }
    }
    else if (v == "-")
    {
        if (isFloat(t))
        {
            return {ir.builder.CreateFSub(lhs, rhs, "fp_sub"), true};
        }
        else
        {
            return {ir.builder.CreateSub(lhs, rhs, "i_sub"), true};
        }
    }
    else if (v == "*")
    {
        if (isFloat(t))
        {
            return {ir.builder.CreateFMul(lhs, rhs, "fp_mul"), true};
        }
        else
        {
            return {ir.builder.CreateMul(lhs, rhs, "i_mul"), true};
        }
    }
    else if (v == "/")
    {
        if (isFloat(t))
        {
            return {ir.builder.CreateFDiv(lhs, rhs, "fp_div"), true};
        }
        else
        {
            return {
                isSigned(t)
                    ? ir.builder.CreateSDiv(lhs, rhs, "signed_div")
                    : ir.builder.CreateUDiv(lhs, rhs, "unsigned_div"),true};
        }
    }
    else if (v == "&" || v == "&&")
    {
        return {ir.builder.CreateAnd(lhs, rhs, "and"),true};
    }
    else if (v == "|" || v == "||")
    {
        return {ir.builder.CreateOr(lhs, rhs, "or"),true};
    }
    else if (v == "^")
    {
        return {ir.builder.CreateXor(lhs, rhs, "xor"),true};
    }
    else if (v == "==")
    {
        if (isFloat(t))
        {
            return {
                ir.builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OEQ, lhs,
                                      rhs, "fcmp_eq"),true};
        }
        else
        {
            return {
                ir.builder.CreateICmp(llvm::CmpInst::Predicate::ICMP_EQ, lhs,
                                      rhs, "icmp_eq"),true};
        }
    }
    else if (v == "!=")
    {
        if (isFloat(t))
        {
            return {
                ir.builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_ONE, lhs,
                                      rhs, "fcmp_neq"),
                true
            };
        }
        else
        {
            return {
                ir.builder.CreateICmp(llvm::CmpInst::Predicate::ICMP_NE, lhs,
                                      rhs, "icmp_neq"),
                true
            };
        }
    }
    else if (v == ">")
    {
        if (isFloat(t))
        {
            return {
                ir.builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OGT, lhs,
                                      rhs, "fcmp_gt"),true};
        }
        else
        {
            return {ir.builder.CreateICmp(isSigned(t)
                                              ? llvm::CmpInst::Predicate::ICMP_SGT
                                              : llvm::CmpInst::Predicate::ICMP_UGT,
                                          lhs, rhs, "icmp_gt"),
                true
            };
        }
    }
    else if (v == "<")
    {
        if (isFloat(t))
        {
            return {
                ir.builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OLT, lhs,
                                      rhs, "fcmp_lt"),true};
        }
        else
        {
            return {ir.builder.CreateICmp(isSigned(t)
                                              ? llvm::CmpInst::Predicate::ICMP_SLT
                                              : llvm::CmpInst::Predicate::ICMP_ULT,
                                          lhs, rhs, "icmp_lt"),
                true
            };
        }
    }
    else if (v == ">=")
    {
        if (isFloat(t))
        {
            return {
                ir.builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OGE, lhs,
                                      rhs, "fcmp_ge"),true};
        }
        else
        {
            return {
                ir.builder.CreateICmp(isSigned(t)
                                          ? llvm::CmpInst::Predicate::ICMP_SGE
                                          : llvm::CmpInst::Predicate::ICMP_UGE,
                                      lhs, rhs, "icmp_ge"),true};
        }
    }
    else if (v == "<=")
    {
        if (isFloat(t))
        {
            return {
                ir.builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OLE, lhs,
                                      rhs, "fcmp_le"),
                true
            };
        }
        else
        {
            return {
                ir.builder.CreateICmp(isSigned(t)
                                          ? llvm::CmpInst::Predicate::ICMP_SLE
                                          : llvm::CmpInst::Predicate::ICMP_ULE,
                                      lhs, rhs, "icmp_le"),
                true
            };
        }
    }
    // TODO handle comparisons
    // TODO pointers
    throw runtime_error("Unhandled operation " + v);
}

IRValue UnaryExpr::codegen(IRContext& ir)
{
    const string v = op.value;
    if (v == "+")
    {
        const auto rhs = right->codegen(ir);
        return {rhs, true};
    }
    if (v == "-")
    {
        const auto rhs = right->codegen(ir);
        if (isInt(right->t))
        {
            return {
                ir.builder.CreateSub(llvm::ConstantInt::get(rhs.value->getType(), 0),
                                     rhs.value),
                true
            };
        }
        if (isFloat(right->t))
        {
            return {
                ir.builder.CreateFSub(llvm::ConstantFP::get(rhs.value->getType(), 0),
                                      rhs.value),true
            };
        }
    }
    else if (v == "*")
    {
        if (ir.unpack_stored)
        {
            const auto rhs = right->codegen(ir);
            return ir.builder.CreateLoad(std::get<PtrType>(right->t->data).pointee->getLLVM(ir), rhs.value, "deref");
        }
        {
            IRCOptions _(ir);
            _.unpackStored();
            return right->codegen(ir);
        }
    }
    else if (v == "&")
    {
        IRCOptions _(ir);
        _.packStored();
        return {right->codegen(ir), true};
    }
    throw runtime_error("Unexpected error UnaryExpr");
}

IRValue GroupingExpr::codegen(IRContext& ir)
{
    return expression->codegen(ir);
}

IRValue VariableExpr::codegen(IRContext& ir)
{
    if (decl->kind == DeclKind::FUNC)
    {
        if (ir.unpack_stored)
        {
            return std::get<FuncDecl>(decl->data).llvm;
        }
        else
        {
            throw std::runtime_error("function has no address as variable lvalue");
        }
    }

    if (ir.unpack_stored)
    {
        return ir.builder.CreateLoad(decl->toType()->getLLVM(ir), decl->alloca);
    }
    return decl->alloca;
}

IRValue AttribExpr::codegen(IRContext& ir)
{
    llvm::Value* lhs = nullptr;
    {
        IRCOptions _(ir);
        _.packStored();
        lhs = foo->codegen(ir);
    }
    auto *base_ty = foo->t;
    if (base_ty->kind == TypeKind::NULLABLE)
    {
        lhs = ir.builder.CreateExtractValue(lhs, {1}, "nullable_obj");
        base_ty = std::get<NullableType>(base_ty->data).base;
    }
    if (base_ty->kind == TypeKind::SLICE)
    {
        if (bar.value == "ptr")
        {
            return {ir.builder.CreateExtractValue(lhs, {0}, "slice.ptr"), true};
        }
        if (bar.value == "len")
        {
            return {ir.builder.CreateExtractValue(lhs, {1}, "slice.len"), true};
        }
        throw std::runtime_error("Unreachable");
    }
    if (base_ty->kind == TypeKind::STRUCT)
    {
        const auto* decl = std::get<StructType>(base_ty->data).decl;
        const auto& ddata = std::get<StructDecl>(decl->data);
        for (const auto & method : ddata.methods)
        {
            if (method->name.value == bar.value)
            {
                if (!ir.unpack_stored)
                {
                    throw runtime_error("Invalid");
                }
                else
                {
                    ir.attrib_struct = lhs;
                    return std::get<FuncDecl>(method->data).llvm;
                }
            }
        }
        for (unsigned int i = 0; i < ddata.fieldNames.size(); i++)
        {
            if (ddata.fieldNames[i].value == bar.value)
            {
                llvm::Value* ptr = ir.builder.CreateStructGEP(ddata.llvm, lhs, i,
                                                              "getl_" + std::to_string(i) + "_" + decl->name.value);

                if (ir.unpack_stored)
                {
                    llvm::Type* field_type = ddata.fieldTypes[i]->getLLVM(ir);
                    return ir.builder.CreateLoad(field_type, ptr, "getr_" + std::to_string(i) + "_" + decl->name.value);
                }
                else
                {
                    return ptr;
                }
            }
        }
        throw runtime_error("Unexpected flow: Struct has no attribute " + bar.value);
    }
    throw runtime_error("I don't know attribute of stuff other than struct is not allowed");
}

IRValue CallExpr::codegen(IRContext& ir)
{
    if (const auto attr = dynamic_cast<AttribExpr*>(func))
    {
        if (const Expr* region_expr = attr->foo; region_expr->t->kind == TypeKind::REGION && attr->bar.value == "alloc")
        {
            assert(typeArgs.size() == 1 && "alloc<T> needs one type param");
            TypeThing* elem_type = typeArgs[0]->t;

            assert(args.size() <= 1 && "alloc<T>(n?) needs zero or one argument");
            llvm::Value* count = args.empty()
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
                                              {ir.builder.getInt64Ty()}, false))
                  .getCallee());

            llvm::Value* raw = ir.builder.CreateCall(malloc_fn, total_size, "malloc_call");

            llvm::Value* typed_ptr = ir.builder.CreateBitCast(
                raw, ir.builder.getPtrTy(), "typed_alloc");

            ir.allocations[std::get<RegionType>(region_expr->t->data).region]
                .push_back(typed_ptr);

            return {typed_ptr, true};
        }
    }
    llvm::Value* l = nullptr;
    std::vector<llvm::Value*> llvm_args = {};
    {
        IRCOptions _(ir);
        _.unpackStored();
        l = func->codegen(ir);
        if (ir.attrib_struct != nullptr)
        {
            llvm_args.reserve(args.size() + 1);
            llvm_args.push_back(ir.attrib_struct);
            ir.attrib_struct = nullptr;
        }
        else
        {
            llvm_args.reserve(args.size());
        }
        const auto [params, return_type] = std::get<FuncType>(func->t->data);
        for (size_t i = 0; i < args.size(); ++i)
        {
            Expr *a = args[i];
            llvm_args.push_back(cast(ir, a->codegen(ir), a->t, params[i]));
        }
    }

    auto* f = llvm::dyn_cast<llvm::Function>(l);
    return {ir.builder.CreateCall(f, llvm_args), true};
}

IRValue ArrayExpr::codegen(IRContext& ir)
{
    auto* llvm_arr_type = llvm::cast<llvm::ArrayType>(arr_type->getLLVM(ir));

    llvm::Type* i32t = ir.builder.getInt32Ty();

    auto idx = [&](unsigned i) -> llvm::Value*
    {
        return llvm::ConstantInt::get(i32t, i);
    };

    llvm::AllocaInst* arr_alloca = ir.builder.CreateAlloca(llvm_arr_type, idx(elements.size()), "arr_init");

    for (size_t i = 0; i < elements.size(); ++i)
    {
        llvm::Value* ptr = ir.builder.CreateGEP(llvm_arr_type, arr_alloca, {llvm::ConstantInt::get(i32t, 0), idx(i)},
                                                "arr_init.ptr");
        ir.builder.CreateStore(elements[i]->codegen(ir), ptr);
    }

    return arr_alloca;
}

IRValue IndexExpr::codegen(IRContext& ir)
{
    auto *arr_ty = arr->t;
    llvm::Value *ptr = nullptr;
    {
        IRCOptions _(ir);
        _.unpackStored();
        ptr = arr->codegen(ir);
    }
    if (arr_ty->kind == TypeKind::NULLABLE)
    {
        ptr = ir.builder.CreateExtractValue(ptr, {1}, "nullable_obj");
        arr_ty = std::get<NullableType>(arr_ty->data).base;
    }
    if (arr_ty->kind == TypeKind::ARRAY)
    {
        auto* ep = ir.builder.CreateGEP(arr_ty->getLLVM(ir), ptr, {0, i->codegen(ir)}, "index_arr");
        if (ir.unpack_stored)
        {
            return ir.builder.CreateLoad(std::get<ArrType>(arr_ty->data).element->getLLVM(ir), ep, "load_arr_index");
        }
        return ep;
    }
    else if (arr_ty->kind == TypeKind::SLICE)
    {
        auto* slice_ptr = ir.builder.CreateExtractValue(ptr, {0}, "slice_ptr");
        auto* ep = ir.builder.CreateGEP(std::get<SliceType>(arr_ty->data).element->getLLVM(ir), slice_ptr,
                                        {i->codegen(ir)}, "index_ptr");
        if (ir.unpack_stored)
        {
            return ir.builder.CreateLoad(std::get<ArrType>(arr_ty->data).element->getLLVM(ir), ep, "load_arr_index");
        }
        return ep;
    }
    else if (arr_ty->kind == TypeKind::POINTER)
    {
        auto* ep = ir.builder.CreateGEP(std::get<PtrType>(arr_ty->data).pointee->getLLVM(ir), ptr,
                                        {i->codegen(ir)}, "index_ptr");
        if (ir.unpack_stored)
        {
            return ir.builder.CreateLoad(std::get<PtrType>(arr_ty->data).pointee->getLLVM(ir), ep, "load_ptr_index");
        }
        return ep;
    }
    throw runtime_error("What this can't be!!  " + arr_ty->toString());
}

IRValue SliceExpr::codegen(IRContext& ir)
{
    auto* arr_ty = arr->t;
    llvm::Value* ptr = nullptr;
    llvm::Value* from_code = nullptr;
    llvm::Value* to_code = nullptr;
    {
        IRCOptions _(ir);
        _.unpackStored();
        ptr = arr->codegen(ir);
    }
    if (arr_ty->kind == TypeKind::NULLABLE)
    {
        ptr = ir.builder.CreateExtractValue(ptr, {1}, "nullable_obj");
        arr_ty = std::get<NullableType>(arr_ty->data).base;
    }

    {
        IRCOptions _(ir);
        _.unpackStored();
        if (from)
        {
            from_code = from.value()->codegen(ir);
        }
        else
        {
            from_code = ir.builder.getInt64(0);
        }
        if (to)
        {
            to_code = to.value()->codegen(ir);
        }
        else
        {
            if (arr_ty->kind == TypeKind::ARRAY)
            {
                if (auto* static_arr = llvm::dyn_cast<llvm::ArrayType>(arr_ty->getLLVM(ir)))
                {
                    to_code = ir.builder.getInt64(static_arr->getNumElements());
                }
                else
                {
                    throw std::runtime_error("Unreachable");
                }
            }
            else if (arr_ty->kind == TypeKind::SLICE)
            {
                to_code = ir.builder.CreateExtractValue(ptr, {1}, "slice_len");
            }
            else if (arr_ty->kind == TypeKind::POINTER)
            {
                throw std::runtime_error("Unreachable");
            }
        }
    }

    llvm::Value* ep;
    if (arr_ty->kind == TypeKind::ARRAY)
    {
        ep = ir.builder.CreateGEP(arr_ty->getLLVM(ir), ptr, {from_code}, "index_arr");
    }
    else if (arr_ty->kind == TypeKind::SLICE)
    {
        auto* slice_ptr = ir.builder.CreateExtractValue(ptr, {0}, "slice_ptr");
        ep = ir.builder.CreateGEP(std::get<SliceType>(arr_ty->data).element->getLLVM(ir), slice_ptr,
                                  {from_code}, "index_ptr");
    }
    else if (arr_ty->kind == TypeKind::POINTER)
    {
        ep = ir.builder.CreateGEP(std::get<PtrType>(arr_ty->data).pointee->getLLVM(ir), ptr,
                                  {from_code}, "index_ptr");
    }
    else
    {
        throw runtime_error("What this can't be!!  " + arr_ty->toString());
    }
    auto* slice_type = llvm::dyn_cast<llvm::StructType>(t->getLLVM(ir));
    assert(slice_type != nullptr);
    llvm::Value* slice_ptr = ir.builder.CreateAlloca(slice_type, nullptr, "slice");

    llvm::Value* ptr_addr = ir.builder.CreateStructGEP(slice_type, slice_ptr, 0, "slice.ptr");
    ir.builder.CreateStore(ep, ptr_addr);

    llvm::Value* len_addr = ir.builder.CreateStructGEP(slice_type, slice_ptr, 1, "slice.len");
    ir.builder.CreateStore(ir.builder.CreateSub(to_code, from_code, "slice_len"), len_addr);

    llvm::Value* ret = slice_ptr;
    if (ir.unpack_stored)
    {
        ret = ir.builder.CreateLoad(slice_type, slice_ptr, "loaded_obj");
    }
    if (ir.infer_type->kind == TypeKind::NULLABLE)
    {
        auto* ty = ir.infer_type->getLLVM(ir);
        llvm::AllocaInst* struct_alloca = ir.builder.CreateAlloca(ty, nullptr, "nullable_wrapper");

        llvm::Value* not_null_ptr = ir.builder.CreateStructGEP(ty, struct_alloca, 0, "not_null.ptr");

        llvm::Value* obj_ptr = ir.builder.CreateStructGEP(ty, struct_alloca, 1, "obj.ptr");

        ir.builder.CreateStore(
            llvm::ConstantInt::getBool(ir.ctx, true),
            not_null_ptr
        );

        ir.builder.CreateStore(
            ret,
            obj_ptr
        );

        return struct_alloca;
    }
    return ret;
}

IRValue AssignStmt::codegen(IRContext& ir)
{
    llvm::AllocaInst* x = ir.fn_entry->CreateAlloca(type->getLLVM(ir), nullptr, name.value);
    {
        IRCOptions _(ir);
        _.unpackStored();
        _.withType(type);
        llvm::Value* val = value->codegen(ir);
        ir.builder.CreateStore(val, x);
    }
    decl->alloca = x;
    return nullptr;
}

IRValue AssignExpr::codegen(IRContext& ir)
{
    const IRValue lhs = left->codegen(ir);
    if (lhs.is_rv)
    {
        throw std::runtime_error("LHS of assignment cannot be an rvalue.");
    }
    assert(lhs.value != nullptr);
    {
        IRCOptions _(ir);
        _.unpackStored();
        _.withType(left->t);
        ir.builder.CreateStore(right->codegen(ir), lhs.value);
    }
    return lhs;
}

IRValue ExprStmt::codegen(IRContext& ir)
{
    return expression->codegen(ir);
}

void BlockStmt::finalize(IRContext& ir)
{
    for (auto& def : defer)
    {
        def(ir);
    }
    for (auto& c : cleanup)
    {
        c(ir);
    }
}

IRValue BlockStmt::codegen(IRContext& ir)
{
    defer.clear();
    cleanup.clear();
    ir.blocks.push_back(this);
    for (auto& stmt : statements)
    {
        stmt->codegen(ir);
        if (ir.builder.GetInsertBlock()->getTerminator() != nullptr)
        {
            break;
        }
    }
    finalize(ir);
    ir.blocks.pop_back();
    return nullptr;
}

IRValue FuncStmt::codegen(IRContext& ir)
{
    auto& func_decl = std::get<FuncDecl>(decl->data);
    std::vector<llvm::Type*> params;
    params.reserve(func_decl.params.size());
    for (auto* vd : func_decl.params)
    {
        auto& var = std::get<VarDecl>(vd->data);
        if (is_extern && (var.type->kind == TypeKind::META || var.type->kind == TypeKind::USER_TYPE || var.type->kind ==
            TypeKind::STRUCT))
        {
            throw std::runtime_error("Invalid argument " + vd->name.value + " in extern function.");
        }
        params.push_back(var.type->getLLVM(ir));
    }
    std::string fname = name.value;
    if (func_decl.parentStruct)
    {
        fname
            = func_decl.parentStruct->name.value + "_" + name.value;
    }
    llvm::FunctionType* fn_type = llvm::FunctionType::get(returnType->getLLVM(ir), params, false);

    auto linkage = decl->visibility == Visibility::PUBLIC
                       ? llvm::Function::ExternalLinkage
                       : llvm::Function::InternalLinkage;
    if (is_extern)
    {
        linkage = llvm::Function::ExternalLinkage;
    }
    llvm::Function* fn = llvm::Function::Create(
        fn_type, linkage, fname, ir.mod);
    func_decl.llvm = fn;
    if (!is_extern)
    {
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(ir.ctx, "entry", fn);
        ir.current_function = fn;
        ir.fn_entry = new llvm::IRBuilder<>(entry);
        ir.builder.SetInsertPoint(entry);

        unsigned idx = 0;
        for (auto& arg : fn->args())
        {
            auto* param_decl = func_decl.params[idx];
            // auto& var_data = std::get<VarDecl>(param_decl->data);

            llvm::AllocaInst* alloca = ir.fn_entry->CreateAlloca(
                arg.getType(), nullptr, param_decl->name.value + ".addr");

            ir.builder.CreateStore(&arg, alloca);

            param_decl->alloca = alloca;

            idx++;
        }

        body.codegen(ir);
    }
    else
    {
        fn->setCallingConv(llvm::CallingConv::C);
    }

    return nullptr;
}

IRValue ReturnStmt::codegen(IRContext& ir)
{
    llvm::Value* ret_val = nullptr;
    {
        IRCOptions _(ir);
        _.unpackStored();
        ret_val = value ? value->codegen(ir) : nullptr;
    }
    for (auto it = ir.blocks.rbegin(); it != ir.blocks.rend(); ++it)
    {
        (*it)->finalize(ir);
    }

    if (ret_val)
    {
        ir.builder.CreateRet(ret_val);
    }
    else
    {
        ir.builder.CreateRetVoid();
    }
    return nullptr;
}

IRValue StructStmt::codegen(IRContext& ir)
{
    auto* s = llvm::StructType::create(ir.ctx, name.value);
    std::get<StructDecl>(decl->data).llvm = s;

    std::vector<llvm::Type*> lt = {};
    lt.reserve(types.size());
    for (TypeThing* t : types)
    {
        lt.push_back(t->getLLVM(ir));
    }
    s->setBody(
        lt,
        /*isPacked=*/false);

    for (auto* met : functions)
    {
        met->codegen(ir);
    }
    return nullptr;
}

IRValue StructInitExpr::codegen(IRContext& ir)
{
    auto& ddata = std::get<StructDecl>(std::get<StructType>(struct_type->data).decl->data);
    auto* ptr = ir.builder.CreateAlloca(ddata.llvm, nullptr, "obj");
    for (size_t i = 0; i < ddata.fieldNames.size(); ++i)
    {
        {
            auto it = std::ranges::find_if(names, [&ddata, i](Lexer::Token& t)
            {
                return t.value == ddata.fieldNames[i].value;
            });
            auto* field = ir.builder.CreateStructGEP(ddata.llvm, ptr, i, "init_field_" + ddata.fieldNames[i].value);
            IRCOptions _(ir);
            _.withType(ddata.fieldTypes[i]);
            if (it != names.end())
            {
                const size_t sub_idx = static_cast<size_t>(std::distance(names.begin(), it));
                llvm::Value* raw_val = values[sub_idx]->codegen(ir);
                llvm::Value* casted_val = cast(ir, raw_val, values[sub_idx]->t, ddata.fieldTypes[i]);
                ir.builder.CreateStore(casted_val, field);
            }
            else
            {
                ir.builder.CreateStore(ddata.fieldDefs[i]->codegen(ir), field);
            }
        }
    }
    llvm::Value* ret = ptr; // FIXME this case should only be possible with &Struct{...}
    if (ir.unpack_stored)
    {
        ret = ir.builder.CreateLoad(ddata.llvm, ptr, "loaded_obj");
    }
    if (ir.infer_type->kind == TypeKind::NULLABLE)
    {
        auto *ty = ir.infer_type->getLLVM(ir);
        llvm::AllocaInst *struct_alloca =ir.builder.CreateAlloca(ty, nullptr, "nullable_wrapper");

        llvm::Value *not_null_ptr = ir.builder.CreateStructGEP(ty, struct_alloca, 0, "not_null.ptr");

        llvm::Value *obj_ptr = ir.builder.CreateStructGEP(ty, struct_alloca, 1, "obj.ptr");

        ir.builder.CreateStore(
            llvm::ConstantInt::getBool(ir.ctx, true),
            not_null_ptr
        );

        ir.builder.CreateStore(
            ret,
            obj_ptr
        );

        return {struct_alloca, true};
    }
    return {ret, true};
}

IRValue ElseStmt::codegen(IRContext& /*ir*/) { return nullptr; }

llvm::Value* codegenCondition(IRContext& ir, Expr* condition)
{
    {
        IRCOptions _(ir);
        _.unpackStored();
        llvm::Value* cond = condition->codegen(ir);

        if (condition->t->kind == TypeKind::NULLABLE)
        {
            llvm::Value* is_null = ir.builder.CreateExtractValue(cond, {0}, "is_null");
            return is_null;
        }
        return cond;
    }
}

IRValue IfStmt::codegen(IRContext& ir)
{
    IfStmt* cur = this;
    llvm::BasicBlock* merge = llvm::BasicBlock::Create(ir.ctx, "merge", ir.current_function);
    while (cur == this || (cur != nullptr && cur->elseIf != nullptr))
    {
        llvm::Value* cond = codegenCondition(ir, cur->condition);
        llvm::BasicBlock* then = llvm::BasicBlock::Create(ir.ctx, "then", ir.current_function);
        if (cur == this)
        {
            ir.builder.CreateCondBr(cond, then, merge);
        }
        else
        {
            llvm::BasicBlock* else_if = llvm::BasicBlock::Create(ir.ctx, "elseif", ir.current_function);
            ir.builder.CreateCondBr(cond, then, else_if);
        }
        ir.builder.SetInsertPoint(then);
        cur->body.codegen(ir);
        if (ir.builder.GetInsertBlock()->getTerminator() == nullptr)
        {
            ir.builder.CreateBr(merge);
        }
        cur = cur->elseIf;
    }
    if (cur != nullptr && cur->elseStmt != nullptr)
    {
        llvm::BasicBlock* then = llvm::BasicBlock::Create(ir.ctx, "then", ir.current_function);
        llvm::BasicBlock* else_bb = llvm::BasicBlock::Create(ir.ctx, "else", ir.current_function);
        ir.builder.CreateCondBr(codegenCondition(ir, cur->condition), then, else_bb);
        ir.builder.SetInsertPoint(then);
        cur->body.codegen(ir);
        if (ir.builder.GetInsertBlock()->getTerminator() == nullptr)
        {
            ir.builder.CreateBr(merge);
        }
        ir.builder.SetInsertPoint(else_bb);
        cur->elseStmt->body.codegen(ir);
        if (ir.builder.GetInsertBlock()->getTerminator() == nullptr)
        {
            ir.builder.CreateBr(merge);
        }
    }
    ir.builder.SetInsertPoint(merge);
    return nullptr;
}

IRValue ForStmt::codegen(IRContext& ir)
{
    if (init != nullptr)
    {
        init->codegen(ir);
    }

    llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(ir.ctx, "cond", ir.current_function);
    llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(ir.ctx, "body", ir.current_function);
    llvm::BasicBlock* step_bb = llvm::BasicBlock::Create(ir.ctx, "step", ir.current_function);
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(ir.ctx, "endfor", ir.current_function);

    ir.builder.CreateBr(cond_bb);

    ir.builder.SetInsertPoint(cond_bb);
    llvm::Value* condition_value = nullptr;

    if (condition != nullptr)
    {
        condition_value = condition->codegen(ir);
    }
    else
    {
        condition_value = llvm::ConstantInt::getTrue(ir.ctx);
    }

    ir.builder.CreateCondBr(condition_value, body_bb, merge_bb);

    ir.builder.SetInsertPoint(body_bb);
    body.codegen(ir);
    if (ir.builder.GetInsertBlock()->getTerminator() == nullptr)
    {
        ir.builder.CreateBr(step_bb);
    }

    ir.builder.SetInsertPoint(step_bb);
    if (update != nullptr)
    {
        update->codegen(ir);
    }
    ir.builder.CreateBr(cond_bb);

    ir.builder.SetInsertPoint(merge_bb);

    return nullptr;
}

IRValue RegionStmt::codegen(IRContext& ir)
{
    ir.blocks.push_back(&body);
    size_t allocations_size = 0;
    for (auto& stmt : body.statements)
    {
        stmt->codegen(ir);
        if (ir.allocations[decl].size() != allocations_size)
        {
            auto* free_fn = llvm::cast<llvm::Function>(
                ir.mod
                  .getOrInsertFunction(
                      "free", llvm::FunctionType::get(ir.builder.getVoidTy(), {llvm::PointerType::get(ir.ctx, 0)},
                                                      false))
                  .getCallee());
            size_t start = allocations_size;
            size_t end = ir.allocations[decl].size();

            body.cleanup.emplace_back([this, start, end, free_fn, &ir](const IRContext& cir)
            {
                const auto& allocas = ir.allocations[decl];
                const size_t real_end = std::min(end, allocas.size());

                for (size_t i = start; i < real_end; ++i)
                {
                    cir.builder.CreateCall(free_fn, allocas[i]);
                }
            });
            allocations_size = ir.allocations[decl].size();
        }

        if (ir.builder.GetInsertBlock()->getTerminator() != nullptr)
        {
            break;
        }
    }
    if (ir.builder.GetInsertBlock()->getTerminator() == nullptr)
    {
        body.finalize(ir);
    }
    ir.blocks.pop_back();
    return nullptr;
}

IRValue ImportStmt::codegen(IRContext&  /*ir*/)
{
    return nullptr;
}

IRValue Program::codegen(IRContext& ir)
{
    for (auto& s : statements)
    {
        s->codegen(ir);
    }
    return nullptr;
}
