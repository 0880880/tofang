#pragma once
#include "ast.h"
#include "lexer.h"
#include <cassert>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

class Symbols;

static inline void error(const string &msg) {
  std::cerr << msg << '\n';
  std::cout.flush();
  std::cerr.flush();
  exit(1);
}

template <typename T>
concept ASTNodePtr = std::is_pointer_v<T> &&
                     std::derived_from<std::remove_pointer_t<T>, ASTNode>;

class Parser {

public:
  std::vector<std::pair<Lexer::Token, TypeThing *>> types;

  Parser();

  template <typename T> class Named {
  public:
    explicit Named(T n) : node(n) {}

    template <typename U>
    Named(const Named<U> &other)
      requires std::is_convertible_v<U, T>
        : node(other.get()) {}

    T get() const { return node; }

    T operator->() const { return node; }
    auto &operator*() const { return *node; }

    explicit operator T() const { return node; }

  private:
    T node;
  };

  using Tokens = std::vector<Lexer::Token>;
  using Iter = Tokens::iterator;
  class Ptr {
    Iter &it;
    Iter end;

  public:
    Ptr(Iter &it, Iter end) : it(it), end(end) {}

    [[nodiscard]] bool eof() const { return it == end; }

    Lexer::Token &operator*() {
      if (it == end) {
        throw std::runtime_error("Unexpected EOF");
      }
      return *it;
    }

    Ptr &operator++() {
      if (it != end) {
        ++it;
      }
      return *this;
    }

    Ptr &operator--() {
      if (it != end) { // FIXME check if it != begin
        --it;
      }
      return *this;
    }

    bool is(const std::string &type) { return it != end && it->type == type; }
    bool isV(const std::string &value) {
      return it != end && it->value == value;
    }

    bool expect(const std::string &type) {
      if (!is(type)) {
        if (it != end) {
          throw std::runtime_error("Expected token: " + type + ", got " +
                                   it->type + " L" +
                                   std::to_string(it->sourceLineStart) + " .");
        } else {
          throw std::runtime_error("Expected token: " + type + ", got EOF.");
        }
      }
      ++it;
      return true;
    }

    template <typename... Ts> bool expectV(const Ts &...expected) {
      // Check each expected value
      bool matched = ((isV(expected)) || ...);

      if (!matched) {
        if (it != end) {
          std::string expected_list = ((std::string(expected) + ", ") + ...);
          expected_list.resize(expected_list.size() - 2); // strip final comma

          throw std::runtime_error("Expected one of: [" + expected_list +
                                   "], got " + it->value + ".");
        } else {
          throw std::runtime_error("Expected one of provided tokens, got EOF.");
        }
      }

      ++it;
      return true;
    }

    [[nodiscard]] std::string str() const {
      return it->type + ":" + it->value + "@L" +
             std::to_string(it->sourceLineStart);
    }
  };

  Program buildAST(vector<Lexer::Token> tokens);

private:
  std::unique_ptr<Symbols> symbols;

  std::optional<TypeThing *> type(Ptr &p);
  Named<Expr *> primary(Ptr &p);
  Named<Expr *> postfix(Ptr &p);
  Named<Expr *> prefix(Ptr &p);
  Named<Expr *> binExpr(Ptr &p, int minPrec = 0);
  Named<Expr *> expr(Ptr &p, int minPrec = 0);
  Named<Stmt *> statement(Ptr &p);

  Decl *search(const std::string &name, bool fail = false);
};

class Symbols {
public:
  std::deque<std::unordered_map<std::string, Decl *>> declarations = {{}};

  void taken(const std::string &name);

  template <typename T> Parser::Named<T> open(T node) {
    if (auto f = dynamic_cast<FuncStmt *>(node)) {
      auto &scope = declarations.back();
      declarations.emplace_back();
      FuncDecl fd = {};
      for (auto generic_param : f->genericParams) {
        fd.genericParams.push_back(generic_param);
      }
      fd.returnType = f->returnType;

      for (size_t i = 0; i < f->paramNames.size(); ++i) {
        auto name = f->paramNames[i];
        auto decl = new Decl{.kind = DeclKind::VAR,
                             .name = name,
                             .data = VarDecl{f->paramTypes[i]},
                             .region = nullptr};
        taken(name.value);
        declarations.back()[name.value] = decl;
        fd.params.push_back(decl);
      }
      f->decl =
          new Decl{.kind = f->generic ? DeclKind::GENERIC_FUNC : DeclKind::FUNC,
                   .name = f->name,
                   .data = fd,
                   .region = nullptr};
      taken(f->name.value);
      scope[f->name.value] = f->decl;
    } else if (auto a = dynamic_cast<AssignStmt *>(node)) {
      a->decl = new Decl{.kind = DeclKind::VAR,
                         .name = a->name,
                         .data = VarDecl{a->type},
                         .region = nullptr};
      taken(a->name.value);
      declarations.back()[a->name.value] = a->decl;
    } else if (auto r = dynamic_cast<RegionStmt *>(node)) {
      r->decl = new Decl{.kind = DeclKind::REGION,
                         .name = r->name,
                         .data = {},
                         .region = nullptr};
      taken(r->name.value);
      declarations.back()[r->name.value] = r->decl;
    } else if (dynamic_cast<BlockStmt *>(node)) {
      declarations.emplace_back();
    } else if (auto v = dynamic_cast<VariableExpr *>(node)) {
      bool found = false;
      for (auto m : std::views::reverse(declarations)) {
        auto d = m.find(v->name.value);
        if (d != m.end()) {
          found = true;
          v->decl = d->second;
          break;
        }
      }
      if (!found) {
        error("Unknown name \"" + v->name.value + "\"");
      }
    } else if (auto s = dynamic_cast<StructStmt *>(node)) {
      StructDecl data;
      data.fieldTypes.insert(data.fieldTypes.begin(), s->types.begin(),
                             s->types.end());
      data.fieldNames.insert(data.fieldNames.begin(), s->names.begin(),
                             s->names.end());
      data.fieldDefs.insert(data.fieldDefs.begin(), s->definitions.begin(),
                            s->definitions.end());
      s->decl = new Decl{.kind = DeclKind::STRUCT,
                         .name = s->name,
                         .data = data,
                         .region = nullptr};
      taken(s->name.value);
      declarations.back()[s->name.value] = s->decl;
    }

    return Parser::Named<T>(node);
  }

  template <typename T> Parser::Named<T> close(T node) {
    if (dynamic_cast<FuncStmt *>(node) || dynamic_cast<BlockStmt *>(node)) {
      declarations.pop_back();
    }

    return Parser::Named<T>(node);
  }

  inline void finish() { assert(declarations.size() == 1); }

private:
  void resolveTypes(bool final = false);
};