#include "Lexer.h"
#include "Source.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

std::string Lexer::utf8(long cp) {
    std::string out;
    appendUtf8(out, cp);
    return out;
}

std::size_t Lexer::universalAt(const std::string &s, std::size_t i, long *cp) {
    if (i + 1 >= s.size() || s[i] != '\\' || (s[i + 1] != 'u' && s[i + 1] != 'U')) return 0;
    const std::size_t digits = s[i + 1] == 'u' ? 4 : 8;
    if (i + 2 + digits > s.size()) return 0;
    for (std::size_t k = 0; k < digits; k++)
        if (!std::isxdigit(static_cast<unsigned char>(s[i + 2 + k]))) return 0;
    std::size_t at = i + 2;
    *cp = static_cast<long>(universalName(s, at, static_cast<int>(digits)));
    return 2 + digits;
}

// **C++11's Annex E, the letter**: clang applies UAX #31 to every mode as P1949's
// defect report and g++ keeps these ranges, so where the two disagree this follows
// the standard the compiler compiles - E.1 the characters allowed, E.2 not first.
static const long kAllowed[][2] = {
    {0xA8, 0xA8}, {0xAA, 0xAA}, {0xAD, 0xAD}, {0xAF, 0xAF}, {0xB2, 0xB5}, {0xB7, 0xBA},
    {0xBC, 0xBE}, {0xC0, 0xD6}, {0xD8, 0xF6}, {0xF8, 0xFF}, {0x100, 0x167F},
    {0x1681, 0x180D}, {0x180F, 0x1FFF}, {0x200B, 0x200D}, {0x202A, 0x202E},
    {0x203F, 0x2040}, {0x2054, 0x2054}, {0x2060, 0x206F}, {0x2070, 0x218F},
    {0x2460, 0x24FF}, {0x2776, 0x2793}, {0x2C00, 0x2DFF}, {0x2E80, 0x2FFF},
    {0x3004, 0x3007}, {0x3021, 0x302F}, {0x3031, 0x303F}, {0x3040, 0xD7FF},
    {0xF900, 0xFD3D}, {0xFD40, 0xFDCF}, {0xFDF0, 0xFE44}, {0xFE47, 0xFFFD},
};
static const long kNotInitially[][2] = {
    {0x300, 0x36F}, {0x1DC0, 0x1DFF}, {0x20D0, 0x20FF}, {0xFE20, 0xFE2F},
};

bool Lexer::identifierCodePoint(long cp) {
    for (const auto &r : kAllowed)
        if (cp >= r[0] && cp <= r[1]) return true;
    // E.1/8: every plane past the first, its last two code points excepted.
    return cp >= 0x10000 && cp <= 0xEFFFD && (cp & 0xFFFF) <= 0xFFFD;
}

static bool notInitially(long cp) {
    for (const auto &r : kNotInitially)
        if (cp >= r[0] && cp <= r[1]) return true;
    return false;
}

// One well-formed UTF-8 sequence at i, its code point; -1 for a malformed one.
static long decodeUtf8(const std::string &s, std::size_t &i) {
    const unsigned char b = static_cast<unsigned char>(s[i]);
    const int more = b >= 0xF0 && b <= 0xF4 ? 3 : b >= 0xE0 ? (b < 0xF0 ? 2 : -1)
                   : b >= 0xC2 ? 1 : -1;
    if (more < 0 || i + more >= s.size()) return -1;
    long cp = b & (0x3F >> more);
    for (int k = 1; k <= more; k++) {
        const unsigned char c = static_cast<unsigned char>(s[i + k]);
        if ((c & 0xC0) != 0x80) return -1;
        cp = (cp << 6) | (c & 0x3F);
    }
    static const long kLeast[] = { 0, 0x80, 0x800, 0x10000 };
    if (cp < kLeast[more] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return -1;
    i += more + 1;
    return cp;
}

void Lexer::identifierCharacter(const std::string &s, std::size_t &i, std::string *text,
                                bool first) const {
    const std::size_t at = i;
    long cp = 0;
    if (s[i] == '\\') {
        const std::size_t n = universalAt(s, i, &cp);
        if (n == 0)
            src_.fail(at, "an incomplete universal character name - '\\u' takes four hex "
                          "digits and '\\U' eight, [lex.charset]/2");
        // [lex.charset]/2: a surrogate is no character, and outside a literal neither a
        // control nor a basic source character may be named this way.
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            src_.fail(at, "'" + s.substr(at, n) + "' is not a valid universal character "
                          "name - [lex.charset]/2");
        if (cp <= 0x1F || (cp >= 0x7F && cp <= 0x9F) ||
            (cp < 0x7F && cp != '$' && cp != '@' && cp != '`'))
            src_.fail(at, "'" + s.substr(at, n) + "' names a basic source or control "
                          "character, which only a literal may - [lex.charset]/2");
        i += n;
    } else {
        cp = decodeUtf8(s, i);
        if (cp < 0)
            src_.fail(at, "this byte does not begin a UTF-8 character, and the source is read "
                          "as UTF-8");
    }
    char hex[16];
    std::snprintf(hex, sizeof hex, "%04lX", cp);
    if (!identifierCodePoint(cp))
        src_.fail(at, std::string("character <U+") + hex + "> is not allowed in an "
                      "identifier - [lex.name], C++11 Annex E.1");
    if (first && notInitially(cp))
        src_.fail(at, std::string("character <U+") + hex + "> is not allowed at the start "
                      "of an identifier - C++11 Annex E.2");
    appendUtf8(*text, cp);
}

// `R"`, `LR"`, `uR"`, `UR"` and `u8R"` - a prefix only where no name runs into it.
std::size_t Lexer::rawStringAt(const std::string &s, std::size_t i, std::string *close) {
    if (i > 0 && (std::isalnum(static_cast<unsigned char>(s[i - 1])) || s[i - 1] == '_' ||
                  static_cast<unsigned char>(s[i - 1]) >= 0x80))
        return 0;
    static const char *const kPrefixes[] = { "u8R\"", "LR\"", "uR\"", "UR\"", "R\"", 0 };
    for (int k = 0; kPrefixes[k] != 0; k++) {
        const std::size_t n = std::strlen(kPrefixes[k]);
        if (s.compare(i, n, kPrefixes[k]) != 0) continue;
        // [lex.string]/2: up to sixteen characters, none of space, (, ), \\ or a control.
        std::size_t j = i + n;
        while (j < s.size() && j - (i + n) <= 16 && s[j] != '(' && s[j] != ')' &&
               s[j] != '\\' && s[j] != '"' && !std::isspace(static_cast<unsigned char>(s[j])))
            j++;
        if (j >= s.size() || s[j] != '(' || j - (i + n) > 16) return 0;
        *close = ")" + s.substr(i + n, j - (i + n)) + "\"";
        return n;
    }
    return 0;
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

// [lex.ppnumber]: where the preprocessing number at i ends - digits, name characters, '.', and a
// sign after an e or E. Phase 7 converts it to one token, so a literal has to be all of it.
static std::size_t ppNumberEnd(const std::string &s, std::size_t i) {
    for (i++; i < s.size(); ) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        long cp = 0;
        if ((c == '+' || c == '-') && (s[i - 1] == 'e' || s[i - 1] == 'E')) i++;
        else if (std::isalnum(c) || c == '_' || c == '.' || c >= 0x80) i++;
        else if (std::size_t n = Lexer::universalAt(s, i, &cp)) i += n;
        else break;
    }
    return i;
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
    auto wideStart = [](const std::string &t, std::size_t k) {
        return static_cast<unsigned char>(t[k]) >= 0x80 ||
               (t[k] == '\\' && k + 1 < t.size() && (t[k + 1] == 'u' || t[k + 1] == 'U'));
    };
    // [lex.ext]: a name run straight onto a literal is its ud-suffix; empty where none is.
    auto suffixAt = [&](std::size_t &k) {
        std::string name;
        if (k >= s.size() || !(identStart(s[k]) || wideStart(s, k))) return name;
        const std::size_t first = k;
        while (k < s.size()) {
            if (identCont(s[k])) name.push_back(s[k++]);
            else if (wideStart(s, k)) identifierCharacter(s, k, &name, k == first);
            else break;
        }
        return name;
    };

    static const char *const three[] = { "<<=", ">>=", "->*" };
    static const char *const two[] = { "==", "!=", "<=", ">=", "<<", ">>", "&&", "||",
                                       "->", "++", "--", "+=", "-=", "*=", "/=", "%=",
                                       "&=", "|=", "^=",
                                       "::", ".*" };
    static const struct { const char *digraph; const char *primary; } digraphs[] = {
        { "<%", "{" }, { "%>", "}" }, { "<:", "[" }, { ":>", "]" }
    };

    while (i < s.size()) {
        if (!out.empty() && out.back().end == 0) out.back().end = i;
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

        // [lex.string]/4: a raw string is its characters as written, newlines included,
        // up to the `)delim"` its opening named - no escape, no splice, no comment.
        std::string rawClose;
        if (std::size_t open = rawStringAt(s, i, &rawClose)) {
            const std::size_t start = i;
            const char p0 = s[i];
            const std::size_t from = i + open + rawClose.size() - 1;
            const std::size_t to = s.find(rawClose, from);
            if (to == std::string::npos) src_.fail(start, "unterminated raw string");
            Token t;
            t.kind = TokenKind::Str;
            t.text = s.substr(from, to - from);
            t.prefix = p0 == 'u' && s[i + 1] == '8' ? '8' : (p0 == 'u' || p0 == 'U') ? p0 : 0;
            t.wide = p0 == 'L';
            t.pos = start;
            i = to + rawClose.size();
            t.udSuffix = suffixAt(i);
            out.push_back(std::move(t));
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
            t.udSuffix = suffixAt(i);
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
                } else if (s[i] == '\\') {
                    i++;
                    // [lex.string]/10: in a wide, char16_t or char32_t literal an escape is one code unit of its
                    // value - carried in the UTF-8 text the way a universal-character-name is; elsewhere one byte.
                    if (wide) appendUtf8(text, unescape(s, i, start, true));
                    else text.push_back(static_cast<char>(unescape(s, i, start)));
                } else text.push_back(s[i++]);
            }
            if (i >= s.size()) src_.fail(start, "unterminated string");
            i++;
            Token t;
            t.kind = TokenKind::Str;
            t.text = std::move(text);
            t.wide = wide && prefix == 0;
            t.prefix = prefix;
            t.pos = start;
            t.udSuffix = suffixAt(i);
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
            const std::size_t ppEnd = ppNumberEnd(s, i);
            auto whole = [&]() {
                if (i < ppEnd)
                    src_.fail(t.pos, "'" + s.substr(t.pos, ppEnd - t.pos) + "' is one preprocessing "
                                     "number and not a literal - [lex.ppnumber], [lex.icon], [lex.fcon]");
            };

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
                const std::size_t digitsEnd = i;
                const std::string suffix = suffixAt(i);
                if (suffix == "f" || suffix == "F") {
                    t.suffixF = true;
                    // A float literal is rounded once ([lex.fcon]): strtof
                    t.dvalue = std::strtof(s.c_str() + from, nullptr);
                } else if (suffix == "l" || suffix == "L") {
                    t.suffixL = true;
                } else if (!suffix.empty()) {
                    t.udSuffix = suffix;
                    t.spelling = s.substr(from, digitsEnd - from);
                }
                whole();
                out.push_back(std::move(t));
                continue;
            }

            const std::size_t from = i;
            errno = 0;
            t.value = static_cast<long long>(std::strtoull(s.c_str() + i, &stop, 0));
            const bool tooLarge = errno == ERANGE;
            i = static_cast<std::size_t>(stop - s.c_str());
            digitSeparator(s, i);

            // The standard suffixes are the whole of the name after the digits, or it is a ud-suffix.
            const std::size_t digitsEnd = i;
            const std::string suffix = suffixAt(i);
            int us = 0, ls = 0;
            std::size_t k = 0;
            for (; k < suffix.size(); k++) {
                if ((suffix[k] == 'u' || suffix[k] == 'U') && us == 0) {
                    t.suffixU = true; us++;
                } else if ((suffix[k] == 'l' || suffix[k] == 'L') && ls < 2) {
                    ls++;
                    t.suffixL = true;
                    if (ls == 2) t.suffixLL = true;
                } else {
                    break;
                }
            }
            if (k < suffix.size()) {
                t.suffixU = t.suffixL = t.suffixLL = false;
                t.udSuffix = suffix;
                t.spelling = s.substr(from, digitsEnd - from);
                t.tooLarge = tooLarge;
            } else if (tooLarge) {
                src_.fail(from, "this integer constant does not fit any type");
            }
            whole();
            out.push_back(std::move(t));
            continue;
        }

        // [lex.name]: a name may begin and go on past ASCII, in UTF-8 or as a `\u` name.
        if (identStart(c) || wideStart(s, i)) {
            std::size_t start = i;
            std::string name;
            while (i < s.size()) {
                if (identCont(s[i])) name.push_back(s[i++]);
                else if (wideStart(s, i)) identifierCharacter(s, i, &name, i == start);
                else break;
            }
            // **A literal prefix is part of the literal, not a name in front
            // of one** - [lex.string].
            if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
                const std::string &pre = name;
                // `L` is not here: a wide literal is read further up and
                // works. What is left is the C++11 set plus the raw forms.
                if (pre == "u8")
                    src_.fail(start, "a u8 character literal is C++17, and this compiler is C++11");
            }
            Token t;
            t.text = name;
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

    if (!out.empty() && out.back().end == 0) out.back().end = i;
    // [lex.string]/13, phase 6: adjacent string literals are one, of the encoding the prefixed ones
    // share - a UTF-8 one beside a wide one is ill-formed, and any other two prefixes are refused.
    char encoding = 0;
    for (std::size_t k = 0; k < out.size(); k++) {
        if (out[k].kind != TokenKind::Str) continue;
        const char e = out[k].prefix != 0 ? out[k].prefix : out[k].wide ? 'L' : 0;
        if (k == 0 || out[k - 1].kind != TokenKind::Str) { encoding = e; continue; }
        if (e != 0 && encoding != 0 && e != encoding)
            src_.fail(out[k].pos, (e == '8' && encoding == 'L') || (e == 'L' && encoding == '8')
                                      ? "a UTF-8 string literal beside a wide one is ill-formed - "
                                        "[lex.string]/13"
                                      : "adjacent string literals with different encoding "
                                        "prefixes - [lex.string]/13");
        if (e != 0) encoding = e;
    }
    Token end;
    end.kind = TokenKind::End;
    end.pos = s.size();
    out.push_back(std::move(end));
    return out;
}
