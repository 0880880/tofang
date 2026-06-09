#pragma once

#include "ast.h"
#include "ast_base.h"
#include "include/territory.h"
#include "lexer.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Linker/Linker.h"

class Symbols;

struct CompileResult
{
    llvm::Module* mod = nullptr;
    Symbols* symbols = nullptr;
};

class Compiler
{
private:
    struct ProgramData
    {
        Program program;
        Symbols* symbols;
    };

    llvm::LLVMContext ctx;

    CompileResult builtins;

    void regionWalk(Territory& territory, ASTNode* node);

    void restoreTypeWalk(ASTNode* node);

    void printAST(ASTNode* node, const string& spacing = "");

    static vector<Lexer::Token> tokenize(const string& source);

public:
    Compiler();

    void generateObject(llvm::Module& mod);

    CompileResult compile(std::string name, std::string source);
};
