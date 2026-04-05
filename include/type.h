#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

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
  UNTYPED_INT,
  UNTYPED_FLOAT,
  POINTER,
  REFERENCE,
  ARRAY,
  REGION,
  REGIONED,
  META,
  TYPE_VAR,
  GENERIC_FUNCTION,
};

struct TypeThing;

struct RegionedType {
  TypeThing *base;
  uint32_t regionID;
};

struct PtrType {
  TypeThing *pointee;
};

struct RefType {
  TypeThing *referee;
};

struct ArrType {
  TypeThing *element;
  size_t length;
};

struct StructType {
  std::string id;
};

struct FuncType {
  std::vector<TypeThing *> params;
  TypeThing *return_type;
};

struct MetaType {
  TypeThing *type;
};

struct VarType {
  std::string name;
};

struct RegionType {
  uint32_t id;
};

struct GenericFuncType {
  std::vector<TypeThing *> type_params;
  std::vector<TypeThing *> params;
  TypeThing *return_type;
};

using TypeData =
    std::variant<RegionedType, PtrType, RefType, ArrType, StructType, FuncType,
                 MetaType, VarType, RegionType, GenericFuncType>;

struct TypeThing {
  TypeKind kind;
  TypeData data;

  std::string toString();
};

struct TypeKey {
  TypeKind kind;

  TypeThing *a = nullptr;
  TypeThing *b = nullptr;

  size_t length = 0;
  uint32_t region = 0;

  std::string name;

  std::vector<TypeThing *> params;
};

struct TypeKeyHash {
  size_t operator()(TypeKey const &k) const {
    size_t h = std::hash<int>()(static_cast<int>(k.kind));

    h ^= std::hash<void *>()(k.a) << 1;
    h ^= std::hash<void *>()(k.b) << 2;
    h ^= std::hash<size_t>()(k.length) << 3;
    h ^= std::hash<uint32_t>()(k.region) << 4;
    h ^= std::hash<std::string>()(k.name) << 5;

    for (auto param : k.params) {
      h ^= std::hash<void *>()(param) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }

    return h;
  }
};

struct TypeKeyEq {
  bool operator()(TypeKey const &a, TypeKey const &b) const {
    if (a.kind != b.kind || a.a != b.a || a.b != b.b || a.length != b.length ||
        a.region != b.region || a.name != b.name ||
        a.params.size() != b.params.size()) {
      return false;
    }

    for (size_t i = 0; i < a.params.size(); i++) {
      if (a.params[i] != b.params[i]) {
        return false;
      }
    }

    return true;
  }
};

class TypeInterner {
  std::unordered_map<TypeKey, TypeThing *, TypeKeyHash, TypeKeyEq> table;

public:
  TypeThing *getRegion(uint32_t regionID);

  TypeThing *getRegioned(TypeThing *base, uint32_t regionID);

  TypeThing *getPointer(TypeThing *pointee);

  TypeThing *getReference(TypeThing *referee);

  TypeThing *getArray(TypeThing *elem, size_t len);

  TypeThing *getFunction(const std::vector<TypeThing *> &params,
                         TypeThing *returnType);

  TypeThing *getMeta(TypeThing *t);

  TypeThing *getTypeVar(const std::string &name);
};

extern TypeInterner *interner;

extern TypeThing *type_void;
extern TypeThing *type_bool;
extern TypeThing *type_unint;
extern TypeThing *type_unfloat;
extern TypeThing *type_u8;
extern TypeThing *type_u16;
extern TypeThing *type_u32;
extern TypeThing *type_u64;
extern TypeThing *type_i8;
extern TypeThing *type_i16;
extern TypeThing *type_i32;
extern TypeThing *type_i64;
extern TypeThing *type_f32;
extern TypeThing *type_f64;
