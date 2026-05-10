#pragma once

#include "ast_base.h"
#include "decl_type.h"
#include "lexer.h"
#include "territory_ast.h"
#include "type.h"
#include <cstdint>
#include <deque>
#include <llvm/IR/Instructions.h>
#include <string>
#include <unordered_map>
#include <variant>

class FuncStmt;

enum class DeclKind : uint8_t {
    VAR,
    FUNC,
    GENERIC_FUNC,
    STRUCT,
    REGION,
    CLASS
};

struct VarDecl {
    TypeThing* type;
    size_t arg_index;
};

struct FuncDecl {
    std::vector<TypeThing*> genericParams;
    std::vector<Decl*> params;
    TypeThing* returnType;
    Decl* parentStruct = nullptr;
    llvm::Function* llvm = nullptr;
};

struct StructDecl {
    std::vector<TypeThing*> fieldTypes;
    std::vector<Lexer::Token> fieldNames;
    std::vector<Expr*> fieldDefs;
    std::vector<Decl*> methods;
    llvm::StructType* llvm = nullptr;
};

enum class Visibility {
    PRIVATE,
    PROTECTED,
    PUBLIC,
};

using DeclData
    = std::variant<VarDecl, FuncDecl, StructDecl>;

struct Decl {
    DeclKind kind;
    Lexer::Token name;
    DeclData data;
    Region* region = nullptr;
    Visibility visibility = Visibility::PROTECTED;
    llvm::AllocaInst* alloca = nullptr;
    bool func_param = false;

    TypeThing* toType();
};

extern std::deque<std::unordered_map<std::string, Decl*>> declarations;
