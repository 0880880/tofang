#pragma once
#include "ast.h"

struct AccessPath
{
    Decl* root;
    std::vector<std::string> fields;

    bool operator==(const AccessPath& other) const
    {
        return root == other.root && fields == other.fields;
    }
};

struct AccessPathHash
{
    std::size_t operator()(const AccessPath& path) const
    {
        std::size_t res = std::hash<Decl*>()(path.root);
        for (const auto& field : path.fields)
        {
            // Standard hash combining
            res ^= std::hash<std::string>()(field) + 0x9e3779b9 + (res << 6) + (res >> 2);
        }
        return res;
    }
};

struct RefinementEnv
{
    std::unordered_map<AccessPath, TypeThing*, AccessPathHash> map;

    TypeThing* lookup(const AccessPath& d);
};

class TypeChecker
{
private:
    FuncStmt* current_function = nullptr;
    RefinementEnv current_env = {};

    void open(ASTNode* node);
    void close(ASTNode* node);

    RefinementEnv checkIf(IfStmt* ifs);
    RefinementEnv checkBlock(BlockStmt* block);

public:
    void walk(ASTNode* node);
};