#include "decl.h"
#include "type.h"
#include <algorithm>

using namespace std;

deque<unordered_map<string, Decl *>> declarations = {{}};

TypeThing *Decl::toType() {
  switch (kind) {
  case DeclKind::VAR: {
    return std::get<VarDecl>(data).type;
  }
  case DeclKind::FUNC: {
    FuncDecl f = std::get<FuncDecl>(data);
    std::vector<TypeThing *> p(f.params.size());
    std::ranges::transform(f.params, p.begin(),
                           [](Decl *x) { return x->toType(); });
    return interner->getFunction(p, f.returnType);
  }
  case DeclKind::GENERIC_FUNC: {
    FuncDecl f = std::get<FuncDecl>(data);
    std::vector<TypeThing *> p(f.params.size());
    std::ranges::transform(f.params, p.begin(),
                           [](Decl *x) { return x->toType(); });
    return interner->getGenericFunction(f.genericParams, p, f.returnType);
  }
  case DeclKind::REGION: {
    return interner->getRegion(this);
  }
  case DeclKind::STRUCT: {
    return interner->getStruct(this);
  }
  }
}