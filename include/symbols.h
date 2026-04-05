#pragma once

#include "ast.h"

namespace Symbols {
void walkAstOpen(ASTNode *node);
void walkAstClose(ASTNode *node);
} // namespace Symbols
