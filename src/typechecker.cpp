#include "typechecker.h"
#include "ast.h"
#include "decl.h"
#include "territory_ast.h"
#include "type.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

using namespace std;

TypeThing *RefinementEnv::lookup(Decl *d) {
  auto it = map.find(d);
  if (it != map.end()) {
    return it->second;
  }
  return nullptr;
}

bool Region::outlives(const Region *o) const {
  const Region *current = o;
  while (current) {
    if (current == this) {
      return true;
    }
    current = current->parent;
  }
  return false;
}

static void error(const string &message) {
  cerr << "Type Error:\n    " << message << '\n';
  cout.flush();
  cerr.flush();
  exit(1);
}

static RefinementEnv mergeEnvs(RefinementEnv &a, RefinementEnv &b) {
  RefinementEnv result;
  unordered_set<Decl *> keys;

  for (auto &[k, _] : a.map) {
    keys.insert(k);
  }
  for (auto &[k, _] : b.map) {
    keys.insert(k);
  }

  for (Decl *key : keys) {
    TypeThing *ta = a.lookup(key);
    TypeThing *tb = b.lookup(key);
    if (!ta) {
      result.map[key] = tb;
      continue;
    }
    if (!tb) {
      result.map[key] = ta;
      continue;
    }
    if (ta->kind == TypeKind::I_NULL && tb->kind == TypeKind::I_NULL) {
      result.map[key] = type_inull;
    } else if (ta->kind != TypeKind::I_NULL && tb->kind != TypeKind::I_NULL) {
      if (ta == tb) {
        result.map[key] = interner->getNullable(ta);
      }
    } else if (ta->kind == TypeKind::I_NULL && tb->kind != TypeKind::I_NULL) {
      result.map[key] = interner->getNullable(tb);
    } else if (ta->kind != TypeKind::I_NULL && tb->kind == TypeKind::I_NULL) {
      result.map[key] = interner->getNullable(ta);
    }
  }

  return result;
}

static RefinementEnv refineTrue(Expr *condition, RefinementEnv &env) {
  if (auto *bin = dynamic_cast<BinaryExpr *>(condition)) {
    if (bin->op.value == "&&") {
      auto env1 = refineTrue(bin->left, env);
      return refineTrue(bin->right, env1);
    } else if (bin->op.value == "||") {
      auto a = refineTrue(bin->left, env);
      auto b = refineTrue(bin->right, env);
      return mergeEnvs(a, b);
    }
  }
  if (condition->t->kind == TypeKind::NULLABLE) {
    if (auto var = dynamic_cast<VariableExpr *>(condition)) {
      RefinementEnv e = {};
      e.map[var->decl] = std::get<NullableType>(condition->t->data).base;
      return mergeEnvs(env, e);
    }
  }
  return env;
}

static RefinementEnv refineFalse(Expr *condition, RefinementEnv &env) {
  if (auto *bin = dynamic_cast<BinaryExpr *>(condition)) {
    if (bin->op.value == "&&") {
      auto a = refineFalse(bin->left, env);
      auto o = refineTrue(bin->left, env);
      auto b = refineFalse(bin->right, o);
      return mergeEnvs(a, b);
    } else if (bin->op.value == "||") {
      auto env1 = refineFalse(bin->left, env);
      return refineFalse(bin->right, env1);
    }
  }
  if (condition->t->kind == TypeKind::NULLABLE) {
    if (auto var = dynamic_cast<VariableExpr *>(condition)) {
      RefinementEnv e = {};
      e.map[var->decl] = type_inull;
      return mergeEnvs(env, e);
    }
  }
  return env;
}

RefinementEnv TypeChecker::checkBlock(BlockStmt *block) {
  RefinementEnv saved = current_env;

  for (auto &stmt : block->statements) {
    walk(stmt);
  }

  RefinementEnv result = current_env;
  current_env = saved;

  return result;
}

RefinementEnv TypeChecker::checkIf(IfStmt *node) {

  RefinementEnv old_env = current_env;

  walk(node->condition);

  auto true_env = refineTrue(node->condition, current_env);
  auto false_env = refineFalse(node->condition, current_env);

  current_env = true_env;
  RefinementEnv after_true = checkBlock(&node->body);

  RefinementEnv after_false;

  if (node->elseIf) {
    current_env = false_env;
    after_false = checkIf(node->elseIf);
  } else if (node->elseStmt) {
    current_env = false_env;
    after_false = checkBlock(&node->elseStmt->body);
  } else {
    after_false = false_env;
  }

  current_env = mergeEnvs(after_true, after_false);

  return current_env;
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
    case LiteralExpr::Null:
      lit->t = type_inull;
      break;
    case LiteralExpr::String:
      throw runtime_error("Unhandeled String literal");
    }
  } else if (auto *var = dynamic_cast<VariableExpr *>(node)) {
    TypeThing *refined = current_env.lookup(var->decl);

    if (refined) {
      var->t = refined;
    } else {
      var->t = var->decl->toType();
    }

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

    if (isNumeric(lhs) && isNumeric(rhs)) {
      if (numOrder(lhs) >= numOrder(rhs)) {
        assign->right->t = lhs;
        rhs = lhs;
      } else {
        error("assignment type mismatch " + lhs->toString() + " = " +
              rhs->toString());
      }
    }

    if (lhs->kind == TypeKind::NULLABLE && rhs->kind != TypeKind::NULLABLE) {
      rhs = interner->getNullable(rhs);
    }

    if (lhs == rhs) {
      return;
    }

    if (lhs->kind == TypeKind::NULLABLE && rhs == type_inull) {
      return;
    }

    TypeThing *l = lhs;
    TypeThing *r = rhs;
    while (true) {
      if (l->kind == TypeKind::REGIONED && r->kind == TypeKind::REGIONED) {
        auto &ld = std::get<RegionedType>(l->data);
        auto &rd = std::get<RegionedType>(r->data);

        if (!rd.region->region->outlives(ld.region->region)) {
          error("Territory: region of RHS does not outlive the region of LHS.");
        }
        l = ld.base;
        r = rd.base;
      } else if (l != r) {
        error("assignment type mismatch " + lhs->toString() + " = " +
              rhs->toString());
      } else if (l->kind == TypeKind::POINTER) {
        l = std::get<PtrType>(l->data).pointee;
        r = std::get<PtrType>(r->data).pointee;
      } else if (l->kind == TypeKind::REFERENCE) {
        l = std::get<RefType>(l->data).referee;
        r = std::get<RefType>(r->data).referee;
      } else {
        break;
      }
    }

    assign->t = rhs;

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

    if (isNumeric(lhs) && isNumeric(rhs)) {
      if (numOrder(lhs) >= numOrder(rhs)) {
        assign->right->t = lhs;
        rhs = lhs;
      } else {
        error("assignment type mismatch " + lhs->toString() + " = " +
              rhs->toString());
      }
    }

    if (lhs->kind == TypeKind::NULLABLE && rhs->kind != TypeKind::NULLABLE) {
      rhs = interner->getNullable(rhs);
    }

    if (lhs == rhs) {
      return;
    }

    if (lhs->kind == TypeKind::NULLABLE && rhs == type_inull) {
      return;
    }

    TypeThing *l = lhs;
    TypeThing *r = rhs;
    while (true) {
      if (l->kind == TypeKind::REGIONED && r->kind == TypeKind::REGIONED) {
        auto &ld = std::get<RegionedType>(l->data);
        auto &rd = std::get<RegionedType>(r->data);

        if (!rd.region->region->outlives(ld.region->region)) {
          error("Territory: region of RHS does not outlive the region of LHS.");
        }
        l = ld.base;
        r = rd.base;
      } else if (l != r) {
        error("assignment type mismatch " + lhs->toString() + " = " +
              rhs->toString());
      } else if (l->kind == TypeKind::POINTER) {
        l = std::get<PtrType>(l->data).pointee;
        r = std::get<PtrType>(r->data).pointee;
      } else if (l->kind == TypeKind::REFERENCE) {
        l = std::get<RefType>(l->data).referee;
        r = std::get<RefType>(r->data).referee;
      } else {
        break;
      }
    }

    assign->t = rhs;
    assign->left->t = rhs;

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

void TypeChecker::walk(ASTNode *node) {

  if (auto *ifs = dynamic_cast<IfStmt *>(node)) {
    checkIf(ifs);
    return;
  }

  open(node);

  for (auto sub : node->walk()) {
    if (!sub) {
      continue;
    }
    walk(sub);
  }

  close(node);
}