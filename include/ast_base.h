#pragma once
#include "type.h"

class ASTNode {
public:
  virtual auto toString() -> std::string = 0;
  virtual auto walk() -> std::vector<ASTNode *> = 0;
  virtual ~ASTNode() = default;
};

class Expr : public ASTNode {
public:
  TypeThing *t = nullptr;
  ~Expr() override = default;
};