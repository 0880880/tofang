#include "territory.h"
#include "ast.h"
#include <iostream>

using namespace std;

static FuncStmt* current_function = nullptr;

static void error(const string& message)
{
    std::cerr << message << "\n";
    exit(1);
}

void Territory::open(ASTNode* node)
{
    if (auto* fun = dynamic_cast<FuncStmt*>(node))
    {
        regions.push_back({&regions.back()});
        fun->decl->region = &regions.back();
        current_function = fun;
    }
    else if (auto* reg = dynamic_cast<RegionStmt*>(node))
    {
        regions.push_back({&regions.back()});
        reg->decl->region = &regions.back();
    }
    else if (auto* assign = dynamic_cast<AssignStmt*>(node))
    {
        assign->region = &regions.back();

        assign->decl->region = assign->region;
    }
    if (auto* stmt = dynamic_cast<Stmt*>(node))
    {
        stmt->region = &regions.back();
    }
    else if (auto* var = dynamic_cast<VariableExpr*>(node))
    {
        if (var->decl->func_param)
        {
            var->region = &regions.front();
        }
        else
        {
            var->region = &regions.back();
        }
    }
    else if (auto* expr = dynamic_cast<Expr*>(node))
    {
        expr->region = &regions.back();
    }
}

void Territory::close(ASTNode* node)
{
    if (dynamic_cast<FuncStmt*>(node))
    {
        regions.pop_back();
        current_function = nullptr;
    }
    else if (dynamic_cast<RegionStmt*>(node))
    {
        regions.pop_back();
    }
    else if (auto* ase = dynamic_cast<AssignExpr*>(node))
    {
        if (!ase->right->region->outlives(ase->left->region))
        {
            error("Territory: RHS does not outlive LHS");
        }
    }
    else if (auto* ret = dynamic_cast<ReturnStmt*>(node))
    {
        assert(current_function != nullptr);
        if (ret->value && ret->value->t->kind == TypeKind::POINTER // If not pointer -> copy
            && !ret->value->region->outlives(current_function->decl->region))
        {
            error("Territory: Cannot return data allocated inside function");
        }
    }
}