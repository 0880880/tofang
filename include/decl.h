#pragma once

#include "ast_base.h"
#include "decl_type.h"
#include "lexer.h"
#include "territory_ast.h"
#include "type.h"
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <variant>

enum class DeclKind : uint8_t {
  VAR,
  FUNC,
  GENERIC_FUNC,
  STRUCT,
  REGION,
  CLASS
};

struct VarDecl {
  TypeThing *type;
};

struct FuncDecl {
  std::vector<TypeThing *> genericParams;
  std::vector<Decl *> params;
  TypeThing *returnType;
};

struct StructDecl {
  std::vector<TypeThing *> fieldTypes;
  std::vector<Lexer::Token> fieldNames;
  std::vector<Expr *> fieldDefs;
};

using DeclData = std::variant<VarDecl, FuncDecl, StructDecl>;

struct Decl {
  DeclKind kind;
  Lexer::Token name;
  DeclData data;
  Region *region;

  TypeThing *toType();
};

extern std::deque<std::unordered_map<std::string, Decl *>> declarations;
