#pragma once

#include "ast_base.h"
#include <deque>
#include <string>
#include <unordered_map>


class Symbols {
public:
  std::deque<std::unordered_map<std::string, Decl *>> declarations = {{}};

  void open(ASTNode *node);
  void close(ASTNode *node);
};
