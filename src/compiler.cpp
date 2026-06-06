#include "compiler.h"
#include "parser.h"
#include "typechecker.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <iostream>
#include <regex>

using namespace llvm;

Compiler::Compiler()
{
    builtins = compile("BUILTINS",
        R"(public struct String {
u8 *?data = null;
u64 len = 0;
}
)");
}

void Compiler::regionWalk(Territory& territory, ASTNode* node)
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

void Compiler::restoreTypeWalk(ASTNode* node)
{
    if (auto *expr = dynamic_cast<Expr *>(node))
    {
        expr->t = expr->actual_t;
    }

    for (auto sub : node->walk()) {
        if (!sub) {
            continue;
        }
        restoreTypeWalk(sub);
    }
}

void Compiler::printAST(ASTNode* node, const string& spacing)
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

vector<Lexer::Token> Compiler::tokenize(const string& source)
{

    Lexer lexer;
    lexer.token("\\b(?:true|false)\\b", "BOOLEAN");
    lexer.token("(?:[0-9]+\\.[0-9]+)|(?:\\.[0-9]+)", "DECIMAL");
    lexer.token("[0-9]+", "INTEGER");
    lexer.token(R"("(?:[^"\\]|\\.)*")", "STRING");
    lexer.token(R"('(?:[^'\\]|\\.)')", "CHAR");
    lexer.token("\\bnull\\b", "NULL");
    lexer.token("\\b(?:if|else|for|return|region|while|struct|import|public|extern)\\b", "KEYWORD");
    lexer.token("\\b(?:void|bool|u8|u16|u32|u64|i8|i16|i32|i64|f32|f64)\\b", "PRIMITIVE");
    lexer.token("[a-zA-Z_][a-zA-Z0-9_]*", "IDENTIFIER");
    lexer.token("\\?", "QUESTION");
    lexer.token(R"(\+\+|--|\|\||&&|==|!=|>=|<=|\+|-|/|\*|@|&|\^|\||>|<|!)", "OP");
    lexer.token("=", "EQUAL");
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

    auto tokens = lexer.tokenize(source);

    for (auto &token : tokens)
    {
        if (token.type == "STRING" || token.type == "CHAR")
        {
            std::string sb;
            sb.reserve(token.value.length());
            for (size_t i = 1; i < token.value.length()-1; ++i)
            {
                const char ch = token.value[i];
                if (ch == '\\')
                {

                    ++i;
                    if (i >= token.value.length()-1)
                    {
                        throw std::runtime_error("Trailing backslash at end of string");
                    }

                    switch (const char control_char = token.value[i]) {
                    case '0':  sb += '\0'; break; // Null
                    case 'a':  sb += '\a'; break; // Bell
                    case 'b':  sb += '\b'; break; // Backspace
                    case 'f':  sb += '\f'; break; // Form Feed
                    case 'n':  sb += '\n'; break; // Newline
                    case 'r':  sb += '\r'; break; // Carriage Return
                    case 't':  sb += '\t'; break; // Tab
                    case 'v':  sb += '\v'; break; // Vertical Tab
                    case '\\': sb += '\\'; break; // Backslash
                    case '"':  sb += '"';  break; // Double Quote
                    case '\'': sb += '\''; break; // Single Quote

                        // case 'x':
                        //     break;
                        // case 'u':
                        //     break;

                    default:
                        throw std::runtime_error("Unknown escape sequence \\" + std::string(1,control_char));
                    }
                } else
                {
                    sb += ch;
                }
            }
            token.value = sb;
        }
    }

    return tokens;
}

void Compiler::generateObject(Module& mod)
{
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();

    Triple target_triple(sys::getDefaultTargetTriple());
    mod.setTargetTriple(target_triple);

    std::string err;
    auto target = TargetRegistry::lookupTarget(target_triple, err);

    TargetOptions opt;
    auto rm = std::optional<Reloc::Model>(Reloc::PIC_);
    auto target_machine = target->createTargetMachine(target_triple, "generic", "", opt, rm);

    mod.setDataLayout(target_machine->createDataLayout());

    std::error_code ec;
    raw_fd_ostream out("output.o", ec, sys::fs::OF_None);
    legacy::PassManager pm;
    target_machine->addPassesToEmitFile(pm, out, nullptr,
        CodeGenFileType::ObjectFile);
    pm.run(mod);
}

CompileResult Compiler::compile(std::string name, std::string source)
{
    Module* mod = new Module(name, ctx);

    source = regex_replace(
        source, regex { "(?://[^\n]*)|(?:/\\*[^*]*\\*+(?:[^/*][^*]*\\*+)*/)" }, "");
    auto tokens = tokenize(source);

    if (builtins.mod != nullptr) {
        Linker::linkModules(*mod, std::move(CloneModule(*builtins.mod)));
    }

    Compiler::ProgramData pdata;

    Parser parser;

    if (builtins.symbols != nullptr) {
        parser.symbols->join(builtins.symbols);
    }

    Program program = parser.buildAST(tokens, this, *mod);
    printAST(&program);

    pdata.symbols = new Symbols(*parser.symbols);

    TypeChecker tc;

    tc.walk(&program);

    Territory territory;

    regionWalk(territory, &program);

    restoreTypeWalk(&program);

    IRBuilder<> builder(ctx);

    IRContext irc = IRContext { ctx, builder, *mod };
    program.codegen(irc);

    mod->print(outs(), nullptr);
    if (verifyModule(*mod, &errs())) {
        errs() << "Invalid module!\n";
    }

    return { std::move(mod), pdata.symbols };
}