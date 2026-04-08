#pragma once

#include "ast_base.h"
#include "parser.h"
#include <deque>
#include <string>
#include <unordered_map>

class Symbols {
public:
  std::deque<std::unordered_map<std::string, Decl *>> declarations = {{}};
  Parser &parser;

  Symbols(Parser &parser) : parser(parser) {}

  void walk(ASTNode *node);

private:
  void open(ASTNode *node);
  void close(ASTNode *node);
  Decl *search(const std::string &name, bool fail = false);

  void resolveTypes(bool final = false);
  void realWalk(ASTNode *node);
};
