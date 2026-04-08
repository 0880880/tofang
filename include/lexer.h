#pragma once
#include <string>
#include <utility>
#include <vector>

using namespace std;

class Lexer {
private:
  struct TokenType {
    string pattern;
    string type;
  };
  vector<TokenType> tokenTypes;

public:
  struct Token {
    string type;
    string value;

    size_t sourceStart;
    size_t sourceEnd;
    size_t sourceLineStart;
    size_t sourceLineEnd;

    bool user;

    Token(string type, string value, size_t sourceStart, size_t sourceEnd,
          size_t sourceLineStart, size_t sourceLineEnd)
        : type(std::move(std::move(type))), value(std::move(std::move(value))),
          sourceStart(sourceStart), sourceEnd(sourceEnd),
          sourceLineStart(sourceLineStart), sourceLineEnd(sourceLineEnd),
          user(true) {}
    Token(string type, string value)
        : type(std::move(std::move(type))), value(std::move(std::move(value))),
          user(false) {}

    bool operator==(const Token &) const = default;
  };

  void token(string re, string type);

  vector<Token> tokenize(const string &source);
};
