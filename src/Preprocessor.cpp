#include "Preprocessor.h"
#include "Lexer.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

// **The same header reached two ways is one file.** `#pragma once` is keyed by
// the resolved path, and `-Idir` plus a relative include can spell one file
// two ways - so the name is put through the platform's own resolver first.
static std::string canonicalPath(const std::string &p) {
    char buf[4096];
#ifdef _WIN32
    if (_fullpath(buf, p.c_str(), sizeof buf) != nullptr) return std::string(buf);
#else
    if (realpath(p.c_str(), buf) != nullptr) return std::string(buf);
#endif
    return p;
}

namespace {

bool identStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool identCont(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// `#` and `##` are spelled `%:` and `%:%:` as well - [lex.digraph] table 2.
// Answered as the length of what is there, so a caller steps over the spelling
// it actually found rather than assuming the short one.
static std::size_t hashLen(const std::string &s, std::size_t i, bool twice) {
    if (s.compare(i, twice ? 2 : 1, twice ? "##" : "#") == 0) return twice ? 2 : 1;
    if (s.compare(i, twice ? 4 : 2, twice ? "%:%:" : "%:") == 0) return twice ? 4 : 2;
    return 0;
}

std::string trim(const std::string &s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

std::vector<std::string> splitLines(const std::string &text) {
    std::vector<std::string> out;
    std::string current;
    std::size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (c == '\n') {
            if (!current.empty() && current.back() == '\\') {
                current.pop_back();
                i++;
                continue;
            }
            out.push_back(current);
            current.clear();
            i++;
            continue;
        }
        if (c == '\r') { i++; continue; }
        current.push_back(c);
        i++;
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

const int kMaxIncludeDepth = 32;

const char kSeparators[] = "/\\";

bool isAbsolute(const std::string &name) {
    if (name.empty()) return false;
    if (name[0] == '/' || name[0] == '\\') return true;
    return name.size() >= 3 && name[1] == ':' &&
           (name[2] == '/' || name[2] == '\\');
}

}

std::string Preprocessor::directoryOf(const std::string &path) {
    std::size_t slash = path.find_last_of(kSeparators);
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

std::string Preprocessor::resolveInclude(const std::string &name, bool angled,
                                         int fileIndex,
                                         std::vector<std::string> &tried) const {
    auto opens = [](const std::string &p) {
        std::FILE *fp = std::fopen(p.c_str(), "rb");
        if (!fp) return false;
        std::fclose(fp);
        return true;
    };

    if (isAbsolute(name)) {
        tried.push_back(name);
        return opens(name) ? name : std::string();
    }

    if (!angled) {
        std::string beside = directoryOf(files_[fileIndex]) + "/" + name;
        tried.push_back(beside);
        if (opens(beside)) return beside;
    }
    for (const std::string &dir : searchPath_) {
        std::string path = dir + "/" + name;
        tried.push_back(path);
        if (opens(path)) return path;
    }
    return std::string();
}

void Preprocessor::fail(int fileIndex, int lineNo, const std::string &line,
                        std::size_t column, const std::string &message) const {
    if (column > line.size()) column = line.size();
    std::string head = files_[fileIndex] + ":" + std::to_string(lineNo) + ": ";
    std::string text = head + line + "\n";
    text.append(head.size() + column, ' ');
    text += "^ " + message + "\n";
    std::fwrite(text.data(), 1, text.size(), stderr);
    std::exit(1);
}

bool Preprocessor::parentEmitting() const {
    for (std::size_t i = 0; i + 1 < conds_.size(); i++)
        if (!conds_[i].active) return false;
    return true;
}

bool Preprocessor::emitting() const {
    for (const Cond &c : conds_)
        if (!c.active) return false;
    return true;
}

void Preprocessor::emitLine(const std::string &text, int fileIndex, int lineNo) {
    out_ += text;
    out_ += '\n';
    lines_.push_back(Source::Line{ fileIndex, lineNo });
}

// [cpp.stringize]/2: the spelling of the argument, a backslash before each `"`
// and `\`, leading and trailing white space deleted - and **each run of white
// space between two preprocessing tokens replaced by a single space**.
std::string Preprocessor::stringify(const std::string &arg) {
    const std::string s = trim(arg);
    std::string out = "\"";
    for (std::size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            while (i + 1 < s.size() && std::isspace(static_cast<unsigned char>(s[i + 1]))) i++;
            out += ' ';
            continue;
        }
        if (c == '"' || c == '\'') {
            // A literal, copied through whole. Its delimiters are escaped, an
            // escape sequence inside it keeps both characters, and the closing
            // quote ends it.
            const char quote = c;
            out += '\\';
            out += c;
            for (i++; i < s.size(); i++) {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    out += "\\\\";
                    if (s[i + 1] == '"' || s[i + 1] == '\\') out += '\\';
                    out += s[i + 1];
                    i++;
                    continue;
                }
                if (s[i] == quote) { out += '\\'; out += s[i]; break; }
                out += s[i];
            }
            continue;
        }
        if (c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

std::vector<std::string> Preprocessor::collectArgs(const std::string &s, std::size_t &i,
                                                   const std::string &name,
                                                   int fileIndex, int lineNo) {
    std::vector<std::string> args;
    std::string current;
    int depth = 0;
    i++;

    for (; i < s.size(); i++) {
        char c = s[i];
        if (c == '"' || c == '\'') {
            char quote = c;
            current += c;
            i++;
            while (i < s.size()) {
                if (s[i] == '\\' && i + 1 < s.size()) { current += s[i]; current += s[i + 1]; i += 2; continue; }
                current += s[i];
                if (s[i] == quote) break;
                i++;
            }
            continue;
        }
        if (c == '(' || c == '[') { depth++; current += c; continue; }
        if (c == ']') { depth--; current += c; continue; }
        if (c == ')') {
            if (depth == 0) { i++; args.push_back(current); return args; }
            depth--;
            current += c;
            continue;
        }
        if (c == ',' && depth == 0) { args.push_back(current); current.clear(); continue; }
        current += c;
    }
    fail(fileIndex, lineNo, reportLine_, 0,
         "the call to '" + name + "' is missing its ')'");
}

std::string Preprocessor::substitute(const Macro &m, const std::vector<std::string> &args,
                                     std::vector<std::string> &busy,
                                     int fileIndex, int lineNo) {
    auto indexOf = [&m](const std::string &name) -> int {
        for (std::size_t k = 0; k < m.params.size(); k++)
            if (m.params[k] == name) return static_cast<int>(k);
        return -1;
    };

    std::string va;
    if (m.variadic) {
        for (std::size_t k = m.params.size(); k < args.size(); k++) {
            if (k > m.params.size()) va += ", ";
            va += trim(args[k]);
        }
    }
    const std::string kVaName = "__VA_ARGS__";

    const std::string &body = m.body;
    std::string out;
    std::size_t i = 0;

    while (i < body.size()) {
        if (std::size_t paste = hashLen(body, i, true)) {
            while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back())))
                out.pop_back();
            i += paste;
            while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) i++;
            if (i < body.size() && identStart(body[i])) {
                std::size_t start = i;
                while (i < body.size() && identCont(body[i])) i++;
                std::string name = body.substr(start, i - start);
                if (m.variadic && name == kVaName) {
                    if (va.empty()) {
                        while (!out.empty() &&
                               std::isspace(static_cast<unsigned char>(out.back())))
                            out.pop_back();
                        if (!out.empty() && out.back() == ',') out.pop_back();
                    } else {
                        out += va;
                    }
                    continue;
                }
                int p = indexOf(name);
                out += (p >= 0) ? trim(args[static_cast<std::size_t>(p)]) : name;
            } else if (i < body.size()) {
                out += body[i++];
            }
            continue;
        }
        if (std::size_t hash = hashLen(body, i, false)) {
            std::size_t j = i + hash;
            while (j < body.size() && std::isspace(static_cast<unsigned char>(body[j]))) j++;
            std::size_t start = j;
            while (j < body.size() && identCont(body[j])) j++;
            std::string name = body.substr(start, j - start);
            if (m.variadic && name == kVaName) {
                out += stringify(va);
                i = j;
                continue;
            }
            int p = indexOf(name);
            if (p < 0)
                fail(fileIndex, lineNo, reportLine_, 0,
                     "'#' needs a parameter of this macro after it, and '" + name +
                     "' is not one");
            out += stringify(args[static_cast<std::size_t>(p)]);
            i = j;
            continue;
        }
        if (body[i] == '"' || body[i] == '\'') {
            char quote = body[i];
            out += body[i++];
            while (i < body.size()) {
                if (body[i] == '\\' && i + 1 < body.size()) { out += body[i]; out += body[i + 1]; i += 2; continue; }
                out += body[i];
                if (body[i] == quote) { i++; break; }
                i++;
            }
            continue;
        }
        if (identStart(body[i])) {
            std::size_t start = i;
            while (i < body.size() && identCont(body[i])) i++;
            std::string name = body.substr(start, i - start);
            if (m.variadic && name == kVaName) {
                std::vector<std::string> vaBusy = busy;
                out += expandText(va, vaBusy, fileIndex, lineNo, false);
                continue;
            }
            int p = indexOf(name);
            if (p < 0) { out += name; continue; }
            std::vector<std::string> argBusy = busy;
            out += expandText(args[static_cast<std::size_t>(p)], argBusy,
                              fileIndex, lineNo, false);
            continue;
        }
        out += body[i++];
    }
    return out;
}

bool Preprocessor::hasOpenCall(const std::string &s) const {
    std::size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '"' || s[i] == '\'') {
            char quote = s[i++];
            while (i < s.size()) {
                if (s[i] == '\\' && i + 1 < s.size()) { i += 2; continue; }
                if (s[i] == quote) { i++; break; }
                i++;
            }
            continue;
        }
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') return false;
        if (!identStart(s[i])) { i++; continue; }
        std::size_t start = i;
        while (i < s.size() && identCont(s[i])) i++;
        auto it = macros_.find(s.substr(start, i - start));
        if (it == macros_.end() || !it->second.functionLike) continue;

        std::size_t j = i;
        while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) j++;
        if (j >= s.size()) return true;
        if (s[j] != '(') continue;
        int depth = 0;
        for (; j < s.size(); j++) {
            if (s[j] == '(') depth++;
            else if (s[j] == ')') { depth--; if (depth == 0) break; }
        }
        if (depth != 0) return true;
        i = j;
    }
    return false;
}

std::string Preprocessor::expandText(const std::string &s, std::vector<std::string> &busy,
                                     int fileIndex, int lineNo, bool trackComments) {
    std::string out;
    std::size_t i = 0;

    while (i < s.size()) {
        if (trackComments && inBlockComment_) {
            std::size_t end = s.find("*/", i);
            if (end == std::string::npos) { out += s.substr(i); return out; }
            out += s.substr(i, end + 2 - i);
            i = end + 2;
            inBlockComment_ = false;
            continue;
        }

        char c = s[i];

        if (trackComments && c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            out += s.substr(i);
            return out;
        }
        if (trackComments && c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            out += "/*";
            i += 2;
            inBlockComment_ = true;
            continue;
        }
        if (c == '"' || c == '\'') {
            char quote = c;
            out += c;
            i++;
            while (i < s.size()) {
                if (s[i] == '\\' && i + 1 < s.size()) { out += s[i]; out += s[i + 1]; i += 2; continue; }
                out += s[i];
                if (s[i] == quote) { i++; break; }
                i++;
            }
            continue;
        }
        if (!identStart(c)) { out += c; i++; continue; }

        std::size_t start = i;
        while (i < s.size() && identCont(s[i])) i++;
        std::string name = s.substr(start, i - start);

        if (name == "__LINE__") { out += std::to_string(lineNo); continue; }
        if (name == "__FILE__") { out += "\"" + files_[fileIndex] + "\""; continue; }

        auto it = macros_.find(name);
        if (it == macros_.end()) { out += name; continue; }

        bool recursing = false;
        for (const std::string &b : busy)
            if (b == name) { recursing = true; break; }
        if (recursing) { out += name; continue; }

        if (!it->second.functionLike) {
            busy.push_back(name);
            out += expandText(it->second.body, busy, fileIndex, lineNo, false);
            busy.pop_back();
            continue;
        }

        std::size_t j = i;
        while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) j++;
        if (j >= s.size() || s[j] != '(') { out += name; continue; }

        i = j;
        std::vector<std::string> args = collectArgs(s, i, name, fileIndex, lineNo);
        if (args.size() == 1 && trim(args[0]).empty() && it->second.params.empty())
            args.clear();
        if (it->second.variadic) {
            if (args.size() < it->second.params.size())
                fail(fileIndex, lineNo, reportLine_, 0,
                     "'" + name + "' takes at least " +
                     std::to_string(it->second.params.size()) +
                     " argument(s) before its '...', given " +
                     std::to_string(args.size()));
        } else if (args.size() != it->second.params.size()) {
            fail(fileIndex, lineNo, reportLine_, 0,
                 "'" + name + "' takes " + std::to_string(it->second.params.size()) +
                 " argument(s), given " + std::to_string(args.size()));
        }

        std::string replaced = substitute(it->second, args, busy, fileIndex, lineNo);
        busy.push_back(name);
        out += expandText(replaced, busy, fileIndex, lineNo, false);
        busy.pop_back();
    }
    return out;
}

std::string Preprocessor::expandLine(const std::string &line, int fileIndex,
                                     int lineNo) {
    std::vector<std::string> busy;
    reportLine_ = line;
    return expandText(line, busy, fileIndex, lineNo, true);
}

std::string Preprocessor::resolveDefined(const std::string &expr, int fileIndex,
                                         int lineNo, const std::string &line) {
    std::string out;
    std::size_t i = 0;
    while (i < expr.size()) {
        if (!identStart(expr[i])) { out += expr[i++]; continue; }
        std::size_t start = i;
        while (i < expr.size() && identCont(expr[i])) i++;
        std::string name = expr.substr(start, i - start);
        if (name != "defined") { out += name; continue; }

        while (i < expr.size() && std::isspace(static_cast<unsigned char>(expr[i]))) i++;
        bool paren = (i < expr.size() && expr[i] == '(');
        if (paren) {
            i++;
            while (i < expr.size() && std::isspace(static_cast<unsigned char>(expr[i]))) i++;
        }
        std::size_t nameStart = i;
        while (i < expr.size() && identCont(expr[i])) i++;
        std::string operand = expr.substr(nameStart, i - nameStart);
        if (operand.empty() || !identStart(operand[0]))
            fail(fileIndex, lineNo, line, 0, "'defined' needs a name");
        if (paren) {
            while (i < expr.size() && std::isspace(static_cast<unsigned char>(expr[i]))) i++;
            if (i >= expr.size() || expr[i] != ')')
                fail(fileIndex, lineNo, line, 0, "'defined(' is missing its ')'");
            i++;
        }
        // **These are defined, though no `#define` wrote them.**
        out += (macros_.count(operand) || isHasPredicate(operand)) ? "1" : "0";
    }
    return out;
}

// [lex.digraph]/2 reaches the conditional as well: `#if 1 and 1` is `#if 1 && 1`.
// Word by word, so a name like `android` is left alone, and quoted spans are
// copied, so a character literal is not rewritten into an operator.
static std::string spellAlternativeTokens(const std::string &expr) {
    std::string out;
    std::size_t i = 0;
    while (i < expr.size()) {
        if (expr[i] == '\'' || expr[i] == '"') {
            char quote = expr[i];
            out += expr[i++];
            while (i < expr.size() && expr[i] != quote) {
                if (expr[i] == '\\' && i + 1 < expr.size()) out += expr[i++];
                out += expr[i++];
            }
            if (i < expr.size()) out += expr[i++];
            continue;
        }
        if (!identStart(expr[i])) { out += expr[i++]; continue; }
        std::size_t start = i;
        while (i < expr.size() && identCont(expr[i])) i++;
        const std::string word = expr.substr(start, i - start);
        const char *primary = Lexer::alternativeToken(word);
        out += primary ? primary : word;
    }
    return out;
}

// **The `__has_*` predicates, resolved before expansion for `defined`'s
// reason**: `__has_include(<vector>)` holds a header name, not an expression,
// and macro-expanding it first would make nonsense of the `<` and `>`.
bool Preprocessor::isHasPredicate(const std::string &name) {
    static const char *const kAll[] = {
        "__has_builtin", "__has_feature", "__has_extension", "__has_attribute",
        "__has_cpp_attribute", "__has_declspec_attribute", "__has_warning",
        "__has_keyword", "__is_identifier", "__building_module",
        "__has_include", "__has_include_next", 0
    };
    for (int k = 0; kAll[k] != 0; k++) if (name == kAll[k]) return true;
    return false;
}

std::string Preprocessor::resolveHasChecks(const std::string &expr, int fileIndex,
                                           int lineNo, const std::string &line) {
    static const char *const kZero[] = {
        "__has_builtin", "__has_feature", "__has_extension", "__has_attribute",
        "__has_cpp_attribute", "__has_declspec_attribute", "__has_warning",
        "__has_keyword", "__building_module", 0
    };
    // **`__is_identifier` is the one that answers 1, not 0.** It asks whether
    // a token is an ordinary identifier rather than a keyword or a builtin,
    // and here everything is - cxx1 has no builtins to shadow one.
    static const char *const kOne[] = { "__is_identifier", 0 };
    std::string out;
    std::size_t i = 0;
    while (i < expr.size()) {
        if (!identStart(expr[i])) { out += expr[i++]; continue; }
        const std::size_t start = i;
        while (i < expr.size() && identCont(expr[i])) i++;
        const std::string name = expr.substr(start, i - start);

        bool zero = false, one = false;
        for (int k = 0; kZero[k] != 0; k++)
            if (name == kZero[k]) { zero = true; break; }
        for (int k = 0; kOne[k] != 0; k++)
            if (name == kOne[k]) { one = true; break; }
        if (!zero && !one && name != "__has_include" && name != "__has_include_next") {
            out += name;
            continue;
        }

        while (i < expr.size() && std::isspace(static_cast<unsigned char>(expr[i]))) i++;
        if (i >= expr.size() || expr[i] != '(') {
            // Not a call: an ordinary identifier that happens to be spelled so.
            out += name;
            continue;
        }
        // The argument, taken whole and balanced - it may hold `<`, `>`, `"`
        // and nested parentheses, none of which is an expression here.
        i++;
        const std::size_t argStart = i;
        int depth = 1;
        while (i < expr.size() && depth > 0) {
            if (expr[i] == '(') depth++;
            else if (expr[i] == ')') depth--;
            if (depth > 0) i++;
        }
        if (depth != 0)
            fail(fileIndex, lineNo, line, 0, "'" + name + "(' is missing its ')'");
        std::string arg = expr.substr(argStart, i - argStart);
        i++;                                   // past the ')'

        if (zero) { out += "0"; continue; }
        if (one) { out += "1"; continue; }

        while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg[0])))
            arg.erase(arg.begin());
        while (!arg.empty() &&
               std::isspace(static_cast<unsigned char>(arg[arg.size() - 1])))
            arg.erase(arg.size() - 1);
        bool angled = !arg.empty() && arg[0] == '<';
        if (arg.size() >= 2 &&
            ((angled && arg[arg.size() - 1] == '>') ||
             (arg[0] == '"' && arg[arg.size() - 1] == '"')))
            arg = arg.substr(1, arg.size() - 2);
        std::vector<std::string> tried;
        out += resolveInclude(arg, angled, fileIndex, tried).empty() ? "0" : "1";
    }
    return out;
}

long long Preprocessor::evalCondition(const std::string &raw, int fileIndex, int lineNo,
                                 const std::string &line) {
    std::string expanded = resolveHasChecks(raw, fileIndex, lineNo, line);
    expanded = resolveDefined(expanded, fileIndex, lineNo, line);
    expanded = expandLine(expanded, fileIndex, lineNo);
    // **And again after expansion**, because a macro may spell one.
    expanded = resolveHasChecks(expanded, fileIndex, lineNo, line);
    // After expansion, because a macro body may spell one and a keyword may not itself be a macro name.
    expanded = spellAlternativeTokens(expanded);

    struct E {
        Preprocessor *pp;
        const std::string &s;
        int fileIndex;
        int lineNo;
        const std::string &line;
        std::size_t i = 0;

        void skip() {
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
        }
        bool take(const char *op) {
            skip();
            std::size_t n = 0;
            while (op[n]) n++;
            if (s.compare(i, n, op) != 0) return false;
            if (n == 1 && i + 1 < s.size()) {
                char a = s[i], b = s[i + 1];
                if ((a == '&' && b == '&') || (a == '|' && b == '|')) return false;
                if ((a == '<' || a == '>') && (b == '=' || b == a)) return false;
                if ((a == '=' || a == '!') && b == '=') return false;
            }
            i += n;
            return true;
        }
        [[noreturn]] void bad(const std::string &m) { pp->fail(fileIndex, lineNo, line, 0, m); }

        long long primary() {
            skip();
            if (i >= s.size()) bad("this condition ends too early");
            if (take("(")) {
                long long v = cond();
                skip();
                if (!take(")")) bad("this condition is missing a ')'");
                return v;
            }
            if (s[i] == '\'') {
                i++;
                long long v = 0;
                if (i < s.size() && s[i] == '\\') {
                    i++;
                    char c = i < s.size() ? s[i++] : '0';
                    v = (c == 'n') ? 10 : (c == 't') ? 9 : (c == '0') ? 0 : c;
                } else if (i < s.size()) {
                    v = static_cast<unsigned char>(s[i++]);
                }
                if (i < s.size() && s[i] == '\'') i++;
                return v;
            }
            if (std::isdigit(static_cast<unsigned char>(s[i]))) {
                char *stop = nullptr;
                long long v = std::strtoll(s.c_str() + i, &stop, 0);
                i = static_cast<std::size_t>(stop - s.c_str());
                while (i < s.size() && (s[i] == 'u' || s[i] == 'U' ||
                                        s[i] == 'l' || s[i] == 'L')) i++;
                return v;
            }
            if (identStart(s[i])) {
                while (i < s.size() && identCont(s[i])) i++;
                return 0;
            }
            bad(std::string("this condition has a stray '") + s[i] + "'");
        }
        // **Everything below wraps rather than overflowing.** [cpp.cond] does
        // this in the widest signed type, where a result that does not fit is
        // undefined - and here that is the compiler, so it is done unsigned.
        static long long wrap(unsigned long long v) {
            return static_cast<long long>(v);
        }

        long long unary() {
            skip();
            if (take("!")) return !unary();
            if (take("~")) return ~unary();
            // Negating the most negative value is the overflow that is
            // easiest to reach and hardest to see.
            if (take("-")) return wrap(0ULL - static_cast<unsigned long long>(unary()));
            if (take("+")) return unary();
            return primary();
        }
        long long mul() {
            long long v = unary();
            for (;;) {
                skip();
                if (take("*")) {
                    const long long r = unary();
                    v = wrap(static_cast<unsigned long long>(v) *
                             static_cast<unsigned long long>(r));
                } else if (take("/")) {
                    const long long r = unary();
                    if (!r) bad("division by zero in this condition");
                    // The one division that overflows, and the one the
                    // hardware traps on rather than wrapping.
                    v = (v == (-9223372036854775807LL - 1) && r == -1) ? v : v / r;
                } else if (take("%")) {
                    const long long r = unary();
                    if (!r) bad("division by zero in this condition");
                    v = (v == (-9223372036854775807LL - 1) && r == -1) ? 0 : v % r;
                } else return v;
            }
        }
        long long add() {
            long long v = mul();
            for (;;) {
                skip();
                if (take("+")) {
                    const long long r = mul();
                    v = wrap(static_cast<unsigned long long>(v) +
                             static_cast<unsigned long long>(r));
                } else if (take("-")) {
                    const long long r = mul();
                    v = wrap(static_cast<unsigned long long>(v) -
                             static_cast<unsigned long long>(r));
                } else return v;
            }
        }
        long long shift() {
            long long v = add();
            for (;;) {
                skip();
                const bool left = take("<<");
                if (!left && !take(">>")) return v;
                const long long n = add();
                // A count outside [0, 63] is undefined rather than large, and
                // so is a left shift of a negative value - so the count is
                // named where it is wrong and the shift is done unsigned.
                if (n < 0 || n > 63)
                    bad("a shift of " + std::to_string(n) +
                        " places in this condition, where a value has 64 bits");
                v = left ? wrap(static_cast<unsigned long long>(v) << n)
                         : (v < 0 ? -(-(v + 1) >> n) - 1 : v >> n);
            }
        }
        long long rel() {
            long long v = shift();
            for (;;) {
                skip();
                if (take("<=")) v = v <= shift();
                else if (take(">=")) v = v >= shift();
                else if (take("<")) v = v < shift();
                else if (take(">")) v = v > shift();
                else return v;
            }
        }
        long long eq() {
            long long v = rel();
            for (;;) {
                skip();
                if (take("==")) v = v == rel();
                else if (take("!=")) v = v != rel();
                else return v;
            }
        }
        // **`long long`, not `long`.** A `long` is 32 bits where cl builds this
        // compiler and 64 where gcc and clang do, so `#if 0x300000002 &
        // 0xFFFFFFFF` answered differently depending on which box built it.
        long long bitAnd() { long long v = eq(); for (;;) { skip(); if (take("&")) v = v & eq(); else return v; } }
        long long bitXor() { long long v = bitAnd(); for (;;) { skip(); if (take("^")) v = v ^ bitAnd(); else return v; } }
        long long bitOr()  { long long v = bitXor(); for (;;) { skip(); if (take("|")) v = v | bitXor(); else return v; } }
        long long land()   { long long v = bitOr(); for (;;) { skip(); if (take("&&")) { long long r = bitOr(); v = (v && r); } else return v; } }
        long long lor()    { long long v = land(); for (;;) { skip(); if (take("||")) { long long r = land(); v = (v || r); } else return v; } }
        long long cond() {
            long long c = lor();
            skip();
            if (!take("?")) return c;
            long long a = cond();
            skip();
            if (!take(":")) bad("this condition is missing the ':' of a '?:'");
            long long b = cond();
            return c ? a : b;
        }
    };

    if (trim(expanded).empty())
        fail(fileIndex, lineNo, line, 0, "this directive needs a condition");

    E e{ this, expanded, fileIndex, lineNo, line };
    long long v = e.cond();
    e.skip();
    if (e.i != expanded.size())
        fail(fileIndex, lineNo, line, 0,
             "this condition has something left over: '" + expanded.substr(e.i) + "'");
    return v;
}

static std::string stripComments(const std::string &s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ) {
        if (s[i] == '"' || s[i] == '\'') {
            char quote = s[i];
            out += s[i++];
            while (i < s.size() && s[i] != quote) {
                if (s[i] == '\\' && i + 1 < s.size()) out += s[i++];
                if (i < s.size()) out += s[i++];
            }
            if (i < s.size()) out += s[i++];
            continue;
        }
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) i++;
            i = i + 1 < s.size() ? i + 2 : s.size();
            out += ' ';
            continue;
        }
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') { out += ' '; break; }
        out += s[i++];
    }
    return out;
}

void Preprocessor::directive(const std::string &line, int fileIndex, int lineNo) {
    // The introducer is one character or two - `#` or `%:` - and the caller has
    // already established that one of them is the first thing on the line.
    std::size_t i = line.find_first_not_of(" \t\v\f\r");
    if (i == std::string::npos) i = 0;
    i += hashLen(line, i, false);
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) i++;

    std::size_t nameStart = i;
    while (i < line.size() && identCont(line[i])) i++;
    std::string what = line.substr(nameStart, i - nameStart);
    std::string rest = trim(line.substr(i));

    if (what == "ifdef" || what == "ifndef") {
        // One name, a comment being whitespace by now: `#ifndef _WIN32 /* why */` asks about _WIN32.
        std::string name = trim(stripComments(rest));
        if (name.empty() || !identStart(name[0]))
            fail(fileIndex, lineNo, line, nameStart, "'#" + what + "' needs a name");
        std::size_t e = 0;
        while (e < name.size() && identCont(name[e])) e++;
        if (!trim(name.substr(e)).empty())
            fail(fileIndex, lineNo, line, nameStart, "'#" + what + "' takes one name, and '" + trim(name.substr(e)) + "' follows it");
        name = name.substr(0, e);
        // **`#ifdef __has_builtin` is a directive and not `defined()`**, and
        // libstdc++ guards its whole builtin layer with exactly that.
        bool defined = macros_.count(name) != 0 || isHasPredicate(name);
        bool want = (what == "ifdef") ? defined : !defined;
        bool on = emitting() && want;
        conds_.push_back(Cond{ on, on, false });
        return;
    }
    if (what == "if") {
        // **A comment is whitespace before a directive is executed** -
        // [lex.phases]/3 replaces it in phase 3 and directives run in phase 4
        // - and `#define`'s body already went through this.
        bool on = emitting() &&
                  evalCondition(stripComments(rest), fileIndex, lineNo, line) != 0;
        conds_.push_back(Cond{ on, on, false });
        return;
    }
    if (what == "elif") {
        if (conds_.empty())
            fail(fileIndex, lineNo, line, nameStart, "'#elif' with no '#if'");
        if (conds_.back().seenElse)
            fail(fileIndex, lineNo, line, nameStart, "'#elif' after '#else'");
        Cond &c = conds_.back();
        if (c.taken || !parentEmitting()) {
            c.active = false;
            return;
        }
        c.active = evalCondition(stripComments(rest), fileIndex, lineNo, line) != 0;
        if (c.active) c.taken = true;
        return;
    }
    if (what == "else") {
        if (conds_.empty())
            fail(fileIndex, lineNo, line, nameStart, "'#else' with no '#if'");
        if (conds_.back().seenElse)
            fail(fileIndex, lineNo, line, nameStart, "a second '#else'");
        Cond &c = conds_.back();
        c.seenElse = true;
        c.active = !c.taken && parentEmitting();
        c.taken = true;
        return;
    }
    if (what == "endif") {
        if (conds_.empty())
            fail(fileIndex, lineNo, line, nameStart, "'#endif' with no '#if'");
        conds_.pop_back();
        return;
    }
    if (!emitting()) return;

    if (what == "define") {
        std::size_t j = 0;
        while (j < rest.size() && identCont(rest[j])) j++;
        std::string name = rest.substr(0, j);
        if (name.empty() || !identStart(name[0]))
            fail(fileIndex, lineNo, line, nameStart, "'#define' needs a name");
        if (j < rest.size() && rest[j] == '(') {
            std::vector<std::string> params;
            bool variadic = false;
            std::size_t k = j + 1;
            for (;;) {
                while (k < rest.size() && std::isspace(static_cast<unsigned char>(rest[k]))) k++;
                if (k < rest.size() && rest[k] == ')') { k++; break; }
                if (rest.compare(k, 3, "...") == 0) {
                    variadic = true;
                    k += 3;
                    while (k < rest.size() &&
                           std::isspace(static_cast<unsigned char>(rest[k]))) k++;
                    if (k >= rest.size() || rest[k] != ')')
                        fail(fileIndex, lineNo, line, nameStart,
                             "'...' has to be the last thing in a parameter list");
                    k++;
                    break;
                }
                std::size_t start = k;
                while (k < rest.size() && identCont(rest[k])) k++;
                std::string param = rest.substr(start, k - start);
                if (param.empty() || !identStart(param[0]))
                    fail(fileIndex, lineNo, line, nameStart,
                         "'" + name + "' has a parameter list this is not a name in");
                for (const std::string &p : params)
                    if (p == param)
                        fail(fileIndex, lineNo, line, nameStart,
                             "'" + name + "' names the parameter '" + param + "' twice");
                params.push_back(param);
                while (k < rest.size() && std::isspace(static_cast<unsigned char>(rest[k]))) k++;
                if (rest.compare(k, 3, "...") == 0)
                    fail(fileIndex, lineNo, line, nameStart,
                         "'" + param + "...' is GNU's named variadic parameter, "
                         "which is not supported - write '...' and use __VA_ARGS__");
                if (k < rest.size() && rest[k] == ',') {
                    k++;
                    std::size_t look = k;
                    while (look < rest.size() &&
                           std::isspace(static_cast<unsigned char>(rest[look]))) look++;
                    if (look < rest.size() && rest[look] == ')')
                        fail(fileIndex, lineNo, line, nameStart,
                             "'" + name + "' has a ',' with no parameter after it");
                    continue;
                }
                if (k < rest.size() && rest[k] == ')') { k++; break; }
                fail(fileIndex, lineNo, line, nameStart,
                     "'" + name + "' has a parameter list that is missing its ')'");
            }
            Macro m;
            m.body = trim(stripComments(rest.substr(k)));
            m.functionLike = true;
            m.params = params;
            m.variadic = variadic;

            if (!variadic) {
                std::size_t v = m.body.find("__VA_ARGS__");
                if (v != std::string::npos)
                    fail(fileIndex, lineNo, line, nameStart,
                         "'" + name + "' uses __VA_ARGS__ but has no '...' in its "
                         "parameter list");
            }

            for (std::size_t q = 0; q < m.body.size(); q++) {
                if (m.body[q] == '"' || m.body[q] == '\'') {
                    char quote = m.body[q++];
                    while (q < m.body.size() && m.body[q] != quote) {
                        if (m.body[q] == '\\') q++;
                        q++;
                    }
                    continue;
                }
                std::size_t hash = hashLen(m.body, q, false);
                if (!hash) continue;
                if (std::size_t paste = hashLen(m.body, q, true)) {
                    q += paste - 1;
                    continue;
                }
                std::size_t r = q + hash;
                while (r < m.body.size() &&
                       std::isspace(static_cast<unsigned char>(m.body[r]))) r++;
                std::size_t startName = r;
                while (r < m.body.size() && identCont(m.body[r])) r++;
                std::string after = m.body.substr(startName, r - startName);
                bool isParam = (variadic && after == "__VA_ARGS__");
                for (const std::string &pn : params)
                    if (pn == after) { isParam = true; break; }
                if (!isParam)
                    fail(fileIndex, lineNo, line, nameStart,
                         after.empty()
                             ? "'#' needs a parameter of '" + name + "' after it"
                             : "'#' needs a parameter of '" + name + "' after it, and '" +
                               after + "' is not one");
                q = r - 1;
            }
            m.file = fileIndex;
            m.line = lineNo;
            macros_[name] = m;
            return;
        }
        Macro m;
        m.body = trim(stripComments(rest.substr(j)));
        m.file = fileIndex;
        m.line = lineNo;
        macros_[name] = m;
        return;
    }
    if (what == "undef") {
        if (rest.empty())
            fail(fileIndex, lineNo, line, nameStart, "'#undef' needs a name");
        macros_.erase(rest);
        return;
    }
    if (what == "include") {

        if (!rest.empty() && rest[0] != '"' && rest[0] != '<') {
            rest = expandLine(rest, fileIndex, lineNo);
            while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t'))
                rest.pop_back();
        }
        if (rest.empty() || (rest[0] != '"' && rest[0] != '<'))
            fail(fileIndex, lineNo, line, nameStart,
                 "'#include' needs \"a file\" in quotes or <a file> in angle brackets");
        bool angled = rest[0] == '<';
        char closer = angled ? '>' : '"';
        std::size_t close = rest.find(closer, 1);
        if (close == std::string::npos)
            fail(fileIndex, lineNo, line, nameStart,
                 std::string("'#include' is missing its '") + closer + "'");
        std::string name = rest.substr(1, close - 1);
        if (name.empty())
            fail(fileIndex, lineNo, line, nameStart, "'#include' names no file");

        if (depth_ >= kMaxIncludeDepth)
            fail(fileIndex, lineNo, line, nameStart,
                 "'#include' is more than " + std::to_string(kMaxIncludeDepth) +
                 " deep - a file probably includes itself");

        std::vector<std::string> tried;
        std::string path = resolveInclude(name, angled, fileIndex, tried);
        if (path.empty()) {
            std::string where;
            for (std::size_t k = 0; k < tried.size(); k++)
                where += (k ? ", " : "") + tried[k];
            fail(fileIndex, lineNo, line, nameStart,
                 "cannot find " + (angled ? "<" + name + ">" : "\"" + name + "\"") +
                 " - looked in " + where);
        }

        // A file that said `#pragma once` is read once, however often it is named.
        if (pragmaOnce_.count(canonicalPath(path)) != 0) return;

        files_.push_back(path);
        int index = static_cast<int>(files_.size()) - 1;
        depth_++;
        processFile(path, index);
        depth_--;
        return;
    }
    if (what == "error") {
        fail(fileIndex, lineNo, line, nameStart,
             rest.empty() ? "#error" : "#error " + rest);
    }

    if (what == "line") {
        std::vector<std::string> busy;
        std::string spec = trim(expandText(rest, busy, fileIndex, lineNo, false));
        std::size_t i2 = 0;
        while (i2 < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i2]))) i2++;
        if (i2 == 0)
            fail(fileIndex, lineNo, line, nameStart,
                 spec.empty() ? "'#line' needs a line number after it"
                              : "'#line' needs a line number, and '" + spec +
                                "' is not one");

        long long want = std::strtoll(spec.substr(0, i2).c_str(), nullptr, 10);
        if (want <= 0)
            fail(fileIndex, lineNo, line, nameStart,
                 "'#line' needs a positive line number, not " + spec.substr(0, i2));
        lineDelta_ = static_cast<int>(want) - (physLine_ + 1);

        std::string tail = trim(spec.substr(i2));
        if (tail.empty()) return;
        if (tail.size() < 2 || tail[0] != '"' || tail[tail.size() - 1] != '"')
            fail(fileIndex, lineNo, line, nameStart,
                 "after the line number '#line' takes a file name in quotes, "
                 "and " + tail + " is not one");

        files_.push_back(tail.substr(1, tail.size() - 2));
        fileOverride_ = static_cast<int>(files_.size()) - 1;
        return;
    }

    if (what == "pragma") {
        // The file that wrote it is not read again, whoever includes it next.
        const std::string p = trim(rest);
        if (p == "once" && fileIndex >= 0 &&
            static_cast<std::size_t>(fileIndex) < files_.size())
            pragmaOnce_.insert(canonicalPath(files_[fileIndex]));
        else if (p.compare(0, 4, "pack") == 0 &&
                 (p.size() == 4 || p[4] == '(' || std::isspace(static_cast<unsigned char>(p[4]))))
            pragmaPack(trim(p.substr(4)), fileIndex, lineNo, line, nameStart);
        return;
    }
    if (what.empty()) return;

    fail(fileIndex, lineNo, line, nameStart, "unknown directive '#" + what + "'");
}

// **`#pragma pack` is the one pragma that changes what a program computes**, and
// it was read and dropped (review of 2026-09-17 against cl, A5). cl's forms are
// read; the value in force is recorded against the next output line.
void Preprocessor::pragmaPack(const std::string &args, int fileIndex, int lineNo,
                              const std::string &line, std::size_t nameStart) {
    if (args.size() < 2 || args[0] != '(' || args[args.size() - 1] != ')')
        fail(fileIndex, lineNo, line, nameStart,
             "'#pragma pack' takes its arguments in parentheses: pack(n), pack(), "
             "pack(push, n), pack(pop)");
    std::vector<std::string> words;
    std::string inner = args.substr(1, args.size() - 2), w;
    for (std::size_t i = 0; i <= inner.size(); i++) {
        if (i == inner.size() || inner[i] == ',') { w = trim(w); if (!w.empty() || i < inner.size()) words.push_back(w); w.clear(); }
        else w += inner[i];
    }
    struct Value {
        static bool of(const std::string &t, int *n) {
            if (t.empty()) return false;
            for (std::size_t i = 0; i < t.size(); i++)
                if (!std::isdigit(static_cast<unsigned char>(t[i]))) return false;
            *n = std::atoi(t.c_str());
            return *n == 1 || *n == 2 || *n == 4 || *n == 8 || *n == 16;
        }
    };
    int n = 0;
    std::size_t wi = 0;
    if (words.empty()) pack_ = 0;
    else if (words[0] == "show") return;
    else if (words[0] == "push" || words[0] == "pop") {
        const bool push = words[0] == "push";
        std::string id;
        wi = 1;
        if (wi < words.size() && !Value::of(words[wi], &n)) id = words[wi++];
        if (push) {
            packStack_.push_back(std::make_pair(id, pack_));
            if (wi < words.size()) {
                if (!Value::of(words[wi], &n))
                    fail(fileIndex, lineNo, line, nameStart,
                         "'#pragma pack(push, " + words[wi] + ")' - the packing is 1, 2, 4, 8 or 16");
                pack_ = n;
                wi++;
            }
        } else {
            if (wi < words.size())
                fail(fileIndex, lineNo, line, nameStart,
                     "'#pragma pack(pop, " + words[wi] + ")' takes only an identifier after pop");
            // pop to the named push, or the last one; an empty stack changes nothing.
            std::size_t to = packStack_.size();
            if (!id.empty()) {
                to = 0;
                for (std::size_t i = packStack_.size(); i > 0; i--)
                    if (packStack_[i - 1].first == id) { to = i; break; }
            }
            if (to != 0) {
                pack_ = packStack_[to - 1].second;
                packStack_.resize(to - 1);
            }
        }
        if (wi < words.size())
            fail(fileIndex, lineNo, line, nameStart,
                 "'#pragma pack' has more after '" + words[wi - 1] + "' than the form takes");
    } else if (words.size() == 1 && Value::of(words[0], &n)) pack_ = n;
    else
        fail(fileIndex, lineNo, line, nameStart,
             "'#pragma pack(" + inner + ")' is not a form this compiler reads: pack(n) with n "
             "1, 2, 4, 8 or 16, pack(), pack(push [, id] [, n]), pack(pop [, id])");
    packs_.push_back(Source::Pack{ lines_.size(), pack_ });
}

void Preprocessor::processFile(const std::string &path, int fileIndex) {
    Source file = Source::fromFile(path);
    std::vector<std::string> lines = splitLines(file.text());

    std::size_t condsAtEntry = conds_.size();

    int savedDelta = lineDelta_, savedFile = fileOverride_, savedPhys = physLine_;
    lineDelta_ = 0;
    fileOverride_ = -1;

    for (std::size_t n = 0; n < lines.size(); n++) {
        const std::string &line = lines[n];
        physLine_ = static_cast<int>(n) + 1;

        int lineNo = physLine_ + lineDelta_;
        int shownFile = (fileOverride_ >= 0) ? fileOverride_ : fileIndex;

        std::size_t first = 0;
        while (first < line.size() &&
               std::isspace(static_cast<unsigned char>(line[first]))) first++;
        if (!inBlockComment_ && first < line.size() && hashLen(line, first, false)) {
            directive(line, shownFile, lineNo);
            continue;
        }

        if (!emitting()) {
            expandLine(line, shownFile, lineNo);
            continue;
        }

        std::string logical = line;
        while (hasOpenCall(logical) && n + 1 < lines.size()) {
            n++;
            logical += " ";
            logical += lines[n];
        }
        emitLine(expandLine(logical, shownFile, lineNo), shownFile, lineNo);
    }

    if (conds_.size() != condsAtEntry)
        fail(fileOverride_ >= 0 ? fileOverride_ : fileIndex,
             static_cast<int>(lines.size()) + lineDelta_,
             lines.empty() ? std::string() : lines.back(), 0,
             "a conditional in this file was never closed by '#endif'");

    lineDelta_ = savedDelta;
    fileOverride_ = savedFile;
    physLine_ = savedPhys;
}

Source Preprocessor::run() {

    for (const std::pair<std::string, std::string> &p : predefined_) {
        Macro m;
        m.body = p.second;
        macros_[p.first] = m;
    }

    files_.push_back(path_);
    processFile(path_, 0);
    Source src(path_, out_, files_, lines_);
    src.setPacks(packs_);
    return src;
}
