#pragma once
#include "ast.h"
#include "lexer.h"
#include <optional>
#include <stdexcept>
#include <vector>

using namespace std;

class Parser {

public:
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
  std::optional<TypeThing *> type(Ptr &p);
  Expr *primary(Ptr &p);
  Expr *postfix(Ptr &p);
  Expr *prefix(Ptr &p);
  Expr *binExpr(Ptr &p, int minPrec = 0);
  Expr *expr(Ptr &p, int minPrec = 0);
  Stmt *statement(Ptr &p);
};