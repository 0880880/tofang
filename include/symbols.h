#pragma once

#include "ast.h"

class Symbols {
private:
  deque<unordered_map<string, Decl *>> declarations = {{}};

public:
  void open(ASTNode *node);
  void close(ASTNode *node);
};
