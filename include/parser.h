#pragma once
#include "ast.h"
#include "lexer.h"
#include <vector>

using namespace std;

namespace Parser {
Program buildAST(vector<Lexer::Token> tokens);
} // namespace Parser