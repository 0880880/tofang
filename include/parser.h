#pragma once
#include "ast.h"
#include "decl.h"
#include "lexer.h"
#include "type.h"
#include <cassert>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

using namespace std;

class Symbols;

static inline void error(const string& msg)
{
    std::cerr << msg << '\n';
    std::cout.flush();
    std::cerr.flush();
    exit(1);
}

template <typename T>
concept ASTNodePtr = std::is_pointer_v<T> && std::derived_from<std::remove_pointer_t<T>, ASTNode>;

class Parser {

public:
    std::vector<std::pair<Lexer::Token, TypeThing*>> types;

    Parser();

    template <typename T>
    class Named {
    public:
        explicit Named(T n)
            : node(n)
        {
        }

        template <typename U>
        Named(const Named<U>& other)
            requires std::is_convertible_v<U, T>
            : node(other.get())
        {
        }

        T get() const { return node; }

        T operator->() const { return node; }
        auto& operator*() const { return *node; }

        explicit operator T() const { return node; }

    private:
        T node;
    };

    using Tokens = std::vector<Lexer::Token>;
    using Iter = Tokens::iterator;
    class Ptr {
        Iter& it;
        Iter end;

    public:
        Ptr(Iter& it, Iter end)
            : it(it)
            , end(end)
        {
        }

        [[nodiscard]] bool eof() const { return it == end; }

        Lexer::Token& operator*()
        {
            if (it == end) {
                throw std::runtime_error("Unexpected EOF");
            }
            return *it;
        }

        Ptr& operator++()
        {
            if (it != end) {
                ++it;
            }
            return *this;
        }

        Ptr& operator--()
        {
            if (it != end) { // FIXME check if it != begin
                --it;
            }
            return *this;
        }

        bool is(const std::string& type) { return it != end && it->type == type; }
        bool isV(const std::string& value)
        {
            return it != end && it->value == value;
        }

        bool expect(const std::string& type)
        {
            if (!is(type)) {
                if (it != end) {
                    throw std::runtime_error("Expected token: " + type + ", got " + it->type + " L" + std::to_string(it->sourceLineStart) + " .");
                } else {
                    throw std::runtime_error("Expected token: " + type + ", got EOF.");
                }
            }
            ++it;
            return true;
        }

        template <typename... Ts>
        bool expectV(const Ts&... expected)
        {
            // Check each expected value
            bool matched = ((isV(expected)) || ...);

            if (!matched) {
                if (it != end) {
                    std::string expected_list = ((std::string(expected) + ", ") + ...);
                    expected_list.resize(expected_list.size() - 2); // strip final comma

                    throw std::runtime_error("Expected one of: [" + expected_list + "], got " + it->value + ".");
                } else {
                    throw std::runtime_error("Expected one of provided tokens, got EOF.");
                }
            }

            ++it;
            return true;
        }

        [[nodiscard]] std::string str() const
        {
            return it->type + ":" + it->value + "@L" + std::to_string(it->sourceLineStart);
        }
    };

    Program buildAST(vector<Lexer::Token> tokens);

private:
    std::unique_ptr<Symbols> symbols;

    std::optional<TypeThing*> type(Ptr& p);
    Named<Expr*> primary(Ptr& p);
    Named<Expr*> postfix(Ptr& p);
    Named<Expr*> prefix(Ptr& p);
    Named<Expr*> binExpr(Ptr& p, int minPrec = 0);
    Named<Expr*> expr(Ptr& p, int minPrec = 0);
    Named<Stmt*> func(Ptr& p, TypeThing* t, Lexer::Token name, Visibility visibility);
    Named<Stmt*> statement(Ptr& p);

    Decl* search(const std::string& name, bool fail = false);
};

class Symbols {
private:
    struct CallArg {
        bool close;
        std::variant<Stmt*, Expr*> node;
    };
    struct FuncDefer {
        FuncStmt* func = nullptr;
        std::deque<CallArg> args = {};
    };

    Decl* current_struct = nullptr;
    Decl* current_func = nullptr;
    int defer_depth = 0;

    void resolveTypes(bool final = false);

    std::deque<std::deque<FuncDefer>> defers = { {} };

    struct Scope {
        const Decl* decl = nullptr;
        std::unordered_map<std::string, Decl*> declarations;

        explicit Scope(const Decl* decl)
            : decl(decl)
        {
        }
    };

public:
    std::deque<Scope>
        declarations
        = { Scope { nullptr } };

    void taken(const std::string& name);

    void pushDeferScope()
    {
        defers.push_back({});
    }

    void popDeferScope()
    {
        defers.pop_back();
    }

    void flushDefers()
    {
        for (auto& def : defers.back()) {
            declarations.emplace_back();
            FuncStmt* f = def.func;
            FuncDecl fd = std::get<FuncDecl>(f->decl->data);
            for (size_t i = 0; i < fd.params.size(); ++i) {
                declarations.back().declarations[fd.params[i]->name.value] = fd.params[i];
            }
            for (auto& ca : def.args) {
                auto& arg = ca.node;
                if (std::holds_alternative<Stmt*>(arg)) {
                    if (ca.close) {
                        close(std::get<Stmt*>(arg));
                    } else {
                        open(std::get<Stmt*>(arg));
                    }
                } else if (std::holds_alternative<Expr*>(arg)) {
                    open(std::get<Expr*>(arg));
                }
            }
            declarations.pop_back();
        }
    }

    Parser::Named<Expr*> open(Expr* node)
    {
        if (defer_depth > 0) {
            defers.back().back().args.push_back({ .close = false, .node = node });
            if (auto v = dynamic_cast<VariableExpr*>(node)) {
                if (current_struct != nullptr) {
                    StructDecl sd = std::get<StructDecl>(current_struct->data);
                    for (size_t i = 0; i < sd.fieldNames.size(); i++) {
                        if (sd.fieldNames[i].value != v->name.value) {
                            continue;
                        }
                        VariableExpr* self = new VariableExpr(Lexer::Token { "IDENTIFIER", "self" });
                        self->decl = std::get<FuncDecl>(current_func->data).params[0];
                        UnaryExpr* un = new UnaryExpr(Lexer::Token { "OP", "*" }, self);
                        AttribExpr* att = new AttribExpr(un, v->name);
                        return Parser::Named<Expr*>(att);
                    }
                }
            }
            return Parser::Named<Expr*>(node);
        }
        if (auto v = dynamic_cast<VariableExpr*>(node)) {
            bool found = false;
            for (auto m : std::views::reverse(declarations)) {
                auto d = m.declarations.find(v->name.value);
                if (d != m.declarations.end()) {
                    found = true;
                    v->decl = d->second;
                    break;
                }
            }
            if (current_struct != nullptr) {
                StructDecl sd = std::get<StructDecl>(current_struct->data);
                for (size_t i = 0; i < sd.fieldNames.size(); i++) {
                    if (sd.fieldNames[i].value != v->name.value) {
                        continue;
                    }
                    VariableExpr* self = new VariableExpr(Lexer::Token { "IDENTIFIER", "self" });
                    UnaryExpr* un = new UnaryExpr(Lexer::Token { "OP", "*" }, self);
                    AttribExpr* att = new AttribExpr(un, v->name);
                    return Parser::Named<Expr*>(att);
                }
            }
            if (!found) {
                error("Unknown name \"" + v->name.value + "\"");
            }
        }

        return Parser::Named<Expr*>(node);
    }

    Parser::Named<Stmt*> open(Stmt* node)
    {
        if (auto f = dynamic_cast<FuncStmt*>(node)) {
            if (declarations.size() > 2) {
                error("Functions can only be declared inside a struct or inside global scope");
            }
            auto& scope = declarations.back();
            FuncDecl fd = {};

            if (current_struct) {
                fd.parentStruct = current_struct;

                auto* self_type = interner->getPointer(fd.parentStruct->toType());
                auto* self_decl = new Decl {
                    .kind = DeclKind::VAR,
                    .name = Lexer::Token {
                        "IDENTIFIER",
                        "self",
                    },
                    .data = VarDecl { self_type, .arg_index = 0 },
                    .func_param = true
                };
                declarations.back().declarations["self"] = self_decl;
                fd.params.push_back(self_decl);
            }

            for (auto generic_param : f->genericParams) {
                fd.genericParams.push_back(generic_param);
            }
            fd.returnType = f->returnType;

            for (size_t i = 0; i < f->paramNames.size(); ++i) {
                auto& name = f->paramNames[i];
                auto* decl = new Decl {
                    .kind = DeclKind::VAR,
                    .name = name,
                    .data = VarDecl { f->paramTypes[i], .arg_index = (current_struct != nullptr ? i + 1 : i) },
                    .func_param = true,
                };
                taken(name.value);
                fd.params.push_back(decl);
            }

            defers.back().push_back(FuncDefer { f, {} });
            defer_depth++;
            f->decl = new Decl {
                .kind = f->generic ? DeclKind::GENERIC_FUNC : DeclKind::FUNC,
                .name = f->name,
                .data = fd,
                .visibility = f->visibility,
            };
            if (current_struct) {
                std::get<StructDecl>(current_struct->data).methods.push_back(f->decl);
            }
            current_func = f->decl;
            taken(f->name.value);
            scope.declarations[f->name.value] = f->decl;
        } else if (auto s = dynamic_cast<StructStmt*>(node)) {
            if (declarations.size() > 1) {
                error("Cannot declare struct inside any other block");
            }
            StructDecl data;
            data.fieldTypes.insert(data.fieldTypes.begin(), s->types.begin(),
                s->types.end());
            data.fieldNames.insert(data.fieldNames.begin(), s->names.begin(),
                s->names.end());
            data.fieldDefs.insert(data.fieldDefs.begin(), s->definitions.begin(),
                s->definitions.end());
            s->decl = new Decl {
                .kind = DeclKind::STRUCT,
                .name = s->name,
                .data = data,
                .visibility = s->visibility,
            };
            taken(s->name.value);
            current_struct = s->decl;
            declarations.back().declarations[s->name.value] = s->decl;
            declarations.emplace_back();
        } else if (defer_depth == 0) {
            if (auto a = dynamic_cast<AssignStmt*>(node)) {
                a->decl = new Decl {
                    .kind = DeclKind::VAR,
                    .name = a->name,
                    .data = VarDecl { a->type },
                    .visibility = a->visibility,
                };
                taken(a->name.value);
                declarations.back().declarations[a->name.value] = a->decl;
            } else if (auto r = dynamic_cast<RegionStmt*>(node)) {
                if (declarations.size() == 0) {
                    error("Cannot define region in global scope.");
                } else if (declarations.back().decl->kind == DeclKind::STRUCT) {
                    error("Cannot define region inside a struct.");
                }
                r->decl = new Decl {
                    .kind = DeclKind::REGION,
                    .name = r->name,
                    .data = {},
                };
                taken(r->name.value);
                declarations.back().declarations[r->name.value] = r->decl;
            } else if (dynamic_cast<BlockStmt*>(node)) {
                declarations.emplace_back();
            } else if (auto r = dynamic_cast<ExprStmt*>(node)) {
                if (declarations.size() == 0) {
                    error("Statement cannot be in global scope.");
                } else if (declarations.back().decl->kind == DeclKind::STRUCT) {
                    error("Statement cannot be inside a struct.");
                }
            }
        } else if (defer_depth != 0) {
            defers.back().back().args.push_back({ .close = false, .node = node });
        }

        return Parser::Named<Stmt*>(node);
    }

    template <typename T>
    Parser::Named<T> close(T node)
    {
        if (dynamic_cast<StructStmt*>(node)) {
            current_struct = nullptr;
            declarations.pop_back();
        } else if (dynamic_cast<FuncStmt*>(node)) {
            defer_depth--;
        } else if (defer_depth == 0) {
            if (dynamic_cast<BlockStmt*>(node)) {
                declarations.pop_back();
            }
        } else if (defer_depth != 0) {
            defers.back().back().args.push_back({ .close = true, .node = node });
        }

        return Parser::Named<T>(node);
    }

    inline void finish() { assert(declarations.size() == 1); }
};