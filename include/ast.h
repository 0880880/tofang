#pragma once

#include "decl.h"
#include "lexer.h"
#include "type.h"
#include <algorithm>
#include <atomic>
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
  TypeThing *t;
  ~Expr() override = default;
};

class LiteralExpr : public Expr {
public:
  enum Type : uint8_t { Integer, Decimal, Boolean, Char, String, MetaType };

  Type type;

  Lexer::Token value;
  TypeThing *typeValue;

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
  Expr *left;
  Lexer::Token op;
  Expr *right;

  BinaryExpr(Expr *left, Lexer::Token op, Expr *right)
      : left(left), op(std::move(op)), right(right) {}

  std::vector<ASTNode *> walk() override { return {left, right}; }

  string toString() override { return "BinaryExpr<" + op.value + ">"; }
};

class UnaryExpr : public Expr {
public:
  Lexer::Token op;
  Expr *right;

  UnaryExpr(Lexer::Token op, Expr *right) : op(std::move(op)), right(right) {}

  std::vector<ASTNode *> walk() override { return {right}; }

  string toString() override { return "UnaryExpr<" + op.value + ">"; }
};

class GroupingExpr : public Expr {
public:
  Expr *expression;

  GroupingExpr(Expr *expression) : expression(expression) {}

  std::vector<ASTNode *> walk() override { return {expression}; }

  string toString() override { return "GroupingExpr"; }
};

class VariableExpr : public Expr {
public:
  Lexer::Token name;
  Decl *decl;

  VariableExpr(Lexer::Token name) : name(std::move(name)) {}

  std::vector<ASTNode *> walk() override { return {}; }

  string toString() override { return "VariableExpr<" + name.value + ">"; }
};

class CallExpr : public Expr {
public:
  Expr *func;
  vector<Expr *> typeArgs;
  vector<Expr *> args;

  CallExpr(Expr *func) : func(func) {}

  std::vector<ASTNode *> walk() override {
    std::vector<ASTNode *> e;
    e.push_back(func);
    e.insert(e.begin(), args.begin(), args.end());
    return e;
  }

  string toString() override { return "CallExpr"; }
};

class IndexExpr : public Expr {
public:
  Expr *arr;
  Expr *i;

  IndexExpr(Expr *arr, Expr *i) : arr(arr), i(i) {}

  std::vector<ASTNode *> walk() override { return {arr, i}; }

  string toString() override { return "IndexExpr"; }
};

class UpdateExpr : public Expr {
public:
  Expr *obj;
  Expr *value;

  UpdateExpr(Expr *obj, Expr *value) : obj(obj), value(value) {}

  std::vector<ASTNode *> walk() override { return {obj, value}; }

  string toString() override { return "UpdateExpr"; }
};

class Stmt : public ASTNode {
public:
  ~Stmt() override = default;
};

class AssignStmt : public Stmt {
public:
  TypeThing *type;
  Lexer::Token name;
  Expr *value;
  Decl *decl;

  AssignStmt(TypeThing *type, Lexer::Token name, Expr *value)
      : type(type), name(std::move(name)), value(value) {}

  std::vector<ASTNode *> walk() override { return {value}; }

  string toString() override {
    return "AssignStmt<" + type->toString() + " " + name.value + ">";
  }
};

class ExprStmt : public Stmt {
public:
  Expr *expression;

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
  TypeThing *returnType;
  Lexer::Token name;
  vector<TypeThing *> genericParams;
  vector<TypeThing *> paramTypes;
  vector<Lexer::Token> paramNames;
  BlockStmt body;
  Decl *decl;

  FuncStmt(TypeThing *returnType, Lexer::Token name)
      : returnType(returnType), name(std::move(name)) {}

  std::vector<ASTNode *> walk() override { return {&body}; }

  string toString() override {
    return "FuncStmt<" + returnType->toString() + " " + name.value + ">";
  }
};

class ReturnStmt : public Stmt {
public:
  Expr *value;

  std::vector<ASTNode *> walk() override { return {value}; }

  string toString() override { return "ReturnStmt"; }
};

class IfStmt : public Stmt {
public:
  Expr *condition;
  BlockStmt body;
  IfStmt *elseIf;
  BlockStmt *elseBody;

  std::vector<ASTNode *> walk() override {
    return {condition, &body, elseIf, elseBody};
  }

  string toString() override { return "IfStmt"; }
};

class ForStmt : public Stmt {
public:
  Stmt *init;
  Expr *condition;
  Expr *update;
  BlockStmt body;

  std::vector<ASTNode *> walk() override {
    return {init, condition, update, &body};
  }

  string toString() override { return "ForStmt"; }
};

class RegionStmt : public Stmt {
private:
  static std::atomic<uint32_t> counter;

public:
  Lexer::Token name;
  BlockStmt body;
  Decl *decl;

  uint32_t id;

  RegionStmt(Lexer::Token name) : name(std::move(name)), id(++counter) {}

  std::vector<ASTNode *> walk() override { return {&body}; }

  string toString() override { return "RegionStmt<" + name.value + ">"; }
};

class AttribExpr : public Expr {
public:
  Expr *foo;
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
