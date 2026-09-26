#include "Lexer.h"
#include "Source.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>

static long long unescape(const std::string &s, std::size_t &i, std::size_t,
                          bool wide = false) {
    char c = s[i++];
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'a': return '\a';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'v': return '\v';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"': return '"';
    case '?': return '?';
    default: break;
    }

    if (c == 'x' || c == 'X') {
        long long v = 0;
        bool any = false;
        while (i < s.size() && std::isxdigit(static_cast<unsigned char>(s[i]))) {
            char d = s[i++];
            int digit = std::isdigit(static_cast<unsigned char>(d))
                      ? d - '0'
                      : (std::tolower(static_cast<unsigned char>(d)) - 'a' + 10);
            v = v * 16 + digit;
            any = true;
        }
        if (!any) return static_cast<unsigned char>(c);

        return wide ? v : (v & 0xff);
    }

    if (c >= '0' && c <= '7') {
        long long v = c - '0';
        for (int n = 0; n < 2 && i < s.size() && s[i] >= '0' && s[i] <= '7'; n++)
            v = v * 8 + (s[i++] - '0');
        return v & 0xff;
    }

    return static_cast<unsigned char>(c);
}

// [lex.charset]/2: \u takes four hex digits and \U eight; the code point they name.
static long long universalName(const std::string &s, std::size_t &i, int digits) {
    long long v = 0;
    for (int n = 0; n < digits && i < s.size() &&
                    std::isxdigit(static_cast<unsigned char>(s[i])); n++) {
        char d = s[i++];
        v = v * 16 + (std::isdigit(static_cast<unsigned char>(d))
                          ? d - '0'
                          : std::tolower(static_cast<unsigned char>(d)) - 'a' + 10);
    }
    return v;
}

// A code point as the UTF-8 bytes a narrow string carries - the execution charset here.
static void appendUtf8(std::string &text, long long cp) {
    if (cp < 0x80) { text.push_back(static_cast<char>(cp)); return; }
    if (cp < 0x800) {
        text.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    } else if (cp < 0x10000) {
        text.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    } else {
        text.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        text.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    }
    text.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
}

// The eleven word-spelled alternative tokens of [lex.digraph] table 2. They are
// keywords in C++ and not macros, and each behaves in every respect as the
// operator it spells - so it becomes that token here, spelling apart.
const char *Lexer::alternativeToken(const std::string &word) {
    static const struct { const char *word; const char *primary; } alt[] = {
        { "and", "&&" },    { "and_eq", "&=" }, { "bitand", "&" },
        { "bitor", "|" },   { "compl", "~" },   { "not", "!" },
        { "not_eq", "!=" }, { "or", "||" },     { "or_eq", "|=" },
        { "xor", "^" },     { "xor_eq", "^=" }
    };
    for (const auto &a : alt)
        if (word == a.word) return a.primary;
    return nullptr;
}

bool Lexer::isKeyword(const std::string &word) {
    // Every C++11 keyword spelled as a word, including the ones nothing here
    // can parse yet: recognising a word is not implementing it, and a keyword
    // with no rule is refused by name. The eleven above never reach this.
    static const char *const kw[] = {
        "alignas", "alignof", "asm", "auto",
        "bool", "break",
        "case", "catch", "char", "char16_t", "char32_t", "class",
        "const", "constexpr", "const_cast", "continue",
        "decltype", "default", "delete", "do", "double", "dynamic_cast",
        "else", "enum", "explicit", "export", "extern",
        "false", "float", "for", "friend", "goto",
        "if", "inline", "int", "long",
        "mutable", "namespace", "new", "noexcept", "nullptr",
        "operator",
        "private", "protected", "public",
        "register", "reinterpret_cast", "return",
        "short", "signed", "sizeof", "static", "static_assert", "static_cast",
        "struct", "switch",
        "template", "this", "thread_local", "throw", "true", "try",
        "typedef", "typeid", "typename",
        "union", "unsigned", "using",
        "virtual", "void", "volatile",
        "wchar_t", "while"
    };
    for (const char *k : kw)
        if (word == k) return true;
    return false;
}

bool Lexer::exactlyADouble(const std::string &s, std::size_t from,
                           std::size_t to) {
    unsigned long long m = 0;
    long long tenth = 0;              // digits seen after the point
    long long zeros = 0;              // zeros seen and not yet needed
    bool afterPoint = false, overflowed = false, any = false;
    std::size_t i = from;
    for (; i < to; i++) {
        const char c = s[i];
        if (c == '.') { afterPoint = true; continue; }
        if (c == 'e' || c == 'E') break;
        if (!std::isdigit(static_cast<unsigned char>(c))) break;
        any = true;
        if (afterPoint) tenth++;
        // **A zero is held back until a later digit needs it.** Trailing zeros
        // only move the exponent, and multiplying them in overflowed the
        // accumulator; only a zero between significant digits is multiplied in.
        if (c == '0') { if (m != 0) zeros++; continue; }
        while (zeros > 0 && !overflowed) {
            if (m > (0xFFFFFFFFFFFFFFFFULL - 9) / 10) overflowed = true;
            else { m *= 10; zeros--; }
        }
        if (m > (0xFFFFFFFFFFFFFFFFULL - 9) / 10) overflowed = true;
        else m = m * 10 + static_cast<unsigned long long>(c - '0');
    }
    if (!any || overflowed) return false;

    long long exp10 = zeros - tenth;
    if (i < to && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        bool neg = false;
        if (i < to && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; }
        long long e = 0;
        for (; i < to && std::isdigit(static_cast<unsigned char>(s[i])); i++) {
            if (e > 100000) return false;
            e = e * 10 + (s[i] - '0');
        }
        exp10 += neg ? -e : e;
    }

    // **A negative power of ten divides out only through its fives.** 10^-k
    // is 2^-k / 5^k, and the halves are free in binary - so the value is a
    // dyadic rational exactly when 5^k divides the digits.
    while (exp10 < 0) {
        if (m % 5 != 0) return false;
        m /= 5;
        exp10++;
    }
    // **A positive power of ten multiplies in only through its fives**: 10^k is
    // 5^k * 2^k and the twos cost nothing, so what accumulates is the odd part -
    // an overflow there is past 2^53, which makes refusing it the true answer.
    while (m != 0 && m % 2 == 0) m /= 2;      // the factors of two are free
    while (exp10 > 0) {
        if (m > 0xFFFFFFFFFFFFFFFFULL / 5) return false;
        m *= 5;
        exp10--;
    }
    return m < (1ULL << 53);
}

void Lexer::digitSeparator(const std::string &s, std::size_t at) const {
    if (at < s.size() && s[at] == '\'' && at + 1 < s.size() &&
        std::isalnum(static_cast<unsigned char>(s[at + 1])))
        src_.fail(at, "a digit separator is C++14, and this compiler is C++11 "
                      "- write the digits with nothing between them");
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    const std::string &s = src_.text();
    std::size_t i = 0;

    auto identStart = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; };
    auto identCont  = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };

    static const char *const three[] = { "<<=", ">>=", "->*" };
    static const char *const two[] = { "==", "!=", "<=", ">=", "<<", ">>", "&&", "||",
                                       "->", "++", "--", "+=", "-=", "*=", "/=", "%=",
                                       "&=", "|=", "^=",
                                       "::", ".*" };
    static const struct { const char *digraph; const char *primary; } digraphs[] = {
        { "<%", "{" }, { "%>", "}" }, { "<:", "[" }, { ":>", "]" }
    };

    while (i < s.size()) {
        char c = s[i];

        if (std::isspace(static_cast<unsigned char>(c))) { i++; continue; }

        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') i++;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            std::size_t end = s.find("*/", i + 2);
            if (end == std::string::npos) src_.fail(i, "unterminated comment");
            i = end + 2;
            continue;
        }

        bool wide = false;
        char prefix = 0;
        if (c == 'L' && i + 1 < s.size() && (s[i + 1] == '\'' || s[i + 1] == '"')) {
            wide = true;
            i++;
            c = s[i];
        }
        // [lex.ccon], [lex.string]: u, U and u8 - u8 only on a string.
        if ((c == 'u' || c == 'U') && i + 1 < s.size() &&
            (s[i + 1] == '\'' || s[i + 1] == '"')) {
            prefix = c; wide = true; i++; c = s[i];
        } else if (c == 'u' && i + 2 < s.size() && s[i + 1] == '8' && s[i + 2] == '"') {
            prefix = '8'; i += 2; c = s[i];
        }

        if (c == '\'') {
            std::size_t start = i++;
            if (i >= s.size()) src_.fail(start, "unterminated character constant");
            long long v = 0;
            int chars = 0;

            while (i < s.size() && s[i] != '\'') {
                long long one;
                if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == 'u' || s[i + 1] == 'U')) {
                    const int digits = s[i + 1] == 'u' ? 4 : 8;
                    i += 2;
                    one = universalName(s, i, digits);
                    if (!wide && one > 0x7F)
                        src_.fail(start, "a universal character name past U+007F does not fit a char");
                } else if (s[i] == '\\') { i++; one = unescape(s, i, start, wide); }
                else one = static_cast<unsigned char>(s[i++]);
                v = wide ? one : ((v << 8) | (one & 0xff));
                chars++;
            }
            if (chars == 0) src_.fail(start, "empty character constant");
            if (prefix != 0 && chars != 1)
                src_.fail(start, "a u or U character literal holds one character - [lex.ccon]/2");

            if (!wide && chars == 1) v = static_cast<signed char>(v);
            bool isChar = (!wide && chars == 1);
            if (i >= s.size() || s[i] != '\'')
                src_.fail(start, "unterminated character constant");
            i++;
            Token t;
            t.kind = TokenKind::Num;
            t.value = v;
            t.wide = wide && prefix == 0;
            t.prefix = prefix;
            t.isChar = isChar;
            t.pos = start;
            out.push_back(std::move(t));
            continue;
        }

        if (c == '"') {
            std::size_t start = i++;
            std::string text;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\n') src_.fail(start, "unterminated string");
                if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == 'u' || s[i + 1] == 'U')) {
                    const int digits = s[i + 1] == 'u' ? 4 : 8;
                    i += 2;
                    appendUtf8(text, universalName(s, i, digits));
                } else if (s[i] == '\\') { i++; text.push_back(static_cast<char>(unescape(s, i, start))); }
                else text.push_back(s[i++]);
            }
            if (i >= s.size()) src_.fail(start, "unterminated string");
            i++;
            Token t;
            t.kind = TokenKind::Str;
            t.text = std::move(text);
            t.wide = wide && prefix == 0;
            t.prefix = prefix;
            t.pos = start;
            out.push_back(std::move(t));
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && i + 1 < s.size() &&
             std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            Token t;
            t.kind = TokenKind::Num;
            t.pos = i;
            char *stop = nullptr;

            std::size_t j = i;
            // **Refused by name because the legal neighbour is one character
            // away.** `0x` is C++98 and `0b` is C++14; without this the literal
            // lexes as `0` and an identifier, and the error lands past the digits.
            if (s[j] == '0' && j + 1 < s.size() &&
                (s[j + 1] == 'b' || s[j + 1] == 'B'))
                src_.fail(i, "a binary literal is C++14, and this compiler is "
                             "C++11 - write the value in hexadecimal");
            bool isHex = (s[j] == '0' && j + 1 < s.size() &&
                          (s[j + 1] == 'x' || s[j + 1] == 'X'));
            t.decimal = !(s[j] == '0' && j + 1 < s.size() &&
                          (s[j + 1] == 'x' || s[j + 1] == 'X' ||
                           std::isdigit(static_cast<unsigned char>(s[j + 1]))));
            bool floating = (s[j] == '.');
            if (!isHex && !floating) {
                while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) j++;
                if (j < s.size() && (s[j] == '.' || s[j] == 'e' || s[j] == 'E'))
                    floating = true;
            }

            if (floating) {
                t.isFloat = true;
                // **`strtod`, not `strtold` - a literal's value must not depend
                // on which machine built the compiler.** A host `long double`
                // rounds twice on one box and once on the next; measured, differing.
                const std::size_t from = i;
                t.dvalue = std::strtod(s.c_str() + i, &stop);
                i = static_cast<std::size_t>(stop - s.c_str());
                t.exactInDouble = exactlyADouble(s, from, i);

                digitSeparator(s, i);
                if (i < s.size() && (s[i] == 'f' || s[i] == 'F')) {
                    t.suffixF = true; i++;
                    // A `float` literal is rounded once too: through `double` it
                    // is rounded twice, 53 bits and then 24. [lex.fcon] gives the
                    // literal the value of its own type, so `strtof` is that.
                    t.dvalue = std::strtof(s.c_str() + from, nullptr);
                } else if (i < s.size() && (s[i] == 'l' || s[i] == 'L')) {
                    t.suffixL = true; i++;
                }
                out.push_back(std::move(t));
                continue;
            }

            errno = 0;
            t.value = static_cast<long long>(std::strtoull(s.c_str() + i, &stop, 0));
            if (errno == ERANGE)
                src_.fail(i, "this integer constant does not fit any type");
            i = static_cast<std::size_t>(stop - s.c_str());
            digitSeparator(s, i);

            int us = 0, ls = 0;
            while (i < s.size()) {
                if ((s[i] == 'u' || s[i] == 'U') && us == 0) {
                    t.suffixU = true; us++; i++;
                } else if ((s[i] == 'l' || s[i] == 'L') && ls < 2) {
                    ls++; i++;
                    t.suffixL = true;
                    if (ls == 2) t.suffixLL = true;
                } else {
                    break;
                }
            }
            out.push_back(std::move(t));
            continue;
        }

        if (identStart(c)) {
            std::size_t start = i;
            while (i < s.size() && identCont(s[i])) i++;
            // **A literal prefix is part of the literal, not a name in front
            // of one** - [lex.string].
            if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
                const std::string pre = s.substr(start, i - start);
                // `L` is not here: a wide literal is read further up and
                // works. What is left is the C++11 set plus the raw forms.
                if (pre == "u8")
                    src_.fail(start, "a u8 character literal is C++17, and this compiler is C++11");
                if (pre == "R" || pre == "u8R" || pre == "LR" || pre == "uR" ||
                    pre == "UR")
                    src_.fail(start, "a '" + pre + "' literal is a raw string, not "
                                "supported yet - an ordinary \"...\" is a "
                                "narrow string of char here");
            }
            Token t;
            t.text = s.substr(start, i - start);
            // [lex.digraph]/2: an alternative token *is* its primary token from
            // here on, so nothing downstream needs a rule for the spelling.
            if (const char *primary = alternativeToken(t.text)) {
                t.text = primary;
                t.kind = TokenKind::Punct;
            } else {
                t.kind = isKeyword(t.text) ? TokenKind::Keyword
                                           : TokenKind::Ident;
            }
            t.pos = start;
            out.push_back(std::move(t));
            continue;
        }

        // [lex.pptoken]/3's exception, and the reason `Foo<::Bar>` is a
        // template-id and not `Foo[:Bar>`: `<::` with neither `:` nor `>` after
        // it is a `<` on its own, and only there does `<:` fail to be a `[`.
        if (s.compare(i, 3, "<::") == 0 &&
            (i + 3 >= s.size() || (s[i + 3] != ':' && s[i + 3] != '>'))) {
            Token t; t.kind = TokenKind::Punct; t.text = "<"; t.pos = i;
            out.push_back(std::move(t));
            i++;
            continue;
        }

        // The bracket digraphs of [lex.digraph] table 2, written as the
        // punctuator each stands for - the same rule the eleven words get, and
        // none of them is ambiguous: `%` and `:` both need a right operand.
        bool matchedDigraph = false;
        for (const auto &d : digraphs) {
            if (s.compare(i, 2, d.digraph) != 0) continue;
            Token t; t.kind = TokenKind::Punct; t.text = d.primary; t.pos = i;
            out.push_back(std::move(t));
            i += 2;
            matchedDigraph = true;
            break;
        }
        if (matchedDigraph) continue;

        bool matched3 = false;
        for (const char *op : three) {
            if (s.compare(i, 3, op) == 0) {
                Token t; t.kind = TokenKind::Punct; t.text = op; t.pos = i;
                out.push_back(std::move(t)); i += 3; matched3 = true; break;
            }
        }
        if (matched3) continue;

        if (s.compare(i, 3, "...") == 0) {
            Token t;
            t.kind = TokenKind::Punct;
            t.text = "...";
            t.pos = i;
            out.push_back(std::move(t));
            i += 3;
            continue;
        }

        bool matched = false;
        for (const char *op : two) {
            if (s.compare(i, 2, op) == 0) {
                Token t;
                t.kind = TokenKind::Punct;
                t.text = op;
                t.pos = i;
                out.push_back(std::move(t));
                i += 2;
                matched = true;
                break;
            }
        }
        if (matched) continue;

        if (std::string("+-*/%()<>={},;!&[].|^~:?").find(c) != std::string::npos) {
            Token t;
            t.kind = TokenKind::Punct;
            t.text.assign(1, c);
            t.pos = i;
            out.push_back(std::move(t));
            i++;
            continue;
        }

        src_.fail(i, std::string("stray '") + c + "' in program");
    }

    Token end;
    end.kind = TokenKind::End;
    end.pos = s.size();
    out.push_back(std::move(end));
    return out;
}
