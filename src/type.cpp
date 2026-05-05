#include "type.h"
#include "decl.h"
#include "irctx.h"
#include <llvm/IR/LLVMContext.h>
#include <string>

using namespace std;

string TypeThing::toString()
{
    switch (kind) {
    case TypeKind::VOID:
        return "void";
    case TypeKind::BOOL:
        return "bool";
    case TypeKind::UNTYPED_INT:
        return "unint";
    case TypeKind::UNTYPED_FLOAT:
        return "unfloat";
    case TypeKind::U8:
        return "u8";
    case TypeKind::U16:
        return "u16";
    case TypeKind::U32:
        return "u32";
    case TypeKind::U64:
        return "u64";
    case TypeKind::I8:
        return "i8";
    case TypeKind::I16:
        return "i16";
    case TypeKind::I32:
        return "i32";
    case TypeKind::I64:
        return "i64";
    case TypeKind::F32:
        return "f32";
    case TypeKind::F64:
        return "f64";
    case TypeKind::STRUCT:
        return "struct(" + std::get<StructType>(data).str->name.value + ")";
    case TypeKind::NULLABLE:
        return std::get<NullableType>(data).base->toString() + "?";
    case TypeKind::TYPE_VAR:
        return std::get<VarType>(data).name;
    case TypeKind::FUNCTION: {
        FuncType f = std::get<FuncType>(data);
        return f.return_type->toString() + " func()";
    }
    case TypeKind::GENERIC_FUNC: {
        FuncType g = std::get<FuncType>(data);
        return g.return_type->toString() + " func<>()";
    }
    case TypeKind::USER_TYPE: {
        UserType u = std::get<UserType>(data);
        return "u:" + u.iden;
    }
    case TypeKind::REGION:
        return "region";
    case TypeKind::META:
        return "type";
    case TypeKind::POINTER:
        return std::get<PtrType>(data).pointee->toString() + "*";
    case TypeKind::ARRAY: {
        ArrType arr = std::get<ArrType>(data);
        return arr.element->toString() + "[" + std::to_string(arr.length) + "]";
    }
    case TypeKind::I_NULL:
        return "inull";
    }
}
using Type = llvm::Type;
using PointerType = llvm::PointerType;

Type* TypeThing::getLLVM(const IRContext& ir)
{
    switch (kind) {
    case TypeKind::VOID:
        return Type::getVoidTy(ir.ctx);
    case TypeKind::BOOL:
        return Type::getInt1Ty(ir.ctx);
    case TypeKind::U8:
    case TypeKind::I8:
        return Type::getInt8Ty(ir.ctx);
    case TypeKind::U16:
    case TypeKind::I16:
        return Type::getInt16Ty(ir.ctx);
    case TypeKind::U32:
    case TypeKind::I32:
        return Type::getInt32Ty(ir.ctx);
    case TypeKind::U64:
    case TypeKind::I64:
        return Type::getInt64Ty(ir.ctx);
    case TypeKind::F32:
        return Type::getFloatTy(ir.ctx);
    case TypeKind::F64:
        return Type::getDoubleTy(ir.ctx);
    case TypeKind::POINTER:
        return PointerType::getUnqual(ir.ctx);
    case TypeKind::STRUCT:
        return std::get<StructDecl>(std::get<StructType>(data).str->data).llvm;
    case TypeKind::META:
        return std::get<MetaType>(data).type->getLLVM(ir);
    }
    throw runtime_error("Unhandled type: " + toString());
}

TypeThing* TypeInterner::getRegion(Decl* region)
{
    TypeKey key {};
    key.kind = TypeKind::REGION;
    key.region = region;

    auto it = table.find(key);
    if (it != table.end()) {
        return it->second;
    }

    auto* t = new TypeThing { .kind = TypeKind::REGION,
        .data = RegionType { .region = region } };

    table[key] = t;
    return t;
}

TypeThing* TypeInterner::getNullable(TypeThing* base)
{
    TypeKey key {};
    key.kind = TypeKind::NULLABLE;
    key.a = base;

    auto it = table.find(key);
    if (it != table.end()) {
        return it->second;
    }

    auto* t = new TypeThing { .kind = TypeKind::NULLABLE,
        .data = NullableType { .base = base } };

    table[key] = t;
    return t;
}

TypeThing* TypeInterner::getPointer(TypeThing* pointee)
{
    TypeKey key {};
    key.kind = TypeKind::POINTER;
    key.a = pointee;

    auto it = table.find(key);
    if (it != table.end()) {
        return it->second;
    }

    auto* t = new TypeThing { .kind = TypeKind::POINTER,
        .data = PtrType { .pointee = pointee } };

    table[key] = t;
    return t;
}

TypeThing* TypeInterner::getArray(TypeThing* elem, size_t len)
{
    TypeKey key {};
    key.kind = TypeKind::ARRAY;
    key.a = elem;
    key.length = len;

    auto it = table.find(key);
    if (it != table.end()) {
        return it->second;
    }

    auto* t = new TypeThing { .kind = TypeKind::ARRAY,
        .data = ArrType { .element = elem, .length = len } };

    table[key] = t;
    return t;
}

TypeThing* TypeInterner::getFunction(const std::vector<TypeThing*>& params,
    TypeThing* returnType)
{
    TypeKey key {};
    key.kind = TypeKind::FUNCTION;
    key.params = params;
    key.a = returnType;

    auto it = table.find(key);
    if (it != table.end()) {
        return it->second;
    }

    auto* t = new TypeThing {
        .kind = TypeKind::FUNCTION,
        .data = FuncType { .params = params, .return_type = returnType }
    };

    table[key] = t;
    return t;
}

TypeThing*
TypeInterner::getGenericFunction(const std::vector<TypeThing*>& type_params,
    const std::vector<TypeThing*>& params,
    TypeThing* returnType)
{
    TypeKey key {};
    key.kind = TypeKind::FUNCTION;
    key.params = params;
    key.params_b = type_params;
    key.a = returnType;

    auto it = table.find(key);
    if (it != table.end()) {
        return it->second;
    }

    auto* t = new TypeThing { .kind = TypeKind::GENERIC_FUNC,
        .data = GenericFuncType { .type_params = type_params,
            .params = params,
            .return_type = returnType } };

    table[key] = t;
    return t;
}

TypeThing* TypeInterner::getMeta(TypeThing* inside)
{
    TypeKey key {};
    key.kind = TypeKind::META;
    key.a = inside;

    auto it = table.find(key);
    if (it != table.end()) {
        return it->second;
    }

    auto* t = new TypeThing { .kind = TypeKind::META, .data = MetaType { inside } };

    table[key] = t;
    return t;
}

TypeThing* TypeInterner::getTypeVar(const string& name)
{
    TypeKey key {};
    key.kind = TypeKind::TYPE_VAR;
    key.name = name;

    auto it = table.find(key);
    if (it != table.end()) {
        return it->second;
    }

    auto* t = new TypeThing { .kind = TypeKind::TYPE_VAR, .data = VarType { name } };

    table[key] = t;
    return t;
}

TypeThing* TypeInterner::getStruct(Decl* s)
{
    TypeKey key {};
    key.kind = TypeKind::STRUCT;
    key.name = s->name.value; // I don't know, but name should be unique so
                              // this can't be a problem
    StructDecl sd = std::get<StructDecl>(s->data);
    key.params.insert(key.params.begin(), sd.fieldTypes.begin(),
        sd.fieldTypes.end());

    auto it = table.find(key);
    if (it != table.end()) {
        return it->second;
    }

    auto* t = new TypeThing { .kind = TypeKind::STRUCT, .data = StructType { s } };

    table[key] = t;
    return t;
}

TypeThing* TypeInterner::getUserType(const std::string& name)
{
    TypeKey key {};
    key.kind = TypeKind::USER_TYPE;
    key.name = name;

    auto it = table.find(key);
    if (it != table.end()) {
        return it->second;
    }

    auto* t = new TypeThing { .kind = TypeKind::USER_TYPE, .data = UserType { name } };

    table[key] = t;
    return t;
}

TypeThing* TypeInterner::substitute(
    TypeThing* t,
    std::unordered_map<TypeKey, TypeThing*, TypeKeyHash, TypeKeyEq>& subst)
{
    switch (t->kind) {
    case TypeKind::TYPE_VAR: {
        TypeKey k = { .kind = TypeKind::TYPE_VAR,
            .a = nullptr,
            .b = nullptr,
            .length = 0,
            .region = 0,
            .name = std::get<VarType>(t->data).name,
            .params = {},
            .params_b = {} };
        return subst[k];
    }
    case TypeKind::POINTER:
        return getPointer(substitute(std::get<PtrType>(t->data).pointee, subst));
    case TypeKind::ARRAY: {
        ArrType arr = std::get<ArrType>(t->data);
        return getArray(substitute(arr.element, subst), arr.length);
    }
    case TypeKind::FUNCTION: {
        FuncType f = std::get<FuncType>(t->data);
        std::vector<TypeThing*> params = {};
        params.reserve(f.params.size());
        for (TypeThing* p : f.params) {
            params.push_back(substitute(p, subst));
        }
        return getFunction(params, substitute(f.return_type, subst));
    }
    default:
        return t;
    }
}

static TypeInterner interner_obj;
TypeInterner* interner = &interner_obj;

static TypeThing type_void_obj { .kind = TypeKind::VOID, .data = {} };
static TypeThing type_bool_obj { .kind = TypeKind::BOOL, .data = {} };

static TypeThing type_unint_obj { .kind = TypeKind::UNTYPED_INT, .data = {} };
static TypeThing type_unfloat_obj { .kind = TypeKind::UNTYPED_FLOAT, .data = {} };

static TypeThing type_u8_obj { .kind = TypeKind::U8, .data = {} };
static TypeThing type_u16_obj { .kind = TypeKind::U16, .data = {} };
static TypeThing type_u32_obj { .kind = TypeKind::U32, .data = {} };
static TypeThing type_u64_obj { .kind = TypeKind::U64, .data = {} };

static TypeThing type_i8_obj { .kind = TypeKind::I8, .data = {} };
static TypeThing type_i16_obj { .kind = TypeKind::I16, .data = {} };
static TypeThing type_i32_obj { .kind = TypeKind::I32, .data = {} };
static TypeThing type_i64_obj { .kind = TypeKind::I64, .data = {} };

static TypeThing type_f32_obj { .kind = TypeKind::F32, .data = {} };
static TypeThing type_f64_obj { .kind = TypeKind::F64, .data = {} };

static TypeThing type_inull_obj { .kind = TypeKind::I_NULL, .data = {} };

TypeThing* type_void = &type_void_obj;
TypeThing* type_bool = &type_bool_obj;

TypeThing* type_unint = &type_unint_obj;
TypeThing* type_unfloat = &type_unfloat_obj;

TypeThing* type_u8 = &type_u8_obj;
TypeThing* type_u16 = &type_u16_obj;
TypeThing* type_u32 = &type_u32_obj;
TypeThing* type_u64 = &type_u64_obj;

TypeThing* type_i8 = &type_i8_obj;
TypeThing* type_i16 = &type_i16_obj;
TypeThing* type_i32 = &type_i32_obj;
TypeThing* type_i64 = &type_i64_obj;

TypeThing* type_f32 = &type_f32_obj;
TypeThing* type_f64 = &type_f64_obj;

TypeThing* type_inull = &type_inull_obj;
