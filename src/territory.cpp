#include "territory.h"
#include "ast.h"
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

  if (auto *v = dynamic_cast<VariableExpr *>(expr)) {
    return v->decl;
  }

  if (auto *a = dynamic_cast<AttribExpr *>(expr)) {
    return getDecl(a->foo);
  }

  if (auto *i = dynamic_cast<IndexExpr *>(expr)) {
    return getDecl(i->arr);
  }

  if (required) {
    throw runtime_error("Territory: Unhandled expression.");
  }
  return nullptr;
}

void Territory::open(ASTNode *node) {
  if (auto *_ = dynamic_cast<FuncStmt *>(node)) {
    regions.push_back({&regions.back()});
  } else if (auto *_ = dynamic_cast<RegionStmt *>(node)) {
    regions.push_back({&regions.back()});
  } else if (auto *assign = dynamic_cast<AssignStmt *>(node)) {
    assign->region = &regions.back();

    assign->decl->region = assign->region;
  } else if (auto *stmt = dynamic_cast<Stmt *>(node)) {
    stmt->region = &regions.back();
  }
}

void Territory::close(ASTNode *node) {
  if (auto *_ = dynamic_cast<FuncStmt *>(node)) {
    regions.pop_back();
  } else if (auto *_ = dynamic_cast<RegionStmt *>(node)) {
    regions.pop_back();
  } else if (auto *assign = dynamic_cast<AssignExpr *>(node)) {
    Decl *ld = getDecl(assign->left, true);
    Decl *rd = getDecl(assign->right);
    if (ld && rd && !rd->region->outlives(ld->region)) {
      throw runtime_error("Territory: " + rd->name.value +
                          " does not outlive " + ld->name.value + ".");
    }
  }
}