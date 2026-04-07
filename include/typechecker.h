#pragma once
#include "ast.h"

struct RefinementEnv {
  RefinementEnv *parent;
  std::unordered_map<Decl *, TypeThing *> map;

  TypeThing *lookup(Decl *d);
};

class TypeChecker {
private:
  FuncStmt *current_function = nullptr;
  RefinementEnv current_env = {};

  void open(ASTNode *node);
  void close(ASTNode *node);

  void checkIf(IfStmt *ifs);
  RefinementEnv checkBlock(BlockStmt *block);

public:
  void walk(ASTNode *node);
};