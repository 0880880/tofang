#pragma once

#include "ast.h"

class Symbols {
public:
  deque<unordered_map<string, Decl *>> declarations = {{}};

  void open(ASTNode *node);
  void close(ASTNode *node);
};
