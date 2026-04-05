#pragma once

#include "lexer.h"
#include "type.h"
#include <deque>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

enum class DeclKind : uint8_t { VAR, FUNC, STRUCT, REGION, CLASS };

struct Decl;

struct VarDecl {
  TypeThing *type;
};

struct FuncDecl {
  std::vector<Decl *> params;
  TypeThing *returnType;
};

struct StructDecl {
  std::vector<TypeThing *> fieldTypes;
  std::vector<std::string> fieldNames;
};

struct RegionDecl {
  uint32_t id;
};

using DeclData = std::variant<VarDecl, FuncDecl, StructDecl, RegionDecl>;

struct Decl {
  DeclKind kind;
  Lexer::Token name;
  DeclData data;

  TypeThing *toType();
};

extern std::deque<std::unordered_map<std::string, Decl *>> declarations;
