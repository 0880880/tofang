#pragma once

#include "ast.h"
#include "territory_ast.h"
#include <deque>

class Territory {
private:
  std::deque<Region> regions{
      Region{} // Global
  };

public:
  void open(ASTNode *node);
  void close(ASTNode *node);
};