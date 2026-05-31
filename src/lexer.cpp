#include "lexer.h"
#include <algorithm>
#include <cstdint>
#include <ranges>
#include <regex>
#include <utility>

using namespace std;

size_t getLine(const string& text, size_t pos)
{
    return 1 + static_cast<uint64_t>(count(text.begin(), text.begin() + pos, '\n'));
}

void Lexer::token(string re, string type)
{
    tokenTypes.push_back({ .pattern = std::move(re), .type = std::move(type) });
}

vector<Lexer::Token> Lexer::tokenize(const string& source)
{
    string pat;
    pat.reserve(30 * tokenTypes.size());

    for (size_t i = 0; i < tokenTypes.size(); ++i) {
        if (i) {
            pat.append("|");
        }
        pat.append("(");
        pat.append(tokenTypes[i].pattern);
        pat.append(")");
    }

    auto no_ws = pat | views::filter([](unsigned char c) { return !isspace(c); });
    std::string clean_pat(no_ws.begin(), no_ws.end());
    pat = std::move(clean_pat);
    regex pattern(pat);

    auto begin = sregex_iterator(source.begin(), source.end(), pattern);
    auto end = sregex_iterator();

    vector<Lexer::Token> tokens;

    for (std::sregex_iterator i = begin; i != end; ++i) {
        const smatch& match = *i;
        auto sub = match.str(0);
        for (size_t j = 1; j < match.size(); ++j) {
            auto g = match.str(j);
            if (g != "") {
                auto m_start = static_cast<size_t>(match.position(0));
                auto m_end = static_cast<size_t>(match.position(0)) + sub.length();
                tokens.emplace_back(tokenTypes[j - 1].type, sub, m_start, m_end,
                    getLine(source, m_start), getLine(source, m_end));
                break;
            }
        }
    }
    return tokens;
}