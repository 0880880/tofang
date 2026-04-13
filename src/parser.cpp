#include "parser.h"
#include "ast.h"
#include "type.h"
#include <cassert>
#include <optional>
#include <stdexcept>
#include <string>

using Ptr = Parser::Ptr;
template <typename T> using Named = Parser::Named<T>;

Decl *Parser::search(const std::string &name, bool fail) {
  for (auto map : std::views::reverse(symbols->declarations)) {
    auto d = map.find(name);
    if (d != map.end()) {
      return d->second;
    }
  }
  if (fail) {
    throw std::runtime_error("Unkown symbol: " + name);
  }
  return nullptr;
}

void Symbols::taken(const std::string &name) {
  for (auto map : std::views::reverse(declarations)) {
    auto d = map.find(name);
    if (d != map.end()) {
      throw runtime_error("Name taken: " + name);
    }
  }
}

Parser::Parser() { symbols = std::make_unique<Symbols>(); }

optional<TypeThing *> Parser::type(Ptr &p) {
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
  } else if (p.is("IDENTIFIER") && search((*p).value, false) &&
             search((*p).value, false)->kind ==
                 DeclKind::STRUCT) { // TODO avoid redoing search
    auto *str = search((*p).value, false);
    t = interner->getStruct(str);
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
  types.emplace_back(*p, t);
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

Named<Expr *> Parser::primary(Ptr &p) {

  if (p.eof()) {
    throw std::runtime_error("Unexpected EOF in primary()");
  }

  if (auto ty = type(p)) {
    auto l = new LiteralExpr(LiteralExpr::Type::MetaType, *p);
    l->typeValue = *ty;
    return symbols->open(l);
  }

  if (p.is("IDENTIFIER")) {
    auto v = new VariableExpr(*p);
    ++p;
    return symbols->open(v);
  }

  if (p.is("NULL")) {
    auto l = new LiteralExpr(LiteralExpr::Type::Null, *p);
    ++p;
    return symbols->open(l);
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
    return symbols->open(l);
  }

  if (p.is("LPAREN")) {
    ++p;
    Expr *inner = expr(p).get();
    p.expect("RPAREN");
    return symbols->open(new GroupingExpr(inner));
  }

  throw runtime_error("Unexpected token in primary(): " + (*p).type + "  L" +
                      to_string((*p).sourceLineStart));
}

Named<Expr *> Parser::postfix(Ptr &p) {
  Expr *left = primary(p).get();
  while (!p.eof()) {
    if (p.is("DOT")) {
      ++p;
      auto bar = *p;
      ++p;
      left = new AttribExpr(left, bar);
    } else if (p.is("LBRACKET")) {
      ++p;
      auto ind = new IndexExpr(left, expr(p).get());
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
        if (!start) {
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
      while (start || p.is("COMMA")) {
        if (p.is("RPAREN")) {
          break;
        }
        if (!start) {
          p.expect("COMMA");
        }
        start = false;
        c->args.push_back(expr(p).get());
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
        if (!start) {
          p.expect("COMMA");
        }
        start = false;
        c->args.push_back(expr(p).get());
      }
      ++p;
      left = c;
    } else if (p.is("LBRACE")) {
      auto in = new StructInitExpr();
      if (auto lit = dynamic_cast<LiteralExpr *>(left)) {
        if (lit->type != LiteralExpr::Type::MetaType) {
          delete in;
          throw runtime_error("Struct initializer LHS must be a type.");
        } else {
          in->struct_type = lit->typeValue;
        }
      } else {
        throw runtime_error("Struct initializer LHS must be a type.");
      }
      bool start = true;
      ++p;
      while (start || p.is("COMMA")) {
        if (p.is("RBRACE")) {
          break;
        }
        if (!start) {
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
    } else {
      break;
    }
  }

  return symbols->open(left);
}

Named<Expr *> Parser::prefix(Ptr &p) {
  if (p.eof()) {
    throw std::runtime_error("Unexpected EOF in prefix()");
  }

  if (p.is("OP") && p.expectV("&", "*", "-", "+", "!")) {
    Lexer::Token op = *p;
    ++p;
    return symbols->open(new UnaryExpr(op, prefix(p).get()));
  }

  return postfix(p);
}

Named<Expr *> Parser::binExpr(Ptr &p, int minPrec) {
  Expr *left = prefix(p).get();

  while (!p.eof() && p.is("OP")) {
    Lexer::Token op = *p;
    int prec = precedence(op.value);
    if (prec < minPrec) {
      break;
    }

    ++p;

    Expr *right = expr(p, prec + 1).get();
    left = new BinaryExpr(left, op, right);
  }

  return symbols->open(left);
}

Named<Expr *> Parser::expr(Ptr &p, int minPrec) {
  Expr *left = binExpr(p, minPrec).get();

  if (!p.eof() && p.is("EQUAL")) {
    Lexer::Token op = *p;
    ++p;
    Expr *right = expr(p, 0).get();
    return symbols->open(new AssignExpr(left, op, right));
  }

  return symbols->open(left);
}

Named<Stmt *> Parser::statement(Ptr &p) {
  if (p.eof()) {
    throw std::runtime_error("Unexpected EOF in statement()");
  }

  if (p.is("KEYWORD") && (*p).value == "return") {
    ++p;
    auto r = new ReturnStmt();
    r->value = expr(p).get();
    p.expect("SEMICOLON");
    return symbols->open(r);
  }

  if (p.is("KEYWORD") && (*p).value == "struct") {
    ++p;
    Lexer::Token name = *p;
    p.expect("IDENTIFIER");
    p.expect("LBRACE");
    auto *str = new StructStmt(name);
    while (!p.eof() && !p.is("RBRACE")) {
      if (auto t = type(p)) {
        auto ty = *t;
        str->types.push_back(ty);
        auto field_name = *p;
        str->names.push_back(field_name);
        p.expect("IDENTIFIER");
        p.expect("EQUAL");
        str->definitions.push_back(expr(p).get());
        p.expect("SEMICOLON");
      } else {
        delete str;
        throw runtime_error("Expected type inside struct " + name.value);
      }
    }
    p.expect("RBRACE");
    return symbols->open(str);
  }

  if (p.is("KEYWORD") && (*p).value == "if") {
    ++p;
    auto *ifs = new IfStmt();
    p.expect("LPAREN");
    Expr *condition = expr(p).get();
    ifs->condition = condition;
    p.expect("RPAREN");
    p.expect("LBRACE");

    auto named = symbols->open(ifs);
    symbols->open(&ifs->body);

    while (!p.eof() && !p.is("RBRACE")) {
      ifs->body.statements.push_back(statement(p).get());
    }

    symbols->close(&ifs->body);

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
        cur->condition = expr(p).get();
        p.expect("RPAREN");
        p.expect("LBRACE");

        symbols->open(cur);
        symbols->open(&cur->body);

        while (!p.eof() && !p.is("RBRACE")) {
          cur->body.statements.push_back(statement(p).get());
        }

        symbols->close(&cur->body);

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

        symbols->open(cur);
        symbols->open(&cur->elseStmt->body);

        while (!p.eof() && !p.is("RBRACE")) {
          cur->elseStmt->body.statements.push_back(statement(p).get());
        }

        symbols->close(&cur->elseStmt->body);

        p.expect("RBRACE");
      }
    }

    return named;
  }

  if (p.is("KEYWORD") && (*p).value == "region") {
    ++p;
    Lexer::Token name = *p;
    p.expect("IDENTIFIER");
    auto r = new RegionStmt(name);
    p.expect("LBRACE");

    auto named = symbols->open(r);
    symbols->open(&r->body);

    while (!p.eof() && !p.is("RBRACE")) {
      r->body.statements.push_back(statement(p).get());
    }

    symbols->close(&r->body);

    p.expect("RBRACE");
    return named;
  }

  if (p.is("KEYWORD") && (*p).value == "for") {
    ++p;
    p.expect("LPAREN");
    Stmt *init = nullptr;
    Expr *condition = nullptr;
    Expr *update = nullptr;
    if (!p.is("SEMICOLON")) {
      init = statement(p).get();
    }
    p.expect("SEMICOLON");
    if (!p.is("SEMICOLON")) {
      condition = expr(p).get();
    }
    p.expect("SEMICOLON");
    if (!p.is("RPAREN")) {
      update = expr(p).get();
    }
    p.expect("RPAREN");
    auto *f = new ForStmt();
    f->init = init;
    f->condition = condition;
    f->update = update;
    p.expect("LBRACE");

    auto named = symbols->open(f);
    symbols->open(&f->body);

    while (!p.eof() && !p.is("RBRACE")) {
      f->body.statements.push_back(statement(p).get());
    }

    symbols->close(&f->body);

    p.expect("RBRACE");
    return named;
  }

  if (auto ty = type(p)) {
    auto t = *ty;

    Lexer::Token name = *p;
    p.expect("IDENTIFIER");

    if (p.is("EQUAL")) {
      ++p;
      Expr *rhs = expr(p).get();
      p.expect("SEMICOLON");
      return symbols->open(new AssignStmt(t, name, rhs));
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
        if (!start) {
          p.expect("COMMA");
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

      auto named = symbols->open(fn);
      symbols->open(&fn->body);

      while (!p.eof() && !p.is("RBRACE")) {
        fn->body.statements.push_back(statement(p).get());
      }

      symbols->close(&fn->body);

      p.expect("RBRACE");
      return named;
    }

    if (p.is("LPAREN")) {
      ++p;
      p.expect("RPAREN");
      p.expect("LBRACE");

      auto *fn = new FuncStmt(t, name);

      auto named = symbols->open(fn);
      symbols->open(&fn->body);

      while (!p.eof() && !p.is("RBRACE")) {
        fn->body.statements.push_back(statement(p).get());
      }

      symbols->close(&fn->body);

      p.expect("RBRACE");
      return named;
    }
  }
  Expr *e = expr(p).get();
  p.expect("SEMICOLON");
  return symbols->open(new ExprStmt(e));

  throw std::runtime_error("Unexpected token in statement(): " + (*p).type);
}

Program Parser::buildAST(Tokens tokens) {
  Program prog;

  auto it = tokens.begin();
  Ptr ptr(it, tokens.end());

  while (it != tokens.end()) {
    prog.statements.push_back(statement(ptr).get());
  }

  return prog;
}
