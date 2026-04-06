#include "symbols.h"
#include "ast.h"
#include "decl.h"
#include <iostream>
#include <ranges>

using namespace std;

static void error(const string &msg) {
  cerr << msg << '\n';
  exit(1);
}

void Symbols::walkAstOpen(ASTNode *node) {
  if (auto f = dynamic_cast<FuncStmt *>(node)) {
    auto &scope = declarations.back();
    declarations.emplace_back();
    FuncDecl fd = {};
    for (auto generic_param : f->genericParams) {
      fd.genericParams.push_back(generic_param);
    }
    fd.returnType = f->returnType;

    for (size_t i = 0; i < f->paramNames.size(); ++i) {
      auto name = f->paramNames[i];
      auto decl = new Decl{.kind = DeclKind::VAR,
                           .name = name,
                           .data = VarDecl{f->paramTypes[i]}};
      declarations.back()[name.value] = decl;
      fd.params.push_back(decl);
    }
    f->decl =
        new Decl{.kind = f->generic ? DeclKind::GENERIC_FUNC : DeclKind::FUNC,
                 .name = f->name,
                 .data = fd};
    scope[f->name.value] = f->decl;
  } else if (auto a = dynamic_cast<AssignStmt *>(node)) {
    a->decl = new Decl{
        .kind = DeclKind::VAR, .name = a->name, .data = VarDecl{a->type}};
    declarations.back()[a->name.value] = a->decl;
  } else if (auto r = dynamic_cast<RegionStmt *>(node)) {
    r->decl = new Decl{
        .kind = DeclKind::REGION, .name = r->name, .data = RegionDecl{r->id}};
    declarations.back()[r->name.value] = r->decl;
  } else if (dynamic_cast<BlockStmt *>(node)) {
    declarations.emplace_back();
  } else if (auto v = dynamic_cast<VariableExpr *>(node)) {
    bool found = false;
    for (auto m : std::views::reverse(declarations)) {
      auto d = m.find(v->name.value);
      if (d != m.end()) {
        found = true;
        v->decl = d->second;
        break;
      }
    }
    if (!found) {
      error("Unknown name \"" + v->name.value + "\"");
    }
  }
}

void Symbols::walkAstClose(ASTNode *node) {
  if (dynamic_cast<FuncStmt *>(node) || dynamic_cast<BlockStmt *>(node)) {
    declarations.pop_back();
  }
}