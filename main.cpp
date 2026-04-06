#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "symbols.h"
#include "typechecker.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;
using namespace std;

string readFile(const fs::path &path) {
  ifstream file(path);
  stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}
void symbolWalk(ASTNode *node) {
  Symbols::walkAstOpen(node);
  for (auto sub : node->walk()) {
    symbolWalk(sub);
  }
  Symbols::walkAstClose(node);
}

void typeWalk(ASTNode *node) {
  TypeChecker::open(node);

  for (auto sub : node->walk()) {
    typeWalk(sub);
  }

  TypeChecker::close(node);
}

void printAST(ASTNode *node, const string &spacing = "") {
  cout << spacing << node->toString() << '\n';
  auto s = spacing + "  ";
  for (auto sub : node->walk()) {
    printAST(sub, s);
  }
}

int main() {
  Lexer lexer;
  lexer.token("true|false", "BOOLEAN");
  lexer.token("(?:[0-9]+\\.[0-9]+)|(?:\\.[0-9]+)", "DECIMAL");
  lexer.token("[0-9]+", "INTEGER");
  lexer.token(R"((?:"[^"]*"))", "STRING");
  lexer.token("(?:'[^']')", "CHAR");
  lexer.token("if|else|for|return|region|while|struct|class", "KEYWORD");
  lexer.token("void|bool|u8|u16|u32|u64|i8|i16|i32|i64|f32|f64", "PRIMITIVE");
  lexer.token("[a-zA-Z_][a-zA-Z0-9_]*", "IDENTIFIER");
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

  auto source = readFile(fs::path("main.to"));
  source = regex_replace(
      source, regex{"(?://[^\n]*)|(?:/\\*[^*]*\\*+(?:[^/*][^*]*\\*+)*/)"}, "");

  auto tokens = lexer.tokenize(source);

  for (Lexer::Token &t : tokens) {
    cout << t.type << ":" << t.value << " ";
  }
  cout << '\n';

  Program p = Parser::buildAST(tokens);

  printAST(&p);

  symbolWalk(&p);
  typeWalk(&p);

  return 0;
}