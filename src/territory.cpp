#include "territory.h"
#include "ast.h"

using namespace std;

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
  }
}