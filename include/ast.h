#pragma once

#include "decl.h"
#include "lexer.h"
#include "territory_ast.h"
#include "type.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace std;

class ASTNode {
public:
  virtual auto toString() -> string = 0;
  virtual auto walk() -> std::vector<ASTNode *> = 0;
  virtual ~ASTNode() = default;
};

class Expr : public ASTNode {
public:
  TypeThing *t = nullptr;
  ~Expr() override = default;
};

class LiteralExpr : public Expr {
public:
  enum Type : uint8_t {
    Null,
    Integer,
    Decimal,
    Boolean,
    Char,
    String,
    MetaType
  };

  Type type;

  Lexer::Token value;
  TypeThing *typeValue = nullptr;

  LiteralExpr(Type type, Lexer::Token value)
      : type(type), value(std::move(value)) {}

  std::vector<ASTNode *> walk() override { return {}; }

  string toString() override {
    string type_string = "integer";
    if (type == Type::Decimal) {
      type_string = "decimal";
    } else if (type == Type::Boolean) {
      type_string = "boolean";
    } else if (type == Type::Char) {
      type_string = "char";
    } else if (type == Type::String) {
      type_string = "string";
    } else if (type == Type::MetaType) {
      return "LiteralExpr<meta:" + typeValue->toString() + ">";
    }
    return "LiteralExpr<" + type_string + ":" + value.value + ">";
  }
};

class BinaryExpr : public Expr {
public:
  Expr *left = nullptr;
  Lexer::Token op;
  Expr *right = nullptr;

  BinaryExpr(Expr *left, Lexer::Token op, Expr *right)
      : left(left), op(std::move(op)), right(right) {}

  std::vector<ASTNode *> walk() override { return {left, right}; }

  string toString() override { return "BinaryExpr<" + op.value + ">"; }
};

class UnaryExpr : public Expr {
public:
  Lexer::Token op;
  Expr *right = nullptr;

  UnaryExpr(Lexer::Token op, Expr *right) : op(std::move(op)), right(right) {}

  std::vector<ASTNode *> walk() override { return {right}; }

  string toString() override { return "UnaryExpr<" + op.value + ">"; }
};

class GroupingExpr : public Expr {
public:
  Expr *expression = nullptr;

  GroupingExpr(Expr *expression) : expression(expression) {}

  std::vector<ASTNode *> walk() override { return {expression}; }

  string toString() override { return "GroupingExpr"; }
};

class VariableExpr : public Expr {
public:
  Lexer::Token name;
  Decl *decl = nullptr;

  VariableExpr(Lexer::Token name) : name(std::move(name)) {}

  std::vector<ASTNode *> walk() override { return {}; }

  string toString() override { return "VariableExpr<" + name.value + ">"; }
};

class CallExpr : public Expr {
public:
  Expr *func = nullptr;
  vector<Expr *> typeArgs;
  vector<Expr *> args;

  CallExpr(Expr *func) : func(func) {}

  std::vector<ASTNode *> walk() override {
    std::vector<ASTNode *> e;
    e.push_back(func);
    e.insert(e.begin(), args.begin(), args.end());
    e.insert(e.begin(), typeArgs.begin(), typeArgs.end());
    return e;
  }

  string toString() override {
    string buf = "";
    for (size_t i = 0; i < typeArgs.size(); i++) {
      if (typeArgs.size() - 1 - i) {
        buf += ", ";
      }
      buf += typeArgs[i]->toString();
    }
    return "CallExpr<" + buf + ">";
  }
};

class IndexExpr : public Expr {
public:
  Expr *arr = nullptr;
  Expr *i = nullptr;

  IndexExpr(Expr *arr, Expr *i) : arr(arr), i(i) {}

  std::vector<ASTNode *> walk() override { return {arr, i}; }

  string toString() override { return "IndexExpr"; }
};

class UpdateExpr : public Expr {
public:
  Expr *obj = nullptr;
  Expr *value = nullptr;

  UpdateExpr(Expr *obj, Expr *value) : obj(obj), value(value) {}

  std::vector<ASTNode *> walk() override { return {obj, value}; }

  string toString() override { return "UpdateExpr"; }
};

class Stmt : public ASTNode {
public:
  Region *region = nullptr;

  ~Stmt() override = default;
};

class AssignStmt : public Stmt {
public:
  TypeThing *type = nullptr;
  Lexer::Token name;
  Expr *value = nullptr;
  Decl *decl = nullptr;

  AssignStmt(TypeThing *type, Lexer::Token name, Expr *value)
      : type(type), name(std::move(name)), value(value) {}

  std::vector<ASTNode *> walk() override { return {value}; }

  string toString() override {
    return "AssignStmt<" + type->toString() + " " + name.value + ">";
  }
};

class AssignExpr : public Expr {
public:
  Expr *left = nullptr;
  Lexer::Token op;
  Expr *right = nullptr;
  Decl *decl = nullptr;

  AssignExpr(Expr *left, Lexer::Token op, Expr *right)
      : left(left), op(std::move(op)), right(right) {}

  std::vector<ASTNode *> walk() override { return {left, right}; }

  string toString() override {
    return "AssignExpr<" + left->toString() + " " + op.value + " " +
           right->toString() + ">";
  }
};

class ExprStmt : public Stmt {
public:
  Expr *expression = nullptr;

  ExprStmt(Expr *expression) : expression(expression) {}

  std::vector<ASTNode *> walk() override { return {expression}; }

  string toString() override { return "ExprStmt"; }
};

class BlockStmt : public Stmt {
public:
  vector<Stmt *> statements;

  std::vector<ASTNode *> walk() override {
    std::vector<ASTNode *> result;
    result.reserve(statements.size());

    std::ranges::transform(statements, std::back_inserter(result),
                           [](Stmt *s) { return static_cast<ASTNode *>(s); });

    return result;
  }

  string toString() override { return "BlockStmt"; }
};

class FuncStmt : public Stmt {
public:
  TypeThing *returnType = nullptr;
  Lexer::Token name;
  bool generic = false;
  vector<TypeThing *> genericParams;
  vector<TypeThing *> paramTypes;
  vector<Lexer::Token> paramNames;
  BlockStmt body;
  Decl *decl = nullptr;

  FuncStmt(TypeThing *returnType, Lexer::Token name)
      : returnType(returnType), name(std::move(name)) {}

  std::vector<ASTNode *> walk() override { return {&body}; }

  string toString() override {
    string buf = "";
    for (size_t i = 0; i < genericParams.size(); i++) {
      if (genericParams.size() - 1 - i) {
        buf += ", ";
      }
      buf += genericParams[i]->toString();
    }
    return "FuncStmt<" + buf + "> <" + returnType->toString() + " " +
           name.value + ">";
  }
};

class ReturnStmt : public Stmt {
public:
  Expr *value = nullptr;

  std::vector<ASTNode *> walk() override { return {value}; }

  string toString() override { return "ReturnStmt"; }
};

class StructStmt : public Stmt {
public:
  Lexer::Token name;
  std::vector<TypeThing *> types;
  std::vector<Lexer::Token> names;
  std::vector<Expr *> definitions;

  std::vector<ASTNode *> walk() override {
    vector<ASTNode *> v;
    v.reserve(definitions.size());
    for (Expr *e : definitions) {
      v.push_back(e);
    }
    return v;
  }

  string toString() override {
    std::string defs = "";
    for (size_t i = 0; i < types.size(); ++i) {
      defs += types[i]->toString();
      defs += " ";
      defs += names[i].value;
      defs += " = ";
      defs += definitions[i]->toString();
      defs += "; ";
    }
    return "Struct{ " + defs + "}";
  }
};

class IfStmt;

class ElseStmt : public Stmt {
public:
  IfStmt *ifStmt = nullptr;
  BlockStmt body;

  std::vector<ASTNode *> walk() override { return {&body}; }

  string toString() override { return "ElseStmt"; }
};

class IfStmt : public Stmt {
public:
  Expr *condition = nullptr;
  BlockStmt body;
  IfStmt *elseIf = nullptr;
  ElseStmt *elseStmt = nullptr;

  std::vector<ASTNode *> walk() override {
    std::vector<ASTNode *> v = {condition, &body};
    if (elseIf) {
      v.push_back(elseIf);
    }
    if (elseStmt) {
      v.push_back(elseStmt);
    }
    return v;
  }

  string toString() override { return "IfStmt"; }
};

class ForStmt : public Stmt {
public:
  Stmt *init = nullptr;
  Expr *condition = nullptr;
  Expr *update = nullptr;
  BlockStmt body;

  std::vector<ASTNode *> walk() override {
    return {init, condition, update, &body};
  }

  string toString() override { return "ForStmt"; }
};

class RegionStmt : public Stmt {

public:
  Lexer::Token name;
  BlockStmt body;
  Decl *decl = nullptr;

  RegionStmt(Lexer::Token name) : name(std::move(name)) {}

  std::vector<ASTNode *> walk() override { return {&body}; }

  string toString() override { return "RegionStmt<" + name.value + ">"; }
};

class AttribExpr : public Expr {
public:
  Expr *foo = nullptr;
  Lexer::Token bar;

  AttribExpr(Expr *foo, Lexer::Token bar) : foo(foo), bar(std::move(bar)) {}

  std::vector<ASTNode *> walk() override { return {foo}; }

  string toString() override { return "AttribExpr<" + bar.value + ">"; }
};

class Program : public Stmt {
public:
  vector<Stmt *> statements;

  std::vector<ASTNode *> walk() override {
    std::vector<ASTNode *> result;
    result.reserve(statements.size());

    std::ranges::transform(statements, std::back_inserter(result),
                           [](Stmt *s) { return static_cast<ASTNode *>(s); });

    return result;
  }

  string toString() override { return "Program"; }
};
