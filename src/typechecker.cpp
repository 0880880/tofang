#include "typechecker.h"
#include "ast.h"
#include "decl.h"
#include "type.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace std;

static FuncStmt *current_function = nullptr;

static void error(const string &message) {
  cerr << "Type Error:\n    " << message << '\n';
  exit(1);
}

void TypeChecker::open(ASTNode *node) {
  if (auto *func = dynamic_cast<FuncStmt *>(node)) {
    current_function = func;
  }
}

void TypeChecker::close(ASTNode *node) {

  if (auto *lit = dynamic_cast<LiteralExpr *>(node)) {
    switch (lit->type) {
    case LiteralExpr::Integer:
      lit->t = type_i32;
      break;
    case LiteralExpr::Decimal:
      lit->t = type_f64;
      break;
    case LiteralExpr::Boolean:
      lit->t = type_bool;
      break;
    case LiteralExpr::Char:
      lit->t = type_u8;
      break;
    case LiteralExpr::MetaType:
      lit->t = interner->getMeta(lit->typeValue);
      break;
    case LiteralExpr::String:
      throw runtime_error("Unhandeled String literal");
    }
  } else if (auto *var = dynamic_cast<VariableExpr *>(node)) {
    var->t = var->decl->toType();
  } else if (auto *bin = dynamic_cast<BinaryExpr *>(node)) {

    TypeThing *lhs = bin->left->t;
    TypeThing *rhs = bin->right->t;

    if (lhs != rhs) {
      error("type mismatch in binary op");
    }

    bin->t = lhs;
  } else if (auto *assign = dynamic_cast<AssignExpr *>(node)) {

    TypeThing *lhs = assign->left->t;
    TypeThing *rhs = assign->right->t;

    if (lhs != rhs) {
      error("type mismatch in assignment");
    }

    assign->t = lhs;
  } else if (auto *ret = dynamic_cast<ReturnStmt *>(node)) {

    TypeThing *typ = ret->value->t;

    if (typ != current_function->returnType) {
      error("wrong return type");
    }
  } else if (auto *assign = dynamic_cast<AssignStmt *>(node)) {

    TypeThing *lhs = assign->type;
    TypeThing *rhs = assign->value->t;

    assert(lhs != nullptr);
    assert(rhs != nullptr);

    if (lhs == rhs) {
      return;
    }

    if (lhs->kind == TypeKind::POINTER && rhs->kind == TypeKind::POINTER) {
      auto lp = std::get<PtrType>(lhs->data);
      auto rp = std::get<PtrType>(rhs->data);

      if (lp.pointee->kind != TypeKind::REGIONED &&
          rp.pointee->kind == TypeKind::REGIONED) {

        assign->type = interner->getPointer(rp.pointee);

        return;
      }
    }

    error("assignment type mismatch");
  } else if (auto cal = dynamic_cast<CallExpr *>(node)) {

    if (auto *att = dynamic_cast<AttribExpr *>(cal->func)) {

      if (att->bar.value == "alloc") {

        TypeThing *region_type = att->foo->t;

        if (region_type->kind != TypeKind::REGION) {
          error("alloc must be called on a region");
        }

        if (cal->args.size() != 1) {
          error("alloc expects one type argument");
        }

        TypeThing *arg = cal->args[0]->t;

        if (arg->kind != TypeKind::META) {
          error("alloc expects a type");
        }

        TypeThing *allocated = std::get<MetaType>(arg->data).type;

        cal->t = interner->getPointer(interner->getRegioned(
            allocated, std::get<RegionType>(region_type->data).region));

        return;
      }
    }

    if (cal->func->t->kind == TypeKind::FUNCTION) {
      FuncType f = std::get<FuncType>(cal->func->t->data);
      if (f.params.size() != cal->args.size()) {
        error("Function call argument size mismatch: Expected " +
              to_string(f.params.size()) + " got " +
              to_string(cal->args.size()) + " instead.");
      }
      for (size_t i = 0; i < f.params.size(); ++i) {
        if (f.params[i] != cal->args[i]->t) {
          error("Function argument type mismatch: Got " +
                cal->args[i]->t->toString() + " instead of " +
                f.params[i]->toString() + ".");
        }
      }
      cal->t = f.return_type;
      return;
    } else if (cal->func->t->kind == TypeKind::GENERIC_FUNC) {
      GenericFuncType g = std::get<GenericFuncType>(cal->func->t->data);
      if (g.params.size() != cal->args.size()) {
        error("Function call argument size mismatch: Expected " +
              to_string(g.params.size()) + " got " +
              to_string(cal->args.size()) + " instead.");
      }
      if (g.type_params.size() != cal->typeArgs.size()) {
        error("Function call generic arguments size mismatch: Expected " +
              to_string(g.type_params.size()) + " got " +
              to_string(cal->typeArgs.size()) + " instead.");
      }
      std::unordered_map<TypeKey, TypeThing *, TypeKeyHash, TypeKeyEq> subst;
      for (size_t i = 0; i < g.type_params.size(); ++i) {
        if (cal->typeArgs[i]->t->kind != TypeKind::META) {
          error("Function call generic arguments must be types.");
        }
        TypeKey k = {};
        k.kind = TypeKind::TYPE_VAR;
        k.name = std::get<VarType>(g.type_params[i]->data).name;

        subst[k] = dynamic_cast<LiteralExpr *>(cal->typeArgs[i])->typeValue;
      }
      std::vector<TypeThing *> params = {};
      params.reserve(g.params.size());
      for (TypeThing *p : g.params) {
        params.push_back(interner->substitute(p, subst));
      }
      FuncType f = {.params = params,
                    .return_type = interner->substitute(g.return_type, subst)};
      for (size_t i = 0; i < f.params.size(); ++i) {
        if (f.params[i] != cal->args[i]->t) {
          error("Function argument type mismatch: Got " +
                cal->args[i]->t->toString() + " instead of " +
                f.params[i]->toString() + ".");
        }
      }
      cal->t = f.return_type;
    } else {
      error("Cannot call non-function type");
    }
  }
}