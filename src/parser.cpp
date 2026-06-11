#include "parser.h"
#include "ast.h"
#include "decl.h"
#include "type.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace std;

using Ptr = Parser::Ptr;
template <typename T>
using Named = Parser::Named<T>;

Decl* Symbols::search(const std::string& name, const bool fail)
{
    for (auto& [_, map] : std::views::reverse(declarations))
    {
        auto d = map.find(name);
        if (d != map.end())
        {
            return d->second;
        }
    }
    if (fail)
    {
        throw std::runtime_error("Unknown symbol: " + name);
    }
    return nullptr;
}

void Symbols::taken(const std::string& name)
{
    for (auto& [_, map] : std::views::reverse(declarations))
    {
        if (auto d = map.find(name); d != map.end())
        {
            throw runtime_error("Name taken: " + name);
        }
    }
}

Parser::Parser() { symbols = std::make_unique<Symbols>(); }

optional<TypeThing*> Parser::type(Ptr& p) const
{
    TypeThing* t;
    if (p.is("PRIMITIVE"))
    {
        if ((*p).value == "void")
        {
            t = type_void;
        }
        else if ((*p).value == "bool")
        {
            t = type_bool;
        }
        else if ((*p).value == "u8")
        {
            t = type_u8;
        }
        else if ((*p).value == "u16")
        {
            t = type_u16;
        }
        else if ((*p).value == "u32")
        {
            t = type_u32;
        }
        else if ((*p).value == "u64")
        {
            t = type_u64;
        }
        else if ((*p).value == "i8")
        {
            t = type_i8;
        }
        else if ((*p).value == "i16")
        {
            t = type_i16;
        }
        else if ((*p).value == "i32")
        {
            t = type_i32;
        }
        else if ((*p).value == "i64")
        {
            t = type_i64;
        }
        else if ((*p).value == "f32")
        {
            t = type_f32;
        }
        else if ((*p).value == "f64")
        {
            t = type_f64;
        }
    }
    else if (p.is("IDENTIFIER"))
    {
        Decl* decl = symbols->search((*p).value, false);
        if (decl != nullptr && decl->kind == DeclKind::STRUCT)
        {
            t = interner->getStruct((*p).value);
            symbols->unresolved_types.push_back(t);
        }
        else
        {
            return nullopt;
        }
    }
    else
    {
        return nullopt;
    }
    ++p;
    while (true)
    {
        if (p.is("OP") && p.isV("*"))
        {
            ++p;
            t = interner->getPointer(t);
        }
        else if (p.is("LBRACKET"))
        {
            ++p;
            if (p.is("RBRACKET"))
            {
                ++p;
                t = interner->getArray(t, -1);
            }
            else if (p.is("RANGE"))
            {
                ++p;
                t = interner->getSlice(t);
            }
            else
            {
                int size = std::stoi((*p).value);
                p.expect("INTEGER");
                p.expect("RBRACKET");
                if (size == 0)
                {
                    error("Array size cannot be zero.");
                }
                else if (size < 0)
                {
                    error("Array size cannot be negative.");
                }
                t = interner->getArray(t, size);
            }
        }
        else
        {
            break;
        }
    }
    if (p.is("QUESTION"))
    {
        ++p;
        t = interner->getNullable(t);
    }
    return t;
}

int precedence(const std::string& op)
{
    // if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=") {
    //   return 1;
    // }
    if (op == "||")
    {
        return 2;
    }
    if (op == "&&")
    {
        return 3;
    }
    if (op == "|")
    {
        return 4;
    }
    if (op == "^")
    {
        return 5;
    }
    if (op == "&")
    {
        return 6;
    }
    if (op == "==" || op == "!=")
    {
        return 7;
    }
    if (op == "<" || op == "<=" || op == ">" || op == ">=")
    {
        return 8;
    }
    if (op == "+" || op == "-")
    {
        return 9;
    }
    if (op == "*" || op == "/")
    {
        return 10;
    }
    return -1;
}

Named<Expr*> Parser::primary(Ptr& p)
{
    if (p.eof())
    {
        throw std::runtime_error("Unexpected EOF in primary()");
    }

    if (auto ty = type(p))
    {
        auto l = new LiteralExpr(LiteralExpr::Type::MetaType, *p);
        l->typeValue = *ty;
        return symbols->open(l);
    }

    if (p.is("IDENTIFIER"))
    {
        auto v = new VariableExpr(*p);
        ++p;
        return symbols->open(v);
    }

    if (p.is("NULL"))
    {
        auto l = new LiteralExpr(LiteralExpr::Type::Null, *p);
        ++p;
        return symbols->open(l);
    }
    if (p.is("INTEGER") || p.is("DECIMAL") || p.is("BOOLEAN") || p.is("CHAR") || p.is("STRING"))
    {
        auto l = new LiteralExpr(LiteralExpr::Type::Integer, *p);
        if (p.is("DECIMAL"))
        {
            l->type = LiteralExpr::Type::Decimal;
        }
        if (p.is("BOOLEAN"))
        {
            l->type = LiteralExpr::Type::Boolean;
        }
        if (p.is("CHAR"))
        {
            l->type = LiteralExpr::Type::Char;
        }
        if (p.is("STRING"))
        {
            l->type = LiteralExpr::Type::String;
            const auto init = new StructInitExpr();
            init->struct_type = interner->getStruct("String");
            symbols->unresolved_types.push_back(init->struct_type);
            init->names.emplace_back("IDENTIFIER", "data");
            init->names.emplace_back("IDENTIFIER", "len");
            init->values.push_back(l);
            init->values.push_back(new LiteralExpr(LiteralExpr::Type::Integer,
                                                   Lexer::Token{"INTEGER", to_string((*p).value.length())}));
            ++p;
            return symbols->open(init);
        }

        ++p;
        return symbols->open(l);
    }

    if (p.is("LPAREN"))
    {
        ++p;
        Expr* inner = expr(p).get();
        p.expect("RPAREN");
        return symbols->open(new GroupingExpr(inner));
    }

    throw runtime_error("Unexpected token in primary(): " + (*p).type + "  L" + to_string((*p).sourceLineStart));
}

Named<Expr*> Parser::postfix(Ptr& p)
{
    Expr* left = primary(p).get();
    while (!p.eof())
    {
        if (p.is("DOT"))
        {
            ++p;
            auto bar = *p;
            ++p;
            left = new AttribExpr(left, bar);
        }
        else if (p.is("LBRACKET"))
        {
            ++p;
            if (p.is("RANGE"))
            {
                ++p;
                if (p.is("RBRACKET"))
                {
                    ++p;
                    auto* slc = new SliceExpr(left, std::nullopt, std::nullopt);
                    left = slc;
                }
                else
                {
                    auto* to = expr(p).get();
                    auto* slc = new SliceExpr(left, std::nullopt, to);
                    left = slc;
                }
            }
            else
            {
                // TODO this is bad/unreadable but I can fix it later
                auto* from = expr(p).get();
                if (p.is("RANGE"))
                {
                    ++p;
                    if (p.is("RBRACKET"))
                    {
                        ++p;
                        auto* slc = new SliceExpr(left, from, std::nullopt);
                        left = slc;
                    }
                    else
                    {
                        auto* to = expr(p).get();
                        auto* slc = new SliceExpr(left, from, to);
                        left = slc;
                    }
                }
                auto* ind = new IndexExpr(left, from);
                left = ind;
            }
            p.expect("RBRACKET");
        }
        else if (p.isV("<"))
        {
            ++p;
            auto c = new CallExpr(left);
            bool start = true;
            while (start || p.is("COMMA"))
            {
                if (p.isV(">"))
                {
                    break;
                }
                if (!start)
                {
                    p.expect("COMMA");
                }
                start = false;
                auto l = new LiteralExpr(LiteralExpr::Type::MetaType, *p);
                auto t = type(p);
                assert(t);
                l->typeValue = *t;
                c->typeArgs.push_back(l);
            }
            ++p;
            p.expect("LPAREN");
            start = true;
            while (start || p.is("COMMA"))
            {
                if (p.is("RPAREN"))
                {
                    break;
                }
                if (!start)
                {
                    p.expect("COMMA");
                }
                start = false;
                c->args.push_back(expr(p).get());
            }
            ++p;
            left = c;
        }
        else if (p.is("LPAREN"))
        {
            bool start = true;
            auto c = new CallExpr(left);
            ++p;
            while (start || p.is("COMMA"))
            {
                if (p.is("RPAREN"))
                {
                    break;
                }
                if (!start)
                {
                    p.expect("COMMA");
                }
                start = false;
                c->args.push_back(expr(p).get());
            }
            ++p;
            left = c;
        }
        else if (p.is("LBRACE"))
        {
            if (auto lit = dynamic_cast<LiteralExpr*>(left))
            {
                if (lit->type != LiteralExpr::Type::MetaType)
                {
                    throw runtime_error("Initializer LHS must be a type.");
                }
                else if (lit->typeValue->kind == TypeKind::STRUCT)
                {
                    auto in = new StructInitExpr();
                    in->struct_type = lit->typeValue;
                    bool start = true;
                    ++p;
                    while (start || p.is("COMMA"))
                    {
                        if (p.is("RBRACE"))
                        {
                            break;
                        }
                        if (!start)
                        {
                            p.expect("COMMA");
                        }
                        start = false;
                        in->names.push_back(*p);
                        p.expect("IDENTIFIER");
                        p.expect("EQUAL");
                        in->values.push_back(expr(p).get());
                    }
                    ++p;
                    left = in;
                }
                else if (lit->typeValue->kind == TypeKind::ARRAY)
                {
                    auto* arr = new ArrayExpr();
                    arr->arr_type = lit->typeValue;
                    ++p;
                    bool start = true;
                    while (start || p.is("COMMA"))
                    {
                        if (p.is("RBRACE"))
                        {
                            break;
                        }
                        if (!start)
                        {
                            p.expect("COMMA");
                        }
                        start = false;
                        arr->elements.push_back(expr(p).get());
                    }
                    ++p;
                    left = arr;
                }
                else
                {
                    throw runtime_error("Invalid initializer: Only array and struct initializers are allowed.");
                }
            }
            else
            {
                throw runtime_error("Initializer LHS must be a type.");
            }
        }
        else
        {
            break;
        }
    }

    return symbols->open(left);
}

Named<Expr*> Parser::prefix(Ptr& p)
{
    if (p.eof())
    {
        throw std::runtime_error("Unexpected EOF in prefix()");
    }

    if (p.is("OP"))
    {
        Lexer::Token op = *p;
        if (p.expectV("*", "-", "+", "!", "&"))
        {
            return symbols->open(new UnaryExpr(op, postfix(p).get()));
        }
        error("Expected unary operator for unary expression");
    }

    return postfix(p);
}

Named<Expr*> Parser::binExpr(Ptr& p, int minPrec)
{
    Expr* left = prefix(p).get();

    while (!p.eof() && p.is("OP"))
    {
        Lexer::Token op = *p;
        const int prec = precedence(op.value);
        if (prec < minPrec)
        {
            break;
        }

        ++p;

        Expr* right = expr(p, prec + 1).get();
        left = new BinaryExpr(left, op, right);
    }

    return symbols->open(left);
}

Named<Expr*> Parser::expr(Ptr& p, int minPrec)
{
    Expr* left = binExpr(p, minPrec).get();

    if (!p.eof() && p.is("EQUAL"))
    {
        Lexer::Token op = *p;
        ++p;
        Expr* right = expr(p, 0).get();
        return symbols->open(new AssignExpr(left, op, right));
    }

    return symbols->open(left);
}

static inline Visibility getVisibility(std::optional<Visibility> opt)
{
    return opt.value_or(Visibility::PROTECTED);
}

Named<Stmt*> Parser::func(Ptr& p, TypeThing* t, const Lexer::Token& name, Visibility visibility, bool is_extern)
{
    if (p.isV("<"))
    {
        error("Extern function cannot be generic.");
        ++p;
        auto* fn = new FuncStmt(t, name, getVisibility(visibility));
        fn->generic = true;
        bool start = true;
        while (start || p.is("COMMA"))
        {
            if (p.isV(">"))
            {
                break;
            }
            if (!start)
            {
                p.expect("COMMA");
            }
            start = false;
            auto v = *p;
            p.expect("IDENTIFIER");
            fn->genericParams.push_back(interner->getTypeVar(v.value));
        }
        ++p;
        p.expect("LPAREN");
        start = true;
        while (start || p.is("COMMA"))
        {
            if (p.is("RPAREN"))
            {
                break;
            }
            if (!start)
            {
                p.expect("COMMA");
            }
            start = false;
            if (auto ty = type(p))
            {
                fn->paramTypes.push_back(*ty);
                fn->paramNames.push_back(*p);
                p.expect("IDENTIFIER");
            }
            else
            {
                error("Expected type in function arguments");
            }
        }
        p.expect("RPAREN");
        p.expect("LBRACE");

        auto named = symbols->open(fn);
        symbols->open(&fn->body);

        while (!p.eof() && !p.is("RBRACE"))
        {
            fn->body.statements.push_back(statement(p).get());
        }

        symbols->close(&fn->body);

        p.expect("RBRACE");
        return named;
    }

    if (p.is("LPAREN"))
    {
        ++p;
        auto* fn = new FuncStmt(t, name, getVisibility(visibility));
        fn->is_extern = is_extern;

        bool start = true;
        while (start || p.is("COMMA"))
        {
            if (p.is("RPAREN"))
            {
                break;
            }
            if (!start)
            {
                p.expect("COMMA");
            }
            start = false;
            if (auto ty = type(p))
            {
                fn->paramTypes.push_back(*ty);
                fn->paramNames.push_back(*p);
                p.expect("IDENTIFIER");
            }
            else
            {
                error("Expected type in function arguments");
            }
        }
        p.expect("RPAREN");
        auto named = symbols->open(fn);
        if (is_extern && p.is("LBRACE"))
        {
            error("Extern function cannot have a body");
        }
        else if (!is_extern)
        {
            p.expect("LBRACE");

            symbols->open(&fn->body);

            while (!p.eof() && !p.is("RBRACE"))
            {
                fn->body.statements.push_back(statement(p).get());
            }

            symbols->close(&fn->body);

            p.expect("RBRACE");
        }
        return named;
    }

    error("Invalid function");
    return Named<FuncStmt*>(nullptr); // unreachable
}

struct TVResult
{
    std::optional<TypeThing*> type;
    std::optional<Visibility> visibility;

    inline explicit operator bool() const noexcept
    {
        return type || visibility;
    }
};

template <typename F>
static TVResult
typeOrVisibility(Ptr& p, F&& type)
{
    if (p.is("KEYWORD") && p.isV("public"))
    {
        ++p;
        return {.type = std::nullopt, .visibility = std::make_optional(Visibility::PUBLIC)};
    }
    return {type(p), std::nullopt};
}

Named<Stmt*> Parser::statement(Ptr& p)
{
    if (p.eof())
    {
        throw std::runtime_error("Unexpected EOF in statement()");
    }

    std::optional<Visibility> visibility = std::nullopt;

    if (p.is("KEYWORD") && (*p).value == "public")
    {
        visibility = std::make_optional(Visibility::PUBLIC);
        ++p;
    }

    if (!visibility.has_value() && p.is("KEYWORD") && (*p).value == "return")
    {
        ++p;
        auto r = new ReturnStmt();
        if (!p.is("SEMICOLON"))
        {
            r->value = expr(p).get();
        }
        p.expect("SEMICOLON");
        return symbols->open(r);
    }

    if (p.is("KEYWORD") && (*p).value == "struct")
    {
        ++p;
        Lexer::Token name = *p;
        p.expect("IDENTIFIER");
        p.expect("LBRACE");
        auto* str = new StructStmt(name, getVisibility(visibility));
        std::optional<Parser::Named<Stmt*>> str_named = std::nullopt;
        symbols->pushDeferScope();
        bool header = true;
        while (!p.eof() && !p.is("RBRACE"))
        {
            if (auto tv = typeOrVisibility(p, [this](Ptr& p)
            {
                return this->type(p);
            }))
            {
                if (tv.visibility)
                {
                    if (auto typ = type(p))
                    {
                        tv.type = typ;
                    }
                    else
                    {
                        error("Not allowed !!!");
                    }
                }
                auto t = *tv.type;
                auto fn = *p;
                p.expect("IDENTIFIER");
                if (p.is("LPAREN") || p.isV("<"))
                {
                    if (!str_named)
                    {
                        str_named = symbols->open(str);
                    }
                    header = false;
                    str->functions.push_back(symbols->close(func(p, t, fn, getVisibility(tv.visibility)).get()).get());
                }
                else
                {
                    // TODO Struct member visibility
                    if (!header)
                    {
                        error("Cannot mix variables and functions order in struct");
                    }
                    str->types.push_back(t);
                    str->names.push_back(fn);
                    if (p.is("EQUAL"))
                    {
                        ++p;
                        str->definitions.push_back(expr(p).get());
                    }
                    else
                    {
                        str->definitions.push_back(nullptr);
                    }
                    p.expect("SEMICOLON");
                }
            }
            else
            {
                delete str;
                throw runtime_error("Expected type inside struct " + name.value);
            }
        }
        if (!str_named)
        {
            str_named = symbols->open(str);
        }
        symbols->flushDefers();
        symbols->popDeferScope();
        p.expect("RBRACE");
        return *str_named;
    }

    if (!visibility.has_value() && p.is("KEYWORD") && (*p).value == "if")
    {
        ++p;
        auto* ifs = new IfStmt();
        p.expect("LPAREN");
        Expr* condition = expr(p).get();
        ifs->condition = condition;
        p.expect("RPAREN");
        p.expect("LBRACE");

        auto named = symbols->open(ifs);
        symbols->open(&ifs->body);

        while (!p.eof() && !p.is("RBRACE"))
        {
            ifs->body.statements.push_back(statement(p).get());
        }

        symbols->close(&ifs->body);

        p.expect("RBRACE");

        IfStmt* cur = ifs;

        if (p.isV("else"))
        {
            ++p;
            bool has_else = true;
            while (p.isV("if"))
            {
                ++p;
                cur->elseIf = new IfStmt();
                cur = cur->elseIf;
                p.expect("LPAREN");
                cur->condition = expr(p).get();
                p.expect("RPAREN");
                p.expect("LBRACE");

                symbols->open(cur);
                symbols->open(&cur->body);

                while (!p.eof() && !p.is("RBRACE"))
                {
                    cur->body.statements.push_back(statement(p).get());
                }

                symbols->close(&cur->body);

                p.expect("RBRACE");
                if (p.isV("else"))
                {
                    ++p;
                }
                else
                {
                    has_else = false;
                    break;
                }
            }
            if (has_else)
            {
                p.expect("LBRACE");
                cur->elseStmt = new ElseStmt();

                symbols->open(cur);
                symbols->open(&cur->elseStmt->body);

                while (!p.eof() && !p.is("RBRACE"))
                {
                    cur->elseStmt->body.statements.push_back(statement(p).get());
                }

                symbols->close(&cur->elseStmt->body);

                p.expect("RBRACE");
            }
        }

        return named;
    }

    if (!visibility.has_value() && p.is("KEYWORD") && (*p).value == "region")
    {
        ++p;
        Lexer::Token name = *p;
        p.expect("IDENTIFIER");
        auto r = new RegionStmt(name);
        p.expect("LBRACE");

        auto named = symbols->open(r);
        symbols->open(&r->body);

        while (!p.eof() && !p.is("RBRACE"))
        {
            r->body.statements.push_back(statement(p).get());
        }

        symbols->close(&r->body);

        p.expect("RBRACE");
        return named;
    }

    if (!visibility.has_value() && p.is("KEYWORD") && (*p).value == "for")
    {
        ++p;
        p.expect("LPAREN");
        Stmt* init = nullptr;
        Expr* condition = nullptr;
        Expr* update = nullptr;
        if (!p.is("SEMICOLON"))
        {
            init = statement(p).get();
        }
        p.expect("SEMICOLON");
        if (!p.is("SEMICOLON"))
        {
            condition = expr(p).get();
        }
        p.expect("SEMICOLON");
        if (!p.is("RPAREN"))
        {
            update = expr(p).get();
        }
        p.expect("RPAREN");
        auto* f = new ForStmt();
        f->init = init;
        f->condition = condition;
        f->update = update;
        p.expect("LBRACE");

        auto named = symbols->open(f);
        symbols->open(&f->body);

        while (!p.eof() && !p.is("RBRACE"))
        {
            f->body.statements.push_back(statement(p).get());
        }

        symbols->close(&f->body);

        p.expect("RBRACE");
        return named;
    }

    if (!visibility.has_value() && p.is("KEYWORD") && (*p).value == "import")
    {
        ++p;
        bool start = true;
        auto* imp = new ImportStmt();
        while (start || p.is("DOT"))
        {
            if (p.is("SEMICOLON"))
            {
                break;
            }
            if (!start)
            {
                p.expect("DOT");
            }
            start = false;
            imp->path.push_back((*p).value);
            p.expect("IDENTIFIER");
        }
        p.expect("SEMICOLON");
        return symbols->open(imp);
    }

    p.save();
    bool is_extern = false;
    if (p.is("KEYWORD") && p.isV("extern"))
    {
        is_extern = true;
        ++p;
    }
    if (auto ty = type(p))
    {
        auto t = *ty;

        if (p.is("IDENTIFIER"))
        {
            Lexer::Token name = *p;
            ++p;

            if (p.is("EQUAL"))
            {
                ++p;
                if (is_extern)
                {
                    error("Variable cannot be extern");
                }
                Expr* rhs = expr(p).get();
                p.expect("SEMICOLON");
                return symbols->open(new AssignStmt(t, name, rhs, getVisibility(visibility)));
            }

            return func(p, t, name, getVisibility(visibility), is_extern);
        }
    }
    p.restore();
    Expr* e = expr(p).get();
    p.expect("SEMICOLON");
    return symbols->open(new ExprStmt(e));

    throw std::runtime_error("Unexpected token in statement(): " + (*p).type);
}

static string readFile(const fs::path& path)
{
    ifstream file(path);
    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

Program Parser::buildAST(Tokens& tokens, Compiler* compiler, llvm::Module& module)
{
    Program prog;

    auto it = tokens.begin();
    Ptr ptr(it, tokens.end());

    while (it != tokens.end())
    {
        Stmt* stmt = symbols->close(statement(ptr).get()).get();
        symbols->resolveTypes();
        if (auto* import = dynamic_cast<ImportStmt*>(stmt))
        {
            fs::path path = std::filesystem::current_path();
            std::string path_display;
            for (auto& r : import->path)
            {
                if (r != *import->path.begin())
                {
                    path_display.append(".");
                }
                path_display.append(r);
            }
            for (auto& r : import->path)
            {
                path /= r;
                if (!fs::exists(path))
                {
                    error("Import error: cannot find module " + path_display);
                }
            }
            std::string source = readFile(path);
            auto res = compiler->compile(import->path[import->path.size() - 1], source);
            symbols->join(res.symbols);
            llvm::Linker::linkModules(module, std::move(llvm::CloneModule(*res.mod)));
        }
        prog.statements.push_back(stmt);
    }
    symbols->flushDefers();

    return prog;
}

void Symbols::resolveTypes(bool /*final*/)
{
    while (!unresolved_types.empty())
    {
        TypeThing* t = unresolved_types.back();
        unresolved_types.pop_back();
        if (t->kind == TypeKind::STRUCT)
        {
            auto& [name, decl] = std::get<StructType>(t->data);
            Decl* resolved_decl = search(name, true);
            if (resolved_decl->kind != DeclKind::STRUCT)
            {
                throw std::runtime_error(name + " is not a struct.");
            }
            decl = resolved_decl;
        }
    }
}
