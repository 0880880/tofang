#include "typechecker.h"
#include "ast.h"
#include "decl.h"
#include "type.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

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

constexpr inline static bool isUnsigned(const TypeThing *t) {
  return t->kind == TypeKind::U8 || t->kind == TypeKind::U16 ||
         t->kind == TypeKind::U32 || t->kind == TypeKind::U64;
}
constexpr inline static bool isSigned(const TypeThing *t) {
  return t->kind == TypeKind::I8 || t->kind == TypeKind::I16 ||
         t->kind == TypeKind::I32 || t->kind == TypeKind::I64;
}
constexpr inline static bool isInt(const TypeThing *t) {
  return isUnsigned(t) || isSigned(t) || t->kind == TypeKind::UNTYPED_INT;
}
constexpr inline static bool isFloat(const TypeThing *t) {
  return t->kind == TypeKind::F32 || t->kind == TypeKind::F64 ||
         t->kind == TypeKind::UNTYPED_FLOAT;
}
constexpr inline static bool isNumeric(const TypeThing *t) {
  return isFloat(t) || isInt(t);
}
static int numOrder(const TypeThing *t) {
  switch (t->kind) {
  case TypeKind::F64:
    return 100;
  case TypeKind::F32:
    return 99;
  case TypeKind::I64:
  case TypeKind::U64:
    return 98;
  case TypeKind::I32:
  case TypeKind::U32:
    return 97;
  case TypeKind::I16:
  case TypeKind::U16:
    return 96;
  case TypeKind::I8:
  case TypeKind::U8:
    return 95;
  case TypeKind::UNTYPED_INT:
  case TypeKind::UNTYPED_FLOAT:
    return 90;
  default:
    return 0;
  }
}

void TypeChecker::close(ASTNode *node) {

  if (auto *lit = dynamic_cast<LiteralExpr *>(node)) {
    switch (lit->type) {
    case LiteralExpr::Integer:
      lit->t = type_unint;
      break;
    case LiteralExpr::Decimal:
      lit->t = type_unfloat;
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

    string op = bin->op.value;

    vector<pair<TypeThing *, TypeThing *>> combinations = {
        {bin->left->t, bin->right->t}, {bin->right->t, bin->left->t}};

    for (auto &[lhs, rhs] : combinations) {
      if (op == "+" || op == "-" || op == "*" || op == "/") {
        if (!isNumeric(lhs) || !isNumeric(rhs)) {
          error("operands of aritmhetic operation must be numeric");
        }
        if ((isUnsigned(lhs) && !isUnsigned(rhs)) ||
            (!isUnsigned(lhs) && isUnsigned(rhs))) {
          error(
              "arithmetic operation between unsigned and non-unsigned type is "
              "invalid.");
        }
        if (lhs->kind == TypeKind::UNTYPED_INT &&
            rhs->kind == TypeKind::UNTYPED_INT) {
          bin->t = type_i32;
          break;
        }
        if (lhs->kind == TypeKind::UNTYPED_FLOAT &&
            (rhs->kind == TypeKind::UNTYPED_FLOAT ||
             rhs->kind == TypeKind::UNTYPED_INT)) {
          bin->t = type_f64;
          break;
        }
        if (lhs == rhs) {
          bin->t = lhs;
          break;
        }
        if (numOrder(lhs) >= numOrder(rhs)) {
          bin->t = lhs;
          break;
        }
      } else if (op == "&" || op == "|" || op == "^") {
        if (!isInt(lhs) || !isInt(rhs)) {
          error("operands of bitwise operation must be integer");
        }
        if (lhs->kind == TypeKind::UNTYPED_INT &&
            rhs->kind == TypeKind::UNTYPED_INT) {
          bin->t = type_i32;
          break;
        }
        if (lhs == rhs) {
          bin->t = lhs;
          break;
        }
        if (numOrder(lhs) >= numOrder(rhs)) {
          bin->t = lhs;
          break;
        }
      } else if (op == "&&" || op == "||") {
        if (lhs != type_bool || rhs != type_bool) {
          error("operands of logical operation must be bool");
        }
        bin->t = type_bool;
        break;
      } else if (op == ">" || op == "<" || op == ">=" || op == "<=") {
        if (!isNumeric(lhs) || !isNumeric(rhs)) {
          error("both sides of comparison must be numeric");
        }
        bin->t = type_bool;
        break;
      } else if (op == "==") {
        // TODO should be better
        if ((isNumeric(lhs) && !isNumeric(rhs)) ||
            (!isNumeric(lhs) && isNumeric(rhs))) {
          error("both sides of equality operation must be comparable");
        }
        bin->t = type_bool;
        break;
      }
    }
  } else if (auto *assign = dynamic_cast<AssignExpr *>(node)) {

    TypeThing *lhs = assign->left->t;
    TypeThing *rhs = assign->right->t;

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

        assign->t = interner->getPointer(rp.pointee);

        return;
      }
    }

    error("assignment type mismatch");
  } else if (auto *ret = dynamic_cast<ReturnStmt *>(node)) {

    TypeThing *typ = ret->value->t;

    if (!(isNumeric(typ) && isNumeric(current_function->returnType) &&
          numOrder(current_function->returnType) >= numOrder(typ)) &&
        (typ != current_function->returnType)) {
      error("wrong return type");
    }
  } else if (auto *assign_stmt = dynamic_cast<AssignStmt *>(node)) {

    TypeThing *lhs = assign_stmt->type;
    TypeThing *rhs = assign_stmt->value->t;

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

        assign_stmt->type = interner->getPointer(rp.pointee);

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