#pragma once
#include "territory_ast.h"
#include "type.h"
#include <llvm/IR/Value.h>

struct IRContext;

struct IRValue
{
    const bool is_rv; // is rvalue
    llvm::Value* value;

    IRValue(llvm::Value* v, const bool is_rvalue = false) : is_rv(is_rvalue), value(v)
    {
    }

    operator llvm::Value*() const
    {
        return value;
    }
};

class ASTNode {
public:
    virtual auto toString() -> std::string = 0;
    virtual auto walk() -> std::vector<ASTNode*> = 0;
    virtual auto codegen(IRContext& ir) -> IRValue = 0;
    virtual ~ASTNode() = default;
};

class Expr : public ASTNode {
public:
    TypeThing* t = nullptr;
    TypeThing* actual_t = nullptr;
    Region* region;
    ~Expr() override = default;

    void setType(TypeThing *ty)
    {
        t = ty;
        actual_t = ty;
    }
};

class Stmt : public ASTNode {
public:
    Region* region = nullptr;

    ~Stmt() override = default;
};
