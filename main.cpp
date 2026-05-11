#include "ast.h"
#include "include/territory.h"
#include "lexer.h"
#include "parser.h"
#include "typechecker.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Linker/Linker.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>

using namespace llvm;

namespace fs = std::filesystem;
using namespace std;

string readFile(const fs::path& path)
{
    ifstream file(path);
    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void regionWalk(Territory& territory, ASTNode* node)
{
    territory.open(node);

    for (auto sub : node->walk()) {
        if (!sub) {
            continue;
        }
        regionWalk(territory, sub);
    }

    territory.close(node);
}

void printAST(ASTNode* node, const string& spacing = "")
{
    cout << spacing << node->toString() << '\n';
    auto s = spacing + "  ";
    for (auto sub : node->walk()) {
        if (!sub) {
            continue;
        }
        printAST(sub, s);
    }
}

vector<Lexer::Token> tokenize(const string& source)
{

    Lexer lexer;
    lexer.token("true|false", "BOOLEAN");
    lexer.token("(?:[0-9]+\\.[0-9]+)|(?:\\.[0-9]+)", "DECIMAL");
    lexer.token("[0-9]+", "INTEGER");
    lexer.token(R"((?:"[^"]*"))", "STRING");
    lexer.token("(?:'[^']')", "CHAR");
    lexer.token("null", "NULL");
    lexer.token("if|else|for|return|region|while|struct|import|public", "KEYWORD");
    lexer.token("void|bool|u8|u16|u32|u64|i8|i16|i32|i64|f32|f64", "PRIMITIVE");
    lexer.token("[a-zA-Z_][a-zA-Z0-9_]*", "IDENTIFIER");
    lexer.token("\\?", "QUESTION");
    lexer.token("=", "EQUAL");
    lexer.token("\\+\\+|--", "ASSIGN");
    lexer.token("\\+|-|/"
                "|\\*|@|&|^|\\||&&|\\|\\|==|\\!=|\\>|\\<|\\>=|\\<=|\\!",
        "OP");
    lexer.token(",", "COMMA");
    lexer.token("\\.", "DOT");
    lexer.token(":", "COLON");
    lexer.token(";", "SEMICOLON");
    lexer.token("\\(", "LPAREN");
    lexer.token("\\)", "RPAREN");
    lexer.token("\\{", "LBRACE");
    lexer.token("\\}", "RBRACE");
    lexer.token("\\[", "LBRACKET");
    lexer.token("\\]", "RBRACKET");
    lexer.token("\\<", "LCHEV");
    lexer.token("\\>", "RCHEV");

    return lexer.tokenize(source);
}

struct ProgramData {
    Program program;
    Symbols* symbols;
};

ProgramData passProgram(vector<Lexer::Token> tokens, Symbols* builtin_symbols)
{
    ProgramData d;

    Parser parser;

    if (builtin_symbols) {
        parser.symbols->join(builtin_symbols);
    }

    Program p = parser.buildAST(std::move(tokens));

    d.symbols = new Symbols(*parser.symbols);

    TypeChecker tc;

    tc.walk(&p);

    Territory territory;

    regionWalk(territory, &p);

    d.program = p;
    return d;
}

void generateObject(Module& mod)
{
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();

    Triple target_triple(sys::getDefaultTargetTriple());
    mod.setTargetTriple(target_triple);

    std::string err;
    auto target = TargetRegistry::lookupTarget(target_triple, err);

    TargetOptions opt;
    auto rm = std::optional<Reloc::Model>();
    auto target_machine = target->createTargetMachine(target_triple, "generic", "", opt, rm);

    mod.setDataLayout(target_machine->createDataLayout());

    std::error_code ec;
    raw_fd_ostream out("output.o", ec, sys::fs::OF_None);
    legacy::PassManager pm;
    target_machine->addPassesToEmitFile(pm, out, nullptr,
        CodeGenFileType::ObjectFile);
    pm.run(mod);
}

ProgramData compile(LLVMContext& ctx, Module& mod, std::string source, Symbols* builtin_symbols = nullptr)
{
    source = regex_replace(
        source, regex { "(?://[^\n]*)|(?:/\\*[^*]*\\*+(?:[^/*][^*]*\\*+)*/)" }, "");
    auto tokens = tokenize(source);

    auto pdata = passProgram(tokens, builtin_symbols);
    auto& program = pdata.program;

    IRBuilder<> builder(ctx);

    IRContext irc = IRContext { ctx, builder, mod };
    program.codegen(irc);

    mod.print(outs(), nullptr);
    if (verifyModule(mod, &errs())) {
        errs() << "Invalid module!\n";
    }

    return pdata;
}

int main()
{

    auto source = readFile(fs::path("main.to"));

    LLVMContext ctx;

    std::unique_ptr<Module> builtins = std::make_unique<Module>("BUILTINS", ctx);

    auto builtins_data = compile(ctx, *builtins, R"()");

    std::unique_ptr<Module>
        main = std::make_unique<Module>("main.to", ctx);

    Linker::linkModules(*main, std::move(builtins));

    compile(ctx, *main, source, builtins_data.symbols);

    generateObject(*main);

    return 0;
}