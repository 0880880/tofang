#include "parser.h"
#include "ast.h"
#include "type.h"
#include <cassert>
#include <optional>
#include <stdexcept>
#include <string>

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
  bool isV(const std::string &value) { return it != end && it->value == value; }

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

  [[nodiscard]] string str() const {
    return it->type + ":" + it->value + "@L" + to_string(it->sourceLineStart);
  }
};

optional<TypeThing *> type(Ptr &p) {
  TypeThing *t;
  if (p.is("PRIMITIVE")) {
    if ((*p).value == "void") {
      t = type_void;
    } else if ((*p).value == "bool") {
      t = type_bool;
    } else if ((*p).value == "u8") {
      t = type_u8;
    } else if ((*p).value == "u16") {
      t = type_u16;
    } else if ((*p).value == "u32") {
      t = type_u32;
    } else if ((*p).value == "u64") {
      t = type_u64;
    } else if ((*p).value == "i8") {
      t = type_i8;
    } else if ((*p).value == "i16") {
      t = type_i16;
    } else if ((*p).value == "i32") {
      t = type_i32;
    } else if ((*p).value == "i64") {
      t = type_i64;
    } else if ((*p).value == "f32") {
      t = type_f32;
    } else if ((*p).value == "f64") {
      t = type_f64;
    }
  } else {
    return nullopt;
  }
  ++p;
  while (true) {
    if (p.is("OP") && p.isV("*")) {
      ++p;
      t = interner->getPointer(t);
    } else if (p.is("OP") && p.isV("&")) {
      ++p;
      t = interner->getReference(t);
    } else {
      break;
    }
  }
  if (p.is("QUESTION")) {
    ++p;
    t = interner->getNullable(t);
  }
  return t;
}

int precedence(const std::string &op) {
  // if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=") {
  //   return 1;
  // }
  if (op == "||") {
    return 2;
  }
  if (op == "&&") {
    return 3;
  }
  if (op == "|") {
    return 4;
  }
  if (op == "^") {
    return 5;
  }
  if (op == "&") {
    return 6;
  }
  if (op == "==" || op == "!=") {
    return 7;
  }
  if (op == "<" || op == "<=" || op == ">" || op == ">=") {
    return 8;
  }
  if (op == "+" || op == "-") {
    return 9;
  }
  if (op == "*" || op == "/") {
    return 10;
  }
  return -1;
}

Expr *expr(Ptr &p, int minPrec = 0);

Expr *primary(Ptr &p) {

  if (p.eof()) {
    throw std::runtime_error("Unexpected EOF in primary()");
  }

  if (p.is("IDENTIFIER")) {
    auto v = new VariableExpr(*p);
    ++p;
    return v;
  }

  if (auto ty = type(p)) {
    auto l = new LiteralExpr(LiteralExpr::Type::MetaType, *p);
    l->typeValue = *ty;
    return l;
  }

  if (p.is("NULL")) {
    auto l = new LiteralExpr(LiteralExpr::Type::Null, *p);
    ++p;
    return l;
  }
  if (p.is("INTEGER") || p.is("DECIMAL") || p.is("BOOLEAN") || p.is("CHAR") ||
      p.is("STRING")) {
    auto l = new LiteralExpr(LiteralExpr::Type::Integer, *p);
    if (p.is("DECIMAL")) {
      l->type = LiteralExpr::Type::Decimal;
    }
    if (p.is("BOOLEAN")) {
      l->type = LiteralExpr::Type::Boolean;
    }
    if (p.is("CHAR")) {
      l->type = LiteralExpr::Type::Char;
    }
    if (p.is("STRING")) {
      l->type = LiteralExpr::Type::String;
    }

    ++p;
    return l;
  }

  if (p.is("LPAREN")) {
    ++p;
    Expr *inner = expr(p);
    p.expect("RPAREN");
    return new GroupingExpr(inner);
  }

  throw runtime_error("Unexpected token in primary(): " + (*p).type + "  L" +
                      to_string((*p).sourceLineStart));
}

Expr *postfix(Ptr &p) {
  Expr *left = primary(p);
  while (!p.eof()) {
    if (p.is("DOT")) {
      ++p;
      auto bar = *p;
      ++p;
      left = new AttribExpr(left, bar);
    } else if (p.is("LBRACKET")) {
      ++p;
      auto ind = new IndexExpr(left, expr(p));
      p.expect("RBRACKET");
      left = ind;
    } else if (p.isV("<")) {
      ++p;
      auto c = new CallExpr(left);
      bool start = true;
      while (start || p.is("COMMA")) {
        if (p.isV(">")) {
          break;
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
      while (start || p.is("COMMA")) {
        if (p.is("RPAREN")) {
          break;
        }
        start = false;
        c->args.push_back(expr(p));
      }
      ++p;
      left = c;
    } else if (p.is("LPAREN")) {
      bool start = true;
      auto c = new CallExpr(left);
      ++p;
      while (start || p.is("COMMA")) {
        if (p.is("RPAREN")) {
          break;
        }
        start = false;
        c->args.push_back(expr(p));
      }
      ++p;
      left = c;
    } else {
      break;
    }
  }

  return left;
}

Expr *prefix(Ptr &p) {
  if (p.eof()) {
    throw std::runtime_error("Unexpected EOF in prefix()");
  }

  if (p.is("OP") && p.expectV("&", "*", "-", "+", "!")) {
    Lexer::Token op = *p;
    ++p;
    return new UnaryExpr(op, prefix(p));
  }

  return postfix(p);
}

Expr *binExpr(Ptr &p, int minPrec) {
  Expr *left = prefix(p);

  while (!p.eof() && p.is("OP")) {
    Lexer::Token op = *p;
    int prec = precedence(op.value);
    if (prec < minPrec) {
      break;
    }

    ++p;

    Expr *right = expr(p, prec + 1);
    left = new BinaryExpr(left, op, right);
  }

  return left;
}

Expr *expr(Ptr &p, int minPrec) {
  Expr *left = binExpr(p, minPrec);

  if (!p.eof() && p.is("EQUAL")) {
    Lexer::Token op = *p;
    ++p;
    Expr *right = expr(p, 0);
    return new AssignExpr(left, op, right);
  }

  return left;
}

Stmt *statement(Ptr &p) {
  if (p.eof()) {
    throw std::runtime_error("Unexpected EOF in statement()");
  }

  if (p.is("KEYWORD") && (*p).value == "return") {
    ++p;
    auto r = new ReturnStmt();
    r->value = expr(p);
    p.expect("SEMICOLON");
    return r;
  }

  if (p.is("KEYWORD") && (*p).value == "if") {
    ++p;
    auto *ifs = new IfStmt();
    p.expect("LPAREN");
    Expr *condition = expr(p);
    ifs->condition = condition;
    p.expect("RPAREN");
    p.expect("LBRACE");

    while (!p.eof() && !p.is("RBRACE")) {
      ifs->body.statements.push_back(statement(p));
    }

    p.expect("RBRACE");

    IfStmt *cur = ifs;

    if (p.isV("else")) {
      ++p;
      bool has_else = true;
      while (p.isV("if")) {
        ++p;
        cur->elseIf = new IfStmt();
        cur = cur->elseIf;
        p.expect("LPAREN");
        cur->condition = expr(p);
        p.expect("RPAREN");
        p.expect("LBRACE");

        while (!p.eof() && !p.is("RBRACE")) {
          cur->body.statements.push_back(statement(p));
        }

        p.expect("RBRACE");
        if (p.isV("else")) {
          ++p;
        } else {
          has_else = false;
          break;
        }
      }
      if (has_else) {
        p.expect("LBRACE");
        cur->elseStmt = new ElseStmt();

        while (!p.eof() && !p.is("RBRACE")) {
          cur->elseStmt->body.statements.push_back(statement(p));
        }

        p.expect("RBRACE");
      }
    }

    return ifs;
  }

  if (p.is("KEYWORD") && (*p).value == "region") {
    ++p;
    Lexer::Token name = *p;
    p.expect("IDENTIFIER");
    auto r = new RegionStmt(name);
    p.expect("LBRACE");

    while (!p.eof() && !p.is("RBRACE")) {
      r->body.statements.push_back(statement(p));
    }

    p.expect("RBRACE");
    return r;
  }

  if (p.is("KEYWORD") && (*p).value == "for") {
    ++p;
    p.expect("LPAREN");
    Stmt *init = nullptr;
    Expr *condition = nullptr;
    Expr *update = nullptr;
    if (!p.is("SEMICOLON")) {
      init = statement(p);
    }
    p.expect("SEMICOLON");
    if (!p.is("SEMICOLON")) {
      condition = expr(p);
    }
    p.expect("SEMICOLON");
    if (!p.is("RPAREN")) {
      update = expr(p);
    }
    p.expect("RPAREN");
    auto *f = new ForStmt();
    f->init = init;
    f->condition = condition;
    f->update = update;
    p.expect("LBRACE");

    while (!p.eof() && !p.is("RBRACE")) {
      f->body.statements.push_back(statement(p));
    }

    p.expect("RBRACE");
    return f;
  }

  if (auto ty = type(p)) {
    auto t = *ty;

    Lexer::Token name = *p;
    p.expect("IDENTIFIER");

    if (p.is("EQUAL")) {
      ++p;
      Expr *rhs = expr(p);
      p.expect("SEMICOLON");
      return new AssignStmt(t, name, rhs);
    }

    if (p.isV("<")) {
      ++p;
      auto *fn = new FuncStmt(t, name);
      fn->generic = true;
      bool start = true;
      while (start || p.is("COMMA")) {
        if (p.isV(">")) {
          break;
        }
        start = false;
        auto v = *p;
        p.expect("IDENTIFIER");
        fn->genericParams.push_back(interner->getTypeVar(v.value));
      }
      ++p;
      p.expect("LPAREN");
      p.expect("RPAREN");
      p.expect("LBRACE");

      while (!p.eof() && !p.is("RBRACE")) {
        fn->body.statements.push_back(statement(p));
      }

      p.expect("RBRACE");
      return fn;
    }

    if (p.is("LPAREN")) {
      ++p;
      p.expect("RPAREN");
      p.expect("LBRACE");

      auto *fn = new FuncStmt(t, name);

      while (!p.eof() && !p.is("RBRACE")) {
        fn->body.statements.push_back(statement(p));
      }

      p.expect("RBRACE");
      return fn;
    }
  }
  Expr *e = expr(p);
  p.expect("SEMICOLON");
  return new ExprStmt(e);

  throw std::runtime_error("Unexpected token in statement(): " + (*p).type);
}

Program Parser::buildAST(Tokens tokens) {
  Program prog;

  auto it = tokens.begin();
  Ptr ptr(it, tokens.end());

  while (it != tokens.end()) {
    prog.statements.push_back(statement(ptr));
  }

  return prog;
}
