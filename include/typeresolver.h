#pragma once
#include "parser.h"
#include "symbols.h"

namespace TypeResolver {
void resolveTypes(Symbols &sym, Parser &parser);
}