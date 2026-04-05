#include "typechecker.h"
#include "ast.h"
#include "decl.h"
#include "type.h"
#include <iostream>
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
  } else if (auto *ret = dynamic_cast<ReturnStmt *>(node)) {

    TypeThing *typ = ret->value->t;

    if (typ != current_function->returnType) {
      error("wrong return type");
    }
  } else if (auto *assign = dynamic_cast<AssignStmt *>(node)) {

    TypeThing *lhs = assign->type;
    TypeThing *rhs = assign->value->t;

    if (lhs == rhs) { // L67
      return;
    }

    if (lhs->kind == TypeKind::POINTER &&
        rhs->kind == TypeKind::POINTER) { // L71
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
            allocated, std::get<RegionType>(region_type->data).id));

        cout << "Wat? " << cal->t->toString() << "\n";
        return;
      }
    }
  }
}