#include "territory.h"
#include "ast.h"
#include <iostream>
#include <stdexcept>

using namespace std;

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

Decl *getDecl(Expr *expr, bool required = false) {
  while (expr) {

    if (auto *v = dynamic_cast<VariableExpr *>(expr))
      return v->decl;

    if (auto *a = dynamic_cast<AttribExpr *>(expr)) {
      expr = a->foo;
      continue;
    }

    if (auto *i = dynamic_cast<IndexExpr *>(expr)) {
      expr = i->arr;
      continue;
    }

    break;
  }

  if (required)
    throw runtime_error("Territory: Unhandled expression.");

  return nullptr;
}

bool outlives(TypeThing *t, Region *region) {
  switch (t->kind) {
  case TypeKind::POINTER:
    return outlives(std::get<PtrType>(t->data).pointee, region);
  case TypeKind::REFERENCE:
    return outlives(std::get<RefType>(t->data).referee, region);
  case TypeKind::REGIONED: {
    auto &d = std::get<RegionedType>(t->data);
    if (d.region->region->outlives(region)) {
      return outlives(d.base, region);
    }
    return false;
  }
  default:
    return true;
  }
}

void Territory::open(ASTNode *node) {
  if (dynamic_cast<FuncStmt *>(node)) {
    regions.push_back({&regions.back()});
  } else if (auto *reg = dynamic_cast<RegionStmt *>(node)) {
    regions.push_back({&regions.back()});
    reg->decl->region = &regions.back();
  } else if (auto *assign = dynamic_cast<AssignStmt *>(node)) {
    assign->region = &regions.back();

    assign->decl->region = assign->region;
  } else if (auto *stmt = dynamic_cast<Stmt *>(node)) {
    stmt->region = &regions.back();
  }
}

void Territory::close(ASTNode *node) {
  if (dynamic_cast<FuncStmt *>(node) || dynamic_cast<RegionStmt *>(node)) {
    regions.pop_back();
  } else if (auto *assign = dynamic_cast<AssignExpr *>(node)) {
    Decl *ld = getDecl(assign->left, true);

    if (!outlives(assign->right->t, ld->region)) {
      throw runtime_error("Territory: RHS does not outlive " + ld->name.value);
    }
  }
}