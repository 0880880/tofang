#pragma once
#include "ast.h"

namespace TypeChecker {

void open(ASTNode *node);

void close(ASTNode *node);
} // namespace TypeChecker