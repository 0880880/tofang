#pragma once

#include "decl_type.h"
#include <cstddef>
#include <cstdint>
#include <llvm/IR/Type.h>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

struct IRContext;

enum class TypeKind : uint8_t {
    VOID,
    BOOL,
    U8,
    U16,
    U32,
    U64,
    I8,
    I16,
    I32,
    I64,
    F32,
    F64,
    FUNCTION,
    NULLABLE,
    UNTYPED_INT,
    UNTYPED_FLOAT,
    POINTER,
    ARRAY,
    REGION,
    STRUCT,
    META,
    TYPE_VAR,
    GENERIC_FUNC,
    I_NULL,
    USER_TYPE,
    SLICE,
};

struct TypeThing;

struct NullableType {
    TypeThing* base;
};

struct PtrType {
    TypeThing* pointee;
};

struct ArrType {
    TypeThing* element;
    size_t length;
};

struct FuncType {
    std::vector<TypeThing*> params;
    TypeThing* return_type;
};

struct MetaType {
    TypeThing* type;
};

struct VarType {
    std::string name;
};

struct RegionType {
    Decl* region;
};

struct StructType {
    std::string name;
    Decl* decl = nullptr;
};

struct GenericFuncType {
    std::vector<TypeThing*> type_params;
    std::vector<TypeThing*> params;
    TypeThing* return_type;
};

struct UserType {
    std::string iden;
};

struct SliceType {
    TypeThing *element;
};

using TypeData = std::variant<NullableType, RegionType, PtrType,
    ArrType, StructType, FuncType, MetaType, VarType,
    GenericFuncType, UserType, SliceType>;

struct TypeThing {
    TypeKind kind;
    TypeData data;

    std::string toString();

    llvm::Type* getLLVM(const IRContext& ir);
};

struct TypeKey {
    TypeKind kind;

    TypeThing* a = nullptr;
    TypeThing* b = nullptr;

    size_t length = 0;
    Decl* region = nullptr;

    std::string name;

    std::vector<TypeThing*> params;
    std::vector<TypeThing*> params_b;
};

constexpr inline bool isUnsigned(const TypeThing* t)
{
    return t->kind == TypeKind::U8 || t->kind == TypeKind::U16 || t->kind == TypeKind::U32 || t->kind == TypeKind::U64;
}
constexpr inline bool isSigned(const TypeThing* t)
{
    return t->kind == TypeKind::I8 || t->kind == TypeKind::I16 || t->kind == TypeKind::I32 || t->kind == TypeKind::I64;
}
constexpr inline bool isInt(const TypeThing* t)
{
    return isUnsigned(t) || isSigned(t) || t->kind == TypeKind::UNTYPED_INT;
}
constexpr inline bool isFloat(const TypeThing* t)
{
    return t->kind == TypeKind::F32 || t->kind == TypeKind::F64 || t->kind == TypeKind::UNTYPED_FLOAT;
}
constexpr inline bool isNumeric(const TypeThing* t)
{
    return isFloat(t) || isInt(t);
}

inline void hashCombine(size_t& h, size_t v)
{
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
}

struct TypeKeyHash {
    size_t operator()(TypeKey const& k) const
    {
        size_t h = 0;

        hashCombine(h, std::hash<int> {}(static_cast<int>(k.kind)));
        hashCombine(h, std::hash<void*> {}(k.a));
        hashCombine(h, std::hash<void*> {}(k.b));
        hashCombine(h, std::hash<size_t> {}(k.length));
        hashCombine(h, std::hash<void*> {}(k.region));
        hashCombine(h, std::hash<std::string> {}(k.name));

        for (auto p : k.params) {
            hashCombine(h, std::hash<void*> {}(p));
        }

        for (auto p : k.params_b) {
            hashCombine(h, std::hash<void*> {}(p));
        }

        return h;
    }
};

struct TypeKeyEq {
    bool operator()(TypeKey const& a, TypeKey const& b) const
    {
        if (a.kind != b.kind || a.a != b.a || a.b != b.b || a.length != b.length || a.region != b.region || a.name != b.name || a.params.size() != b.params.size() || a.params_b.size() != b.params_b.size()) {
            return false;
        }

        for (size_t i = 0; i < a.params.size(); i++) {
            if (a.params[i] != b.params[i]) {
                return false;
            }
        }

        for (size_t i = 0; i < a.params_b.size(); i++) {
            if (a.params_b[i] != b.params_b[i]) {
                return false;
            }
        }

        return true;
    }
};

class TypeInterner {
    std::unordered_map<TypeKey, TypeThing*, TypeKeyHash, TypeKeyEq> table;

public:
    TypeThing* getNullable(TypeThing* base);

    TypeThing* getPointer(TypeThing* pointee);

    TypeThing* getReference(TypeThing* referee);

    TypeThing* getArray(TypeThing* elem, size_t len);

    TypeThing* getFunction(const std::vector<TypeThing*>& params,
        TypeThing* returnType);

    TypeThing* getGenericFunction(const std::vector<TypeThing*>& type_params,
        const std::vector<TypeThing*>& params,
        TypeThing* returnType);

    TypeThing* getMeta(TypeThing* t);

    TypeThing* getTypeVar(const std::string& name);

    TypeThing* getRegion(Decl* region);

    TypeThing* getStruct(const std::string& name);

    TypeThing* getUserType(const std::string& name);

    TypeThing* substitute(
        TypeThing* t,
        std::unordered_map<TypeKey, TypeThing*, TypeKeyHash, TypeKeyEq>& subst);
};

extern TypeInterner* interner;

extern TypeThing* type_void;
extern TypeThing* type_bool;
extern TypeThing* type_unint;
extern TypeThing* type_unfloat;
extern TypeThing* type_u8;
extern TypeThing* type_u16;
extern TypeThing* type_u32;
extern TypeThing* type_u64;
extern TypeThing* type_i8;
extern TypeThing* type_i16;
extern TypeThing* type_i32;
extern TypeThing* type_i64;
extern TypeThing* type_f32;
extern TypeThing* type_f64;

extern TypeThing* type_inull;
