#pragma once

#include "ast.h"
#include "include/territory.h"
#include "lexer.h"
#include "parser.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Linker/Linker.h"

struct CompileResult {
    llvm::Module* mod;
    Symbols* symbols;
};

class Compiler {
private:
    struct ProgramData {
        Program program;
        Symbols* symbols;
    };

    llvm::LLVMContext ctx;

    CompileResult builtins;

    void regionWalk(Territory& territory, ASTNode* node);

    void printAST(ASTNode* node, const string& spacing = "");

    vector<Lexer::Token> tokenize(const string& source);

    ProgramData createProgram(vector<Lexer::Token> tokens, Symbols* builtin_symbols);

public:
    Compiler()
    {
        builtins = compile("BUILTINS", R"()");
    }

    void generateObject(llvm::Module& mod);

    CompileResult compile(std::string name, std::string source);
};