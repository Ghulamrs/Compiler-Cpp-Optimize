#include "Preprocessor.h"
#include "Lexer.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

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

// A byte past ASCII is a piece of a name's UTF-8 here; the lexer says whether Annex E allows it.
bool identStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;
}
bool identCont(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;
}

// `#` and `##` are spelled `%:` and `%:%:` as well - [lex.digraph] table 2.
bool hashText(const std::string &s) { return s == "#" || s == "%:"; }
bool pasteText(const std::string &s) { return s == "##" || s == "%:%:"; }

// [lex.trigraph] table 1: what `??` and this character stand for, or 0.
char trigraph(char c) {
    switch (c) {
    case '=': return '#';
    case '/': return '\\';
    case '\'': return '^';
    case '(': return '[';
    case ')': return ']';
    case '!': return '|';
    case '<': return '{';
    case '>': return '}';
    case '-': return '~';
    default: return 0;
    }
}

// Annex B's guideline for nested #include.
const int kMaxIncludeDepth = 256;

const char kSeparators[] = "/\\";

bool isAbsolute(const std::string &name) {
    if (name.empty()) return false;
    if (name[0] == '/' || name[0] == '\\') return true;
    return name.size() >= 3 && name[1] == ':' &&
           (name[2] == '/' || name[2] == '\\');
}

// Where the name at k ends: its characters, a universal character name among them.
std::size_t nameEnd(const std::string &s, std::size_t k) {
    for (;;) {
        long cp = 0;
        if (k < s.size() && identCont(s[k])) { k++; continue; }
        if (std::size_t u = Lexer::universalAt(s, k, &cp)) { k += u; continue; }
        return k;
    }
}

// [lex.ext]: a name run straight onto a literal is its ud-suffix, and part of the token.
std::size_t suffixEnd(const std::string &s, std::size_t k) {
    long cp = 0;
    return (k < s.size() && identStart(s[k])) || Lexer::universalAt(s, k, &cp) ? nameEnd(s, k) : k;
}

// **`caf\u00e9` and its UTF-8 are one name** - phase 1, read the other way round: a name's universal
// character names become its UTF-8, so a macro is found by either. One Annex E does not allow is
// left as written, for the lexer to refuse.
std::string utf8Name(const std::string &s) {
    if (s.find('\\') == std::string::npos) return s;
    std::string out;
    for (std::size_t i = 0; i < s.size(); ) {
        long cp = 0;
        const std::size_t n = s[i] == '\\' ? Lexer::universalAt(s, i, &cp) : 0;
        if (n != 0 && Lexer::identifierCodePoint(cp)) { out += Lexer::utf8(cp); i += n; continue; }
        out += s[i++];
    }
    return out;
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

void Preprocessor::fail(const Tok &at, const std::string &message) const {
    const std::string &file = at.file >= 0 && static_cast<std::size_t>(at.file) < files_.size()
                            ? files_[static_cast<std::size_t>(at.file)] : path_;
    std::string head = file + ":" + std::to_string(at.line) + ": ";
    std::string text;
    if (at.src >= 0) {
        // The physical line as it was written, and a caret under the token.
        const File &f = *sources_[static_cast<std::size_t>(at.src)];
        const std::size_t from = f.rawLineAt[static_cast<std::size_t>(at.phys)];
        std::size_t to = f.raw.find('\n', from);
        if (to == std::string::npos) to = f.raw.size();
        const std::size_t column = std::min(static_cast<std::size_t>(at.col), to - from);
        text = head + f.raw.substr(from, to - from) + "\n";
        text.append(head.size() + column, ' ');
        text += "^ " + message + "\n";
    } else {
        text = head + message + "\n";
    }
    { std::lock_guard<std::mutex> hold(diagnosticLock()); std::fwrite(text.data(), 1, text.size(), stderr); }
    compileFailed();  // thrown, not exited: this runs in a worker, and the pool gives the verdict (P1)
}

int Preprocessor::HideSets::intern(const std::vector<int> &v) {
    auto it = ids.find(v);
    if (it != ids.end()) return it->second;
    const int id = static_cast<int>(sets.size());
    sets.push_back(v);
    ids.emplace(v, id);
    return id;
}

int Preprocessor::HideSets::unite(int a, int b) {
    if (a == b || b == 0) return a;
    if (a == 0) return b;
    const std::pair<int, int> key(std::min(a, b), std::max(a, b));
    auto it = unions.find(key);
    if (it != unions.end()) return it->second;
    std::vector<int> v;
    std::set_union(sets[a].begin(), sets[a].end(), sets[b].begin(), sets[b].end(),
                   std::back_inserter(v));
    const int id = intern(v);
    unions.emplace(key, id);
    return id;
}

int Preprocessor::HideSets::intersect(int a, int b) {
    if (a == b) return a;
    if (a == 0 || b == 0) return 0;
    const std::pair<int, int> key(std::min(a, b), std::max(a, b));
    auto it = crossings.find(key);
    if (it != crossings.end()) return it->second;
    std::vector<int> v;
    std::set_intersection(sets[a].begin(), sets[a].end(), sets[b].begin(), sets[b].end(),
                          std::back_inserter(v));
    const int id = intern(v);
    crossings.emplace(key, id);
    return id;
}

int Preprocessor::HideSets::with(int hs, int name) {
    return unite(hs, intern(std::vector<int>(1, name)));
}

bool Preprocessor::HideSets::has(int hs, int name) const {
    return std::binary_search(sets[hs].begin(), sets[hs].end(), name);
}

int Preprocessor::nameId(const std::string &name) {
    auto it = nameIds_.find(name);
    if (it != nameIds_.end()) return it->second;
    const int id = static_cast<int>(nameIds_.size());
    nameIds_.emplace(name, id);
    return id;
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

// Phases 1 and 2 - [lex.phases]/1-2: trigraphs replaced, and each backslash with the new-line after
// it deleted. Where every character came from is kept, for the raw strings that undo both, and so
// is where every physical line begins, for the line a token is reported at.
Preprocessor::File *Preprocessor::load(const std::string &path, int index) {
    Source file = Source::fromFile(path);
    std::unique_ptr<File> f(new File);
    f->id = static_cast<int>(sources_.size());
    f->index = index;
    for (char c : file.text())
        if (c != '\r') f->raw.push_back(c);
    const std::string &raw = f->raw;
    f->text.reserve(raw.size());
    f->rawAt.reserve(raw.size() + 1);
    f->lineAt.push_back(0);
    f->rawLineAt.push_back(0);
    for (std::size_t i = 0; i < raw.size(); ) {
        char c = raw[i];
        std::size_t n = 1;
        if (c == '?' && i + 2 < raw.size() && raw[i + 1] == '?' && trigraph(raw[i + 2])) {
            c = trigraph(raw[i + 2]);
            n = 3;
        }
        if (c == '\\' && i + n < raw.size() && raw[i + n] == '\n') {
            i += n + 1;
            f->lineAt.push_back(f->text.size());
            f->rawLineAt.push_back(i);
            continue;
        }
        f->rawAt.push_back(i);
        f->text.push_back(c);
        i += n;
        if (c == '\n') {
            f->lineAt.push_back(f->text.size());
            f->rawLineAt.push_back(i);
        }
    }
    f->rawAt.push_back(raw.size());
    sources_.push_back(std::move(f));
    return sources_.back().get();
}

void Preprocessor::enter(File *f) {
    Reader r;
    r.f = f;
    r.condsAtEntry = conds_.size();
    readers_.push_back(r);
}

// White space and comments up to the next token - [lex.phases]/3: a comment is one space, and its
// characters are blanked where they stand, so a line keeps its columns. In a directive a new-line
// ends it, and is left for the line after.
bool Preprocessor::skipSpace(Reader &r, bool directive, bool *space) {
    std::string &s = r.f->text;
    while (r.pos < s.size()) {
        const char c = s[r.pos];
        if (c == '\n') {
            if (directive) return false;
            r.pos++;
            r.bol = true;
            *space = true;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f') { r.pos++; *space = true; continue; }
        if (c == '/' && r.pos + 1 < s.size() && s[r.pos + 1] == '/') {
            while (r.pos < s.size() && s[r.pos] != '\n') s[r.pos++] = ' ';
            *space = true;
            continue;
        }
        if (c == '/' && r.pos + 1 < s.size() && s[r.pos + 1] == '*') {
            const std::size_t end = s.find("*/", r.pos + 2);
            if (end == std::string::npos) {
                Tok at;
                place(r, at, r.pos);
                fail(at, "unterminated comment");
            }
            // A new-line inside one starts a line for a directive, as it does for clang and gcc.
            for (; r.pos < end + 2; r.pos++) {
                if (s[r.pos] == '\n') { if (!directive) r.bol = true; }
                else s[r.pos] = ' ';
            }
            *space = true;
            continue;
        }
        return true;
    }
    return false;
}

void Preprocessor::place(Reader &r, Tok &t, std::size_t start) {
    const File &f = *r.f;
    while (static_cast<std::size_t>(r.phys) + 1 < f.lineAt.size() &&
           f.lineAt[static_cast<std::size_t>(r.phys) + 1] <= start)
        r.phys++;
    t.src = f.id;
    t.phys = r.phys;
    t.col = static_cast<int>(start - f.lineAt[static_cast<std::size_t>(r.phys)]);
    t.file = r.fileOverride >= 0 ? r.fileOverride : f.index;
    t.line = r.phys + 1 + r.lineDelta;
}

// The end of the preprocessing token at i - [lex.pptoken]. Raw strings in a file are the reader's,
// which reads them from the characters phases 1 and 2 left alone; in text that never had those
// phases, such as a token `##` made, they are read here.
std::size_t Preprocessor::scanToken(const std::string &s, std::size_t i, Tok::Kind *kind) {
    const std::size_t n = s.size();
    auto at = [&s, n](std::size_t k) { return k < n ? s[k] : '\0'; };
    auto ucn = [&s](std::size_t k) { long cp = 0; return Lexer::universalAt(s, k, &cp); };
    auto suffixed = [&s](std::size_t k) { return suffixEnd(s, k); };
    const char c = s[i];

    std::string close;
    if (std::size_t open = Lexer::rawStringAt(s, i, &close)) {
        const std::size_t end = s.find(close, i + open);
        if (end != std::string::npos) { *kind = Tok::String; return suffixed(end + close.size()); }
    }
    std::size_t p = i;
    if (s.compare(i, 3, "u8\"") == 0) p = i + 2;
    else if ((c == 'u' || c == 'U' || c == 'L') && (at(i + 1) == '"' || at(i + 1) == '\'')) p = i + 1;
    if (at(p) == '"' || at(p) == '\'') {
        const char q = s[p];
        for (std::size_t k = p + 1; k < n && s[k] != '\n'; k++) {
            if (s[k] == '\\' && k + 1 < n && s[k + 1] != '\n') { k++; continue; }
            if (s[k] == q) {
                *kind = q == '"' ? Tok::String : Tok::Char;
                return suffixed(k + 1);
            }
        }
        // [lex.pptoken]/2: a lone quote is undefined. It is kept with the rest of its line, which
        // the lexer refuses if the line is not skipped.
        std::size_t k = p;
        while (k < n && s[k] != '\n') k++;
        *kind = Tok::Other;
        return k;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '.' && std::isdigit(static_cast<unsigned char>(at(i + 1))))) {
        // [lex.ppnumber]: digits, name characters, '.', and a sign after an e or E.
        std::size_t k = i + 1;
        for (;;) {
            if ((at(k) == '+' || at(k) == '-') && (s[k - 1] == 'e' || s[k - 1] == 'E')) { k++; continue; }
            if (k < n && (identCont(s[k]) || s[k] == '.')) { k++; continue; }
            if (std::size_t u = ucn(k)) { k += u; continue; }
            break;
        }
        *kind = Tok::Number;
        return k;
    }
    if (identStart(c) || ucn(i)) { *kind = Tok::Ident; return nameEnd(s, i); }

    *kind = Tok::Punct;
    // [lex.pptoken]/3: `<::` with neither `:` nor `>` after it is a `<` and a `::`.
    if (s.compare(i, 3, "<::") == 0 && at(i + 3) != ':' && at(i + 3) != '>') return i + 1;
    static const char *const kLong[] = {
        "%:%:", "<<=", ">>=", "->*", "...", "::", ".*", "->", "++", "--", "<<", ">>", "<=", ">=",
        "==", "!=", "&&", "||", "+=", "-=", "*=", "/=", "%=", "^=", "&=", "|=", "##", "<:", ":>",
        "<%", "%>", "%:", 0
    };
    for (int k = 0; kLong[k] != 0; k++) {
        const std::size_t len = std::strlen(kLong[k]);
        if (s.compare(i, len, kLong[k]) == 0) return i + len;
    }
    if (c != '\0' && std::strchr("{}[]#();:?.+-*/%^&|~!=<>,", c) != nullptr) return i + 1;
    *kind = Tok::Other;
    return i + 1;
}

std::vector<Preprocessor::Tok> Preprocessor::tokenize(const std::string &text) {
    std::vector<Tok> out;
    bool space = false;
    for (std::size_t i = 0; i < text.size(); ) {
        if (std::isspace(static_cast<unsigned char>(text[i]))) { space = true; i++; continue; }
        Tok t;
        t.space = space;
        space = false;
        const std::size_t e = scanToken(text, i, &t.kind);
        t.text = text.substr(i, e - i);
        if (t.kind == Tok::Ident) {
            t.text = utf8Name(t.text);
            if (Lexer::alternativeToken(t.text)) t.kind = Tok::Punct;
        }
        out.push_back(std::move(t));
        i = e;
    }
    return out;
}

// [lex.pptoken]/3: a raw string's characters are the ones phases 1 and 2 had not touched - its
// trigraphs and splices are put back - so it is read from the file as written, to its `)delim"`.
Preprocessor::Tok Preprocessor::lexRawString(Reader &r, std::size_t start, std::size_t quote) {
    const File &f = *r.f;
    const std::string &raw = f.raw;
    Tok t;
    place(r, t, start);
    const std::size_t open = f.rawAt[quote] + 1;
    std::size_t d = open;
    while (d < raw.size() && d - open <= 16 && raw[d] != '(' && raw[d] != ')' && raw[d] != '\\' &&
           raw[d] != '"' && !std::isspace(static_cast<unsigned char>(raw[d])))
        d++;
    if (d >= raw.size() || raw[d] != '(' || d - open > 16) {
        if (!emitting()) { t.kind = Tok::End; return t; }
        fail(t, "a raw string's delimiter is at most sixteen characters, none of them a space, "
                "'(', ')' or '\\', and a '(' follows it - [lex.string]/2");
    }
    const std::string close = ")" + raw.substr(open, d - open) + "\"";
    const std::size_t end = raw.find(close, d + 1);
    if (end == std::string::npos) fail(t, "unterminated raw string");
    const std::size_t rawEnd = end + close.size();
    std::size_t k = static_cast<std::size_t>(
        std::lower_bound(f.rawAt.begin(), f.rawAt.end(), rawEnd) - f.rawAt.begin());
    t.kind = Tok::String;
    t.text = f.text.substr(start, quote + 1 - start) + raw.substr(open, rawEnd - open);
    const std::size_t suffix = k;
    k = suffixEnd(f.text, k);
    t.text += f.text.substr(suffix, k - suffix);
    r.pos = k;
    return t;
}

// Phase 3 - [lex.pptoken]: the next preprocessing token of the file, or End at its end (and in a
// directive at the end of the line).
Preprocessor::Tok Preprocessor::lex(Reader &r, bool directive) {
    bool space = false;
    const bool more = skipSpace(r, directive, &space);
    Tok t;
    if (!more) {
        place(r, t, r.pos);
        t.kind = Tok::End;
        return t;
    }
    const std::string &s = r.f->text;
    const std::size_t start = r.pos;
    std::size_t quote = 0;
    static const char *const kRaw[] = { "R\"", "u8R\"", "uR\"", "UR\"", "LR\"", 0 };
    for (int k = 0; kRaw[k] != 0; k++)
        if (s.compare(start, std::strlen(kRaw[k]), kRaw[k]) == 0) {
            quote = start + std::strlen(kRaw[k]) - 1;
            break;
        }
    if (quote != 0) {
        t = lexRawString(r, start, quote);
        if (t.kind == Tok::End) quote = 0;
    }
    if (quote == 0) {
        place(r, t, start);
        const std::size_t end = scanToken(s, start, &t.kind);
        t.text = s.substr(start, end - start);
        if (t.kind == Tok::Ident) {
            t.text = utf8Name(t.text);
            if (Lexer::alternativeToken(t.text)) t.kind = Tok::Punct;
        }
        r.pos = end;
    }
    t.space = space;
    t.bol = r.bol;
    const std::size_t from = std::max(r.prevEnd, r.f->lineAt[static_cast<std::size_t>(t.phys)]);
    if (start > from) {
        t.ws = s.data() + from;
        t.wsLen = static_cast<unsigned>(start - from);
    }
    r.prevEnd = r.pos;
    r.bol = false;
    return t;
}

std::vector<Preprocessor::Tok> Preprocessor::restOfLine(Reader &r) {
    std::vector<Tok> out;
    for (;;) {
        Tok t = lex(r, true);
        if (t.kind == Tok::End) return out;
        out.push_back(std::move(t));
    }
}

std::string Preprocessor::spell(const std::vector<Tok> &toks, std::size_t from) {
    std::string out;
    for (std::size_t k = from; k < toks.size(); k++) {
        if (k > from && toks[k].space) out += ' ';
        out += toks[k].text;
    }
    return out;
}

// The file's next token for the text: directives are executed on the way, and a skipped group's
// lines are read only for the directives in them - [cpp.cond]/6.
Preprocessor::Tok Preprocessor::fileNext() {
    for (;;) {
        Reader &r = readers_.back();
        Tok t;
        if (r.hasAhead) {
            t = std::move(r.ahead);
            r.hasAhead = false;
        } else {
            t = lex(r, false);
        }
        if (t.kind == Tok::End) return t;
        if (t.bol && t.kind == Tok::Punct && hashText(t.text)) { directive(); continue; }
        if (!emitting()) continue;
        return t;
    }
}

// The token after this one, for a function-like macro's name to look for its '(' - not across the
// end of the file ([lex.phases]/1, phase 4), and a directive is not one.
const Preprocessor::Tok &Preprocessor::filePeek() {
    Reader &r = readers_.back();
    if (!r.hasAhead) {
        r.ahead = lex(r, false);
        r.hasAhead = true;
    }
    return r.ahead;
}

void Preprocessor::finishFile() {
    if (conds_.size() != readers_.back().condsAtEntry)
        fail(conds_.back().at, "a conditional in this file was never closed by '#endif'");
    readers_.pop_back();
}

// [cpp.predefined]: the names a program may neither define nor undefine - /4.
bool Preprocessor::isPredefinedName(const std::string &name) {
    static const char *const kAll[] = {
        "__cplusplus", "__DATE__", "__FILE__", "__LINE__", "__STDC_HOSTED__", "__TIME__",
        "__STDC__", "__STDC_MB_MIGHT_NEQ_WC__", "__STDC_VERSION__", "__STDC_ISO_10646__",
        "__STDCPP_STRICT_POINTER_SAFETY__", "__STDCPP_THREADS__", 0
    };
    for (int k = 0; kAll[k] != 0; k++) if (name == kAll[k]) return true;
    return false;
}

void Preprocessor::checkName(const Tok &n, const char *what) const {
    if (n.kind == Tok::Punct && Lexer::alternativeToken(n.text))
        fail(n, "'" + n.text + "' is an operator in C++ and not a name - [lex.digraph]/2, "
                "so '#" + what + "' cannot take it");
    if (n.kind != Tok::Ident) fail(n, std::string("'#") + what + "' needs a name");
    if (n.text == "defined")
        fail(n, std::string("'defined' cannot be the name of a macro - [cpp.predefined]/4, so '#") +
                what + "' cannot take it");
    if (n.text == "__VA_ARGS__")
        fail(n, std::string("__VA_ARGS__ names a macro's variable arguments, and '#") + what +
                "' cannot take it - [cpp.replace]/5");
    if (isPredefinedName(n.text) && (macros_.count(n.text) != 0 || isDynamicMacro(n.text)))
        fail(n, "'" + n.text + "' is predefined, and [cpp.predefined]/4 does not let '#" + what +
                "' change it");
}

void Preprocessor::directive() {
    Reader &r = readers_.back();
    const Tok name = lex(r, true);
    if (name.kind == Tok::End) return;   // the null directive, [cpp.null]
    const std::string what = name.kind == Tok::Ident ? name.text : std::string();
    if (what == "if" || what == "ifdef" || what == "ifndef" || what == "elif" ||
        what == "else" || what == "endif") {
        conditional(what, name, r);
        return;
    }
    if (!emitting()) {
        restOfLine(r);
        return;
    }
    if (what == "define") {
        define(restOfLine(r), name);
        return;
    }
    if (what == "undef") {
        const std::vector<Tok> rest = restOfLine(r);
        if (rest.empty()) fail(name, "'#undef' needs a name");
        checkName(rest[0], "undef");
        if (rest.size() > 1)
            fail(rest[1], "'#undef' takes one name, and '" + spell(rest, 1) + "' follows it");
        macros_.erase(rest[0].text);
        return;
    }
    if (what == "include") {
        include(name, r);
        return;
    }
    if (what == "line") {
        const std::vector<Tok> rest = restOfLine(r);
        // The next line is the physical line after the new-line this one ends at.
        const int next = r.phys + 2;
        auto digits = [](const Tok &t) {
            if (t.kind != Tok::Number) return false;
            for (char c : t.text) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            return true;
        };
        // [cpp.line]/5: a form that is neither of the two is macro-replaced first.
        std::vector<Tok> spec = rest;
        if (spec.empty() || !digits(spec[0]) || spec.size() > 2 ||
            (spec.size() == 2 && spec[1].kind != Tok::String))
            spec = expandAll(rest, false);
        if (spec.empty() || !digits(spec[0]))
            fail(name, spec.empty() ? "'#line' needs a line number after it"
                                    : "'#line' needs a line number, and '" + spell(spec) +
                                      "' is not one");
        // [cpp.line]/3: decimal whatever its first digit, and from 1 to 2147483647.
        long long want = 0;
        for (char c : spec[0].text) {
            want = want * 10 + (c - '0');
            if (want > 2147483647LL) break;
        }
        if (want <= 0 || want > 2147483647LL)
            fail(spec[0], "'#line' takes a line number from 1 to 2147483647, and " +
                          spec[0].text + " is not one - [cpp.line]/3");
        r.lineDelta = static_cast<int>(want) - next;
        if (spec.size() == 1) return;
        const std::string &tail = spec[1].text;
        if (spec.size() != 2 || spec[1].kind != Tok::String || tail.size() < 2 || tail[0] != '"' ||
            tail[tail.size() - 1] != '"')
            fail(name, "after the line number '#line' takes a file name in quotes, and " +
                       spell(spec, 1) + " is not one");
        files_.push_back(tail.substr(1, tail.size() - 2));
        r.fileOverride = static_cast<int>(files_.size()) - 1;
        return;
    }
    if (what == "error") {
        const std::vector<Tok> rest = restOfLine(r);
        fail(name, rest.empty() ? "#error" : "#error " + spell(rest));
    }
    if (what == "pragma") {
        pragma(spell(restOfLine(r)), name);
        return;
    }
    fail(name, "unknown directive '#" + name.text + "'");
}

void Preprocessor::conditional(const std::string &what, const Tok &name, Reader &r) {
    const std::vector<Tok> rest = restOfLine(r);
    if (what == "ifdef" || what == "ifndef") {
        if (!emitting()) { conds_.push_back(Cond{ false, true, false, name }); return; }
        // One name, a comment being white space by now: `#ifndef _WIN32 /* why */` asks about _WIN32.
        if (rest.empty() || rest[0].kind != Tok::Ident) fail(name, "'#" + what + "' needs a name");
        if (rest.size() > 1)
            fail(rest[1], "'#" + what + "' takes one name, and '" + spell(rest, 1) + "' follows it");
        // **`#ifdef __has_builtin` is a directive and not `defined()`**, and
        // libstdc++ guards its whole builtin layer with exactly that.
        const std::string &n = rest[0].text;
        const bool defined = macros_.count(n) != 0 || isHasPredicate(n) || isDynamicMacro(n);
        const bool on = (what == "ifdef") == defined;
        conds_.push_back(Cond{ on, on, false, name });
        return;
    }
    if (what == "if") {
        // [cpp.cond]/6: a skipped group's `#if` is not evaluated.
        if (!emitting()) { conds_.push_back(Cond{ false, true, false, name }); return; }
        const bool on = evalCondition(rest, name) != 0;
        conds_.push_back(Cond{ on, on, false, name });
        return;
    }
    if (conds_.size() <= r.condsAtEntry) fail(name, "'#" + what + "' with no '#if'");
    if (what == "elif") {
        if (conds_.back().seenElse) fail(name, "'#elif' after '#else'");
        if (conds_.back().taken || !parentEmitting()) {
            conds_.back().active = false;
            return;
        }
        const bool on = evalCondition(rest, name) != 0;
        conds_.back().active = on;
        if (on) conds_.back().taken = true;
        return;
    }
    if (parentEmitting() && !rest.empty())
        fail(rest[0], "'#" + what + "' takes nothing after it, and '" + spell(rest) + "' follows it");
    if (what == "else") {
        Cond &c = conds_.back();
        if (c.seenElse) fail(name, "a second '#else'");
        c.seenElse = true;
        c.active = !c.taken && parentEmitting();
        c.taken = true;
        return;
    }
    conds_.pop_back();
}

void Preprocessor::define(const std::vector<Tok> &line, const Tok &at) {
    if (line.empty()) fail(at, "'#define' needs a name");
    const Tok &n = line[0];
    checkName(n, "define");
    const std::string &name = n.text;
    Macro m;
    std::size_t k = 1;
    if (k < line.size() && line[k].is("(") && !line[k].space) {
        m.functionLike = true;
        k++;
        for (;;) {
            if (k >= line.size())
                fail(n, "'" + name + "' has a parameter list that is missing its ')'");
            const Tok &p = line[k];
            if (p.is(")") && m.params.empty()) { k++; break; }
            if (p.is("...")) {
                m.variadic = true;
                k++;
                if (k >= line.size() || !line[k].is(")"))
                    fail(p, "'...' has to be the last thing in a parameter list");
                k++;
                break;
            }
            if (p.kind != Tok::Ident)
                fail(p, "'" + name + "' has a parameter list this is not a name in");
            if (p.text == "__VA_ARGS__")
                fail(p, "__VA_ARGS__ names the variable arguments and cannot be a parameter - "
                        "[cpp.replace]/5");
            for (const std::string &q : m.params)
                if (q == p.text) fail(p, "'" + name + "' names the parameter '" + q + "' twice");
            m.params.push_back(p.text);
            k++;
            if (k < line.size() && line[k].is("..."))
                fail(line[k], "'" + p.text + "...' is GNU's named variadic parameter, "
                              "which is not supported - write '...' and use __VA_ARGS__");
            if (k < line.size() && line[k].is(",")) {
                k++;
                if (k < line.size() && line[k].is(")"))
                    fail(line[k], "'" + name + "' has a ',' with no parameter after it");
                continue;
            }
            if (k < line.size() && line[k].is(")")) { k++; break; }
            fail(n, "'" + name + "' has a parameter list that is missing its ')'");
        }
    } else if (k < line.size() && !line[k].space) {
        fail(line[k], "'" + name + "' needs white space between its name and what replaces it - "
                      "[cpp.replace]/3");
    }
    for (; k < line.size(); k++) {
        Tok b = line[k];
        b.ws = nullptr;
        b.wsLen = 0;
        b.bol = false;
        b.pasteOp = b.kind == Tok::Punct && pasteText(b.text);
        m.body.push_back(std::move(b));
    }
    if (!m.body.empty()) m.body[0].space = false;
    if (!m.body.empty() && (m.body.front().pasteOp || m.body.back().pasteOp))
        fail(m.body.front().pasteOp ? m.body.front() : m.body.back(),
             "'##' cannot begin or end a replacement list - [cpp.concat]/1");
    auto isParam = [&m](const Tok &t) {
        if (t.kind != Tok::Ident) return false;
        if (m.variadic && t.text == "__VA_ARGS__") return true;
        for (const std::string &p : m.params) if (p == t.text) return true;
        return false;
    };
    for (std::size_t q = 0; q < m.body.size(); q++) {
        const Tok &b = m.body[q];
        if (b.kind == Tok::Ident && b.text == "__VA_ARGS__" && !m.variadic)
            fail(b, "'" + name + "' uses __VA_ARGS__ but has no '...' in its parameter list");
        if (m.functionLike && b.kind == Tok::Punct && hashText(b.text) &&
            !(q + 1 < m.body.size() && isParam(m.body[q + 1])))
            fail(b, q + 1 < m.body.size()
                        ? "'#' needs a parameter of '" + name + "' after it, and '" +
                          m.body[q + 1].text + "' is not one"
                        : "'#' needs a parameter of '" + name + "' after it");
    }
    m.id = nameId(name);
    m.file = at.file;
    m.line = at.line;
    // [cpp.replace]/2: a macro is defined again only as it was - the same parameters and the same
    // replacement list, token for token and with white space in the same places.
    auto it = macros_.find(name);
    if (it != macros_.end()) {
        const Macro &o = *it->second;
        bool same = o.functionLike == m.functionLike && o.variadic == m.variadic &&
                    o.params == m.params && o.body.size() == m.body.size();
        for (std::size_t q = 0; same && q < m.body.size(); q++)
            same = o.body[q].kind == m.body[q].kind && o.body[q].text == m.body[q].text &&
                   (q == 0 || o.body[q].space == m.body[q].space);
        if (!same)
            fail(n, "'" + name + "' is a macro already, defined differently " +
                    (o.line == 0 ? std::string("by the compiler or the command line")
                                 : "at " + files_[static_cast<std::size_t>(o.file)] + ":" +
                                   std::to_string(o.line)) +
                    " - [cpp.replace]/2");
    }
    macros_[name] = std::make_shared<const Macro>(std::move(m));
}

void Preprocessor::include(const Tok &name, Reader &r) {
    // [cpp.include]/1: a header name is read as one, not as tokens, where it is written out.
    std::string header;
    bool angled = false, spelled = false;
    bool space = false;
    if (skipSpace(r, true, &space)) {
        const std::string &s = r.f->text;
        const char open = s[r.pos];
        if (open == '<' || open == '"') {
            const char closer = open == '<' ? '>' : '"';
            std::size_t e = r.pos + 1;
            while (e < s.size() && s[e] != closer && s[e] != '\n') e++;
            if (e < s.size() && s[e] == closer) {
                header = s.substr(r.pos + 1, e - r.pos - 1);
                angled = open == '<';
                spelled = true;
                r.pos = e + 1;
                r.prevEnd = r.pos;
            }
        }
    }
    const std::vector<Tok> rest = restOfLine(r);
    if (spelled) {
        if (!rest.empty())
            fail(rest[0], "'#include' takes one file, and '" + spell(rest) + "' follows it");
    } else {
        // [cpp.include]/4: any other form is macro-replaced, and has to come out as one of the two.
        const std::vector<Tok> e = expandAll(rest, false);
        if (e.size() == 1 && e[0].kind == Tok::String && e[0].text.size() >= 2 &&
            e[0].text[0] == '"' && e[0].text[e[0].text.size() - 1] == '"') {
            header = e[0].text.substr(1, e[0].text.size() - 2);
        } else if (!e.empty() && e[0].is("<")) {
            std::size_t k = 1;
            for (; k < e.size() && !e[k].is(">"); k++) {
                if (k > 1 && e[k].space) header += ' ';
                header += e[k].text;
            }
            if (k >= e.size()) fail(name, "'#include' is missing its '>'");
            if (k + 1 < e.size())
                fail(e[k + 1], "'#include' takes one file, and '" + spell(e, k + 1) + "' follows it");
            angled = true;
        } else {
            fail(name, "'#include' needs \"a file\" in quotes or <a file> in angle brackets");
        }
    }
    if (header.empty()) fail(name, "'#include' names no file");

    if (readers_.size() > static_cast<std::size_t>(kMaxIncludeDepth))
        fail(name, "'#include' is more than " + std::to_string(kMaxIncludeDepth) +
                   " deep - a file probably includes itself");

    std::vector<std::string> tried;
    std::string path = resolveInclude(header, angled, r.f->index, tried);
    if (path.empty() && angled && isStandardHeader(header))
        fail(name, "the standard header <" + header +
                   "> is not provided by this compiler yet - docs/EXCLUSIONS.md lists "
                   "what is missing from include/");
    if (path.empty()) {
        std::string where;
        for (std::size_t k = 0; k < tried.size(); k++)
            where += (k ? ", " : "") + tried[k];
        fail(name, "cannot find " + (angled ? "<" + header + ">" : "\"" + header + "\"") +
                   " - looked in " + where);
    }

    // A file that said `#pragma once` is read once, however often it is named.
    if (pragmaOnce_.count(canonicalPath(path)) != 0) return;

    files_.push_back(path);
    enter(load(path, static_cast<int>(files_.size()) - 1));
}

// `#pragma once`, `#pragma pack(...)`; any other pragma is read and dropped, as cl drops the ones
// it does not know. `_Pragma("...")` arrives here too, destringized ([cpp.pragma.op]).
void Preprocessor::pragma(const std::string &spec, const Tok &at) {
    // The file that wrote it is not read again, whoever includes it next.
    std::string p = spec;
    while (!p.empty() && std::isspace(static_cast<unsigned char>(p[0]))) p.erase(0, 1);
    while (!p.empty() && std::isspace(static_cast<unsigned char>(p[p.size() - 1]))) p.erase(p.size() - 1);
    if (p == "once")
        pragmaOnce_.insert(canonicalPath(files_[static_cast<std::size_t>(readers_.back().f->index)]));
    else if (p.compare(0, 4, "pack") == 0 &&
             (p.size() == 4 || p[4] == '(' || std::isspace(static_cast<unsigned char>(p[4])))) {
        std::string args = p.substr(4);
        while (!args.empty() && std::isspace(static_cast<unsigned char>(args[0]))) args.erase(0, 1);
        pragmaPack(args, at);
    }
}

// **`#pragma pack` is the one pragma that changes what a program computes**, and
// it was read and dropped (review of 2026-09-17 against cl, A5). cl's forms are
// read; the value in force is recorded against the next output line.
void Preprocessor::pragmaPack(const std::string &args, const Tok &at) {
    auto trim = [](std::string s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s[0]))) s.erase(0, 1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s[s.size() - 1]))) s.erase(s.size() - 1);
        return s;
    };
    if (args.size() < 2 || args[0] != '(' || args[args.size() - 1] != ')')
        fail(at, "'#pragma pack' takes its arguments in parentheses: pack(n), pack(), "
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
                    fail(at, "'#pragma pack(push, " + words[wi] + ")' - the packing is 1, 2, 4, 8 or 16");
                pack_ = n;
                wi++;
            }
        } else {
            if (wi < words.size())
                fail(at, "'#pragma pack(pop, " + words[wi] + ")' takes only an identifier after pop");
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
            fail(at, "'#pragma pack' has more after '" + words[wi - 1] + "' than the form takes");
    } else if (words.size() == 1 && Value::of(words[0], &n)) pack_ = n;
    else
        fail(at, "'#pragma pack(" + inner + ")' is not a form this compiler reads: pack(n) with n "
                 "1, 2, 4, 8 or 16, pack(), pack(push [, id] [, n]), pack(pop [, id])");
    // `_Pragma` in a line the text has begun takes that line; otherwise the lines after.
    std::size_t from = lines_.size();
    if (pragmaInLine_ && from > 0 && outFile_ == at.file && outLine_ == at.line) from--;
    packs_.push_back(Source::Pack{ from, pack_ });
}

// [headers] table 14's C++11 library headers and the C ones it lists beside them.
bool Preprocessor::isStandardHeader(const std::string &name) {
    static const char *const kAll[] = {
        "algorithm", "array", "atomic", "bitset", "chrono", "codecvt", "complex",
        "condition_variable", "deque", "exception", "forward_list", "fstream", "functional",
        "future", "initializer_list", "iomanip", "ios", "iosfwd", "iostream", "istream",
        "iterator", "limits", "list", "locale", "map", "memory", "mutex", "new", "numeric",
        "ostream", "queue", "random", "ratio", "regex", "scoped_allocator", "set", "sstream",
        "stack", "stdexcept", "streambuf", "string", "strstream", "system_error", "thread",
        "tuple", "type_traits", "typeindex", "typeinfo", "unordered_map", "unordered_set",
        "utility", "valarray", "vector", "cassert", "ccomplex", "cctype", "cerrno", "cfenv",
        "cfloat", "cinttypes", "ciso646", "climits", "clocale", "cmath", "csetjmp", "csignal",
        "cstdalign", "cstdarg", "cstdbool", "cstddef", "cstdint", "cstdio", "cstdlib",
        "cstring", "ctgmath", "ctime", "cuchar", "cwchar", "cwctype", 0
    };
    for (int k = 0; kAll[k] != 0; k++) if (name == kAll[k]) return true;
    return false;
}

// The macros no `#define` writes, whose value is computed where they are expanded.
bool Preprocessor::isDynamicMacro(const std::string &name) {
    return name == "__LINE__" || name == "__FILE__" || name == "__COUNTER__";
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

Preprocessor::Tok Preprocessor::hasCheck(Ctx &c, const Tok &t) {
    get(c);                                   // the '('
    // The argument, taken whole and balanced - it may hold `<`, `>`, `"`
    // and nested parentheses, none of which is an expression here.
    std::vector<Tok> arg;
    for (int depth = 1;;) {
        Tok a = get(c);
        if (a.kind == Tok::End) fail(t, "'" + t.text + "(' is missing its ')'");
        if (a.is("(")) depth++;
        else if (a.is(")") && --depth == 0) break;
        arg.push_back(std::move(a));
    }
    // **`__is_identifier` is the one that answers 1, not 0.** It asks whether
    // a token is an ordinary identifier rather than a keyword or a builtin,
    // and here everything is - cxx1 has no builtins to shadow one.
    bool yes = t.text == "__is_identifier";
    if (t.text == "__has_include" || t.text == "__has_include_next") {
        if (!arg.empty() && arg[0].kind != Tok::String && !arg[0].is("<")) arg = expandAll(arg, false);
        std::string header;
        bool angled = false;
        if (arg.size() == 1 && arg[0].kind == Tok::String && arg[0].text.size() >= 2 &&
            arg[0].text[0] == '"') {
            header = arg[0].text.substr(1, arg[0].text.size() - 2);
        } else if (arg.size() >= 2 && arg[0].is("<") && arg.back().is(">")) {
            angled = true;
            for (std::size_t k = 1; k + 1 < arg.size(); k++) {
                if (k > 1 && arg[k].space) header += ' ';
                header += arg[k].text;
            }
        } else {
            fail(t, "'" + t.text + "' takes \"a file\" in quotes or <a file> in angle brackets");
        }
        std::vector<std::string> tried;
        yes = !resolveInclude(header, angled, readers_.back().f->index, tried).empty();
    }
    Tok v = t;
    v.kind = Tok::Number;
    v.text = yes ? "1" : "0";
    return v;
}

// [cpp.stringize]/2: the spellings, one space where there was white space between two tokens and
// none at either end, and a backslash before each `"` and `\` of a character or string literal.
Preprocessor::Tok Preprocessor::stringify(const std::vector<Tok> &arg) {
    std::string s = "\"";
    for (std::size_t k = 0; k < arg.size(); k++) {
        const Tok &t = arg[k];
        if (k > 0 && t.space) s += ' ';
        if (t.kind == Tok::String || t.kind == Tok::Char) {
            for (char ch : t.text) {
                if (ch == '"' || ch == '\\') s += '\\';
                s += ch;
            }
        } else {
            s += t.text;
        }
    }
    s += '"';
    Tok r;
    r.kind = Tok::String;
    r.text = s;
    return r;
}

// [cpp.concat]/3: the two spellings as one, which has to be one preprocessing token; a placemarker
// gives the other operand.
Preprocessor::Tok Preprocessor::paste(const Tok &lhs, const Tok &rhs, const Tok &at) {
    if (lhs.kind == Tok::Placemarker) {
        Tok t = rhs;
        t.space = lhs.space;
        return t;
    }
    if (rhs.kind == Tok::Placemarker) return lhs;
    const std::string s = lhs.text + rhs.text;
    Tok::Kind kind;
    if (scanToken(s, 0, &kind) != s.size() || s.compare(0, 2, "//") == 0 ||
        s.compare(0, 2, "/*") == 0 || kind == Tok::Other)
        fail(at, "pasting '" + lhs.text + "' and '" + rhs.text + "' gives '" + s +
                 "', which is not one preprocessing token - [cpp.concat]/3");
    Tok t = lhs;
    t.kind = kind;
    t.text = s;
    t.painted = false;
    t.hide = hide_.intersect(lhs.hide, rhs.hide);
    if (kind == Tok::Ident) {
        t.text = utf8Name(s);
        if (Lexer::alternativeToken(t.text)) t.kind = Tok::Punct;
    }
    return t;
}

std::vector<std::vector<Preprocessor::Tok> > Preprocessor::collectArgs(Ctx &c, const Macro &m,
                                                                       const Tok &name, Tok *close) {
    std::vector<std::vector<Tok> > args(1);
    for (int depth = 0;;) {
        Tok t = get(c);
        if (t.kind == Tok::End) fail(name, "the call to '" + name.text + "' is missing its ')'");
        if (t.is("(")) depth++;
        else if (t.is(")")) {
            if (depth == 0) { *close = std::move(t); break; }
            depth--;
        } else if (t.is(",") && depth == 0 && !(m.variadic && args.size() > m.params.size())) {
            args.emplace_back();
            continue;
        }
        args.back().push_back(std::move(t));
    }
    if (args.size() == 1 && args[0].empty() && m.params.empty() && !m.variadic) args.clear();
    // [cpp.replace]/4: as many arguments as parameters, and with a '...' more than that.
    if (m.variadic) {
        if (args.size() <= m.params.size())
            fail(name, "'" + name.text + "' takes " + std::to_string(m.params.size()) +
                       " argument(s) and at least one for its '...', given " +
                       std::to_string(args.size()) + " - [cpp.replace]/4");
    } else if (args.size() != m.params.size()) {
        fail(name, "'" + name.text + "' takes " + std::to_string(m.params.size()) +
                   " argument(s), given " + std::to_string(args.size()));
    }
    return args;
}

// [cpp.subst], [cpp.stringize], [cpp.concat]: the replacement list with each parameter replaced -
// by its argument macro-replaced, or as written beside `#` and `##` - then `##` done and the
// placemarkers dropped. What comes out carries the invocation's place and the names that made it.
std::vector<Preprocessor::Tok> Preprocessor::substitute(const Macro &m,
                                                        const std::vector<std::vector<Tok> > &args,
                                                        const Tok &name, const Tok &close, int hs,
                                                        bool inIf) {
    auto paramIndex = [&m](const Tok &b) -> int {
        if (b.kind != Tok::Ident) return -1;
        if (m.variadic && b.text == "__VA_ARGS__") return static_cast<int>(m.params.size());
        for (std::size_t k = 0; k < m.params.size(); k++)
            if (m.params[k] == b.text) return static_cast<int>(k);
        return -1;
    };
    // [cpp.subst]/1: an argument is macro-replaced once, however often it is used.
    std::vector<std::vector<Tok> > expanded(args.size());
    std::vector<char> done(args.size(), 0);
    const std::vector<Tok> &body = m.body;
    std::vector<Tok> out;
    for (std::size_t k = 0; k < body.size(); k++) {
        const Tok &b = body[k];
        if (m.functionLike && b.kind == Tok::Punct && hashText(b.text) && k + 1 < body.size()) {
            const int p = paramIndex(body[k + 1]);
            if (p >= 0) {
                Tok s = stringify(args[static_cast<std::size_t>(p)]);
                s.space = b.space;
                out.push_back(std::move(s));
                k++;
                continue;
            }
        }
        const int p = m.functionLike ? paramIndex(b) : -1;
        if (p < 0) { out.push_back(b); continue; }
        const std::size_t a = static_cast<std::size_t>(p);
        const bool pasted = (!out.empty() && out.back().pasteOp) ||
                            (k + 1 < body.size() && body[k + 1].pasteOp);
        if (!pasted && !done[a]) {
            expanded[a] = expandAll(args[a], inIf);
            done[a] = 1;
        }
        const std::vector<Tok> &with = pasted ? args[a] : expanded[a];
        if (with.empty()) {
            if (pasted) {
                Tok marker;
                marker.kind = Tok::Placemarker;
                marker.space = b.space;
                out.push_back(std::move(marker));
            }
            continue;
        }
        const std::size_t first = out.size();
        out.insert(out.end(), with.begin(), with.end());
        out[first].space = b.space;
        for (std::size_t q = first; q < out.size(); q++) out[q].pasteOp = false;
    }
    std::vector<Tok> joined;
    for (std::size_t k = 0; k < out.size(); k++) {
        if (out[k].pasteOp && !joined.empty() && k + 1 < out.size()) {
            Tok lhs = std::move(joined.back());
            joined.pop_back();
            joined.push_back(paste(lhs, out[k + 1], name));
            k++;
            continue;
        }
        joined.push_back(std::move(out[k]));
    }
    // __LINE__ in a replacement is the line the outermost invocation ends on, as clang has it.
    const int end = name.fromMacro ? name.endLine
                  : m.functionLike ? (close.fromMacro ? close.endLine : close.line) : name.line;
    std::vector<Tok> result;
    result.reserve(joined.size());
    for (Tok &t : joined) {
        if (t.kind == Tok::Placemarker) continue;
        t.hide = hide_.unite(t.hide, hs);
        t.fromMacro = true;
        t.bol = false;
        t.pasteOp = false;
        t.file = name.file;
        t.line = name.line;
        t.endLine = end;
        t.src = name.src;
        t.phys = name.phys;
        t.col = name.col;
        t.ws = nullptr;
        t.wsLen = 0;
        result.push_back(std::move(t));
    }
    if (!result.empty()) {
        result[0].space = name.space;
        result[0].ws = name.ws;
        result[0].wsLen = name.wsLen;
    }
    return result;
}

Preprocessor::Tok Preprocessor::get(Ctx &c) {
    if (!c.pending.empty()) {
        Tok t = std::move(c.pending.back());
        c.pending.pop_back();
        return t;
    }
    if (c.fromFile) return fileNext();
    return Tok();
}

const Preprocessor::Tok &Preprocessor::peek(Ctx &c) {
    static const Tok kEnd;
    if (!c.pending.empty()) return c.pending.back();
    if (c.fromFile) return filePeek();
    return kEnd;
}

// The names replacement answers itself: __LINE__, __FILE__, __COUNTER__, `_Pragma`, and in a
// condition `defined` and the `__has_*` predicates. A placemarker for what leaves nothing.
bool Preprocessor::builtin(Ctx &c, Tok &t) {
    const std::string &n = t.text;
    if (c.inIf && n == "defined") {
        // [cpp.cond]/1: `defined X` or `defined ( X )`, the name not replaced.
        Tok a = get(c);
        const bool paren = a.is("(");
        if (paren) a = get(c);
        if (a.kind != Tok::Ident) fail(a.kind == Tok::End ? t : a, "'defined' needs a name");
        if (paren) {
            const Tok b = get(c);
            if (!b.is(")")) fail(b.kind == Tok::End ? a : b, "'defined(' is missing its ')'");
        }
        // **These are defined, though no `#define` wrote them.**
        const bool yes = macros_.count(a.text) != 0 || isHasPredicate(a.text) ||
                         isDynamicMacro(a.text);
        t.kind = Tok::Number;
        t.text = yes ? "1" : "0";
        return true;
    }
    if (c.inIf && isHasPredicate(n)) {
        // Not a call: an ordinary identifier that happens to be spelled so.
        if (!peek(c).is("(")) return false;
        t = hasCheck(c, t);
        return true;
    }
    if (n == "__LINE__") {
        t.kind = Tok::Number;
        t.text = std::to_string(t.fromMacro ? t.endLine : t.line);
        return true;
    }
    if (n == "__COUNTER__") {
        t.kind = Tok::Number;
        t.text = std::to_string(counter_++);
        return true;
    }
    if (n == "__FILE__") {
        // A Windows path is full of backslashes: escape them, and any quote, for the literal.
        std::string s = "\"";
        for (char f : files_[static_cast<std::size_t>(t.file)]) {
            if (f == '\\' || f == '"') s += '\\';
            s += f;
        }
        t.kind = Tok::String;
        t.text = s + "\"";
        return true;
    }
    if (n == "_Pragma") {
        // [cpp.pragma.op]: `_Pragma("...")` is a `#pragma` in the middle of a line - the string
        // destringized and handed to the same reader, so `_Pragma("pack(1)")` packs and an unknown
        // one is dropped (1.27, 2026-10-21, review of 1.25, item 3: it was "not declared").
        const Tok open = get(c);
        const Tok str = open.is("(") ? get(c) : Tok();
        const Tok close = str.kind == Tok::String ? get(c) : Tok();
        std::string s = str.text;
        if (!s.empty() && s[0] == 'L') s.erase(0, 1);
        if (!close.is(")") || s.size() < 2 || s[0] != '"' || s[s.size() - 1] != '"')
            fail(t, "'_Pragma' takes one string literal in parentheses - [cpp.pragma.op]");
        std::string text;
        for (std::size_t k = 1; k + 1 < s.size(); k++) {
            if (s[k] == '\\' && k + 2 < s.size() && (s[k + 1] == '"' || s[k + 1] == '\\')) k++;
            text += s[k];
        }
        pragmaInLine_ = true;
        pragma(text, t);
        pragmaInLine_ = false;
        t.kind = Tok::Placemarker;
        return true;
    }
    return false;
}

// [cpp.rescan]: the next token with every macro in it replaced, the replacement read again with
// what follows it. A name that was being replaced where it was produced is not replaced again, and
// never will be - the hide sets of the algorithm Prosser wrote for C89.
Preprocessor::Tok Preprocessor::expand(Ctx &c) {
    for (;;) {
        Tok t = get(c);
        if (t.kind != Tok::Ident || t.painted) return t;
        if (builtin(c, t)) {
            if (t.kind == Tok::Placemarker) continue;
            return t;
        }
        auto it = macros_.find(t.text);
        if (it == macros_.end()) return t;
        const std::shared_ptr<const Macro> m = it->second;
        if (hide_.has(t.hide, m->id)) {
            t.painted = true;
            return t;
        }
        std::vector<Tok> result;
        if (!m->functionLike) {
            result = substitute(*m, std::vector<std::vector<Tok> >(), t, t,
                                hide_.with(t.hide, m->id), c.inIf);
        } else {
            if (!peek(c).is("(")) return t;
            get(c);
            Tok close;
            const std::vector<std::vector<Tok> > args = collectArgs(c, *m, t, &close);
            result = substitute(*m, args, t, close,
                                hide_.with(hide_.intersect(t.hide, close.hide), m->id), c.inIf);
        }
        c.pending.insert(c.pending.end(), std::make_move_iterator(result.rbegin()),
                         std::make_move_iterator(result.rend()));
    }
}

// [cpp.subst]/1: tokens replaced "as if they formed the rest of the preprocessing file" - and
// nothing after them is available.
std::vector<Preprocessor::Tok> Preprocessor::expandAll(const std::vector<Tok> &toks, bool inIf) {
    Ctx c;
    c.inIf = inIf;
    c.pending.assign(toks.rbegin(), toks.rend());
    std::vector<Tok> out;
    for (;;) {
        Tok t = expand(c);
        if (t.kind == Tok::End) return out;
        out.push_back(std::move(t));
    }
}

long long Preprocessor::evalCondition(const std::vector<Tok> &line, const Tok &at) {
    const std::vector<Tok> e = expandAll(line, true);
    if (e.empty()) fail(at, "this directive needs a condition");

    // [cpp.cond]/4: values are intmax_t or uintmax_t, and the usual arithmetic
    // conversions make an operation unsigned with either operand (`#if -1 >
    // 0u` holds); a value carries the flag, `!` and comparisons give int.
    struct V {
        long long v;
        bool u;
    };
    // **[expr.const]/2: what is not evaluated is not checked.** The arm of a `?:` not taken and the
    // right of a `&&` or `||` that is decided are parsed with `live` off; everything evaluated that
    // a constant expression may not do - divide by zero, overflow, shift too far - is refused.
    struct E {
        Preprocessor *pp;
        const std::vector<Tok> &t;
        const Tok &at;
        std::size_t i;
        bool live;

        const Tok &here() const { return i < t.size() ? t[i] : (t.empty() ? at : t.back()); }
        std::string op() const {
            if (i >= t.size() || t[i].kind != Tok::Punct) return std::string();
            const char *primary = Lexer::alternativeToken(t[i].text);
            return primary ? primary : t[i].text;
        }
        bool take(const char *s) {
            if (op() != s) return false;
            i++;
            return true;
        }
        [[noreturn]] void bad(const std::string &m) const { pp->fail(here(), m); }
        [[noreturn]] void overflow(const char *what) const {
            pp->fail(here(), std::string("this condition overflows: the ") + what +
                             " is past what an intmax_t holds - [expr.const]/2, [cpp.cond]/4");
        }
        static V S(long long v) { return V{ v, false }; }
        static unsigned long long bits(long long v) { return static_cast<unsigned long long>(v); }
        static long long wrap(unsigned long long v) { return static_cast<long long>(v); }

        V number(const Tok &n) const {
            const std::string &s = n.text;
            const bool hex = s.size() > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
            const bool octal = !hex && s[0] == '0';
            std::size_t k = hex ? 2 : 0;
            unsigned long long v = 0;
            bool over = false, any = false;
            for (; k < s.size(); k++) {
                const char c = s[k];
                int d;
                if (std::isdigit(static_cast<unsigned char>(c))) d = c - '0';
                else if (hex && std::isxdigit(static_cast<unsigned char>(c)))
                    d = std::tolower(static_cast<unsigned char>(c)) - 'a' + 10;
                else break;
                if (octal && d > 7)
                    pp->fail(n, "'" + s + "' is octal, and '" + c + "' is not an octal digit");
                const unsigned base = hex ? 16 : octal ? 8 : 10;
                if (v > (ULLONG_MAX - static_cast<unsigned>(d)) / base) over = true;
                v = v * base + static_cast<unsigned>(d);
                any = true;
            }
            if (s[0] == '.' || (k < s.size() && (s[k] == '.' ||
                                (!hex && (s[k] == 'e' || s[k] == 'E')))))
                pp->fail(n, "'" + s + "' is a floating constant, and '#if' takes integers - "
                            "[cpp.cond]/1");
            if (!any) pp->fail(n, "'" + s + "' has no digits after its '0x'");
            const std::string suffix = s.substr(k);
            static const char *const kSuffixes[] = {
                "", "u", "U", "l", "L", "ll", "LL", "ul", "uL", "Ul", "UL", "lu", "lU", "Lu", "LU",
                "ull", "uLL", "Ull", "ULL", "llu", "llU", "LLu", "LLU", 0
            };
            bool known = false;
            for (int q = 0; kSuffixes[q] != 0; q++) if (suffix == kSuffixes[q]) known = true;
            if (!known)
                pp->fail(n, suffix[0] == '_'
                                ? "a user-defined literal cannot be in '#if' - [cpp.cond]/1"
                                : "'" + suffix + "' is not a suffix an integer constant can have - "
                                  "[lex.icon]");
            if (over) pp->fail(n, "this integer constant does not fit any type");
            bool u = suffix.find_first_of("uU") != std::string::npos;
            // [lex.icon]/2-3: past the signed range a hex or octal constant is unsigned, and a
            // decimal one without 'u' has no type at all.
            if (v > static_cast<unsigned long long>(LLONG_MAX) && !u) {
                if (!hex && !octal)
                    pp->fail(n, "'" + s + "' is too large for every signed type, and a decimal "
                                "constant without 'u' has only signed ones - [lex.icon]/3");
                u = true;
            }
            return V{ wrap(v), u };
        }

        // Read as the lexer reads it, so `#if 'A' == 65` and the program agree on a character's value.
        V character(const Tok &n) const {
            const std::string &s = n.text;
            std::size_t k = 0;
            char prefix = 0;
            if (s[0] != '\'') prefix = s[k++];
            std::size_t close = k + 1;
            for (; close < s.size() && s[close] != '\''; close++)
                if (s[close] == '\\') close++;
            if (close + 1 != s.size())
                pp->fail(n, "a user-defined literal cannot be in '#if' - [cpp.cond]/1");
            const bool wide = prefix != 0;
            long long v = 0;
            int chars = 0;
            for (k++; k < close; chars++) {
                long long one;
                const char c = s[k++];
                if (c != '\\') {
                    one = static_cast<unsigned char>(c);
                } else {
                    const char e = s[k++];
                    switch (e) {
                    case 'n': one = '\n'; break;
                    case 't': one = '\t'; break;
                    case 'r': one = '\r'; break;
                    case 'a': one = '\a'; break;
                    case 'b': one = '\b'; break;
                    case 'f': one = '\f'; break;
                    case 'v': one = '\v'; break;
                    case 'x': case 'X': {
                        one = 0;
                        bool any = false;
                        while (k < close && std::isxdigit(static_cast<unsigned char>(s[k]))) {
                            const char d = s[k++];
                            one = one * 16 + (std::isdigit(static_cast<unsigned char>(d))
                                                  ? d - '0'
                                                  : std::tolower(static_cast<unsigned char>(d)) - 'a' + 10);
                            any = true;
                        }
                        if (!any) one = static_cast<unsigned char>(e);
                        else if (!wide) one &= 0xff;
                        break;
                    }
                    case 'u': case 'U': {
                        const int digits = e == 'u' ? 4 : 8;
                        one = 0;
                        for (int q = 0; q < digits && k < close &&
                                        std::isxdigit(static_cast<unsigned char>(s[k])); q++) {
                            const char d = s[k++];
                            one = one * 16 + (std::isdigit(static_cast<unsigned char>(d))
                                                  ? d - '0'
                                                  : std::tolower(static_cast<unsigned char>(d)) - 'a' + 10);
                        }
                        if (!wide && one > 0x7F)
                            pp->fail(n, "a universal character name past U+007F does not fit a char");
                        break;
                    }
                    default:
                        if (e >= '0' && e <= '7') {
                            one = e - '0';
                            for (int q = 0; q < 2 && k < close && s[k] >= '0' && s[k] <= '7'; q++)
                                one = one * 8 + (s[k++] - '0');
                            one &= 0xff;
                        } else {
                            one = static_cast<unsigned char>(e);
                        }
                    }
                }
                v = wide ? one : ((v << 8) | (one & 0xff));
            }
            if (chars == 0) pp->fail(n, "empty character constant");
            if ((prefix == 'u' || prefix == 'U') && chars != 1)
                pp->fail(n, "a u or U character literal holds one character - [lex.ccon]/2");
            if (!wide && chars == 1) v = static_cast<signed char>(v);
            // char16_t and char32_t are unsigned types, so in a condition they are uintmax_t.
            return V{ v, prefix == 'u' || prefix == 'U' };
        }

        V primary() {
            if (i >= t.size()) bad("this condition ends too early");
            if (take("(")) {
                const V v = comma();
                if (!take(")")) bad("this condition is missing a ')'");
                return v;
            }
            const Tok &n = t[i];
            if (n.kind == Tok::Number) { i++; return number(n); }
            if (n.kind == Tok::Char) { i++; return character(n); }
            // [cpp.cond]/4: every name left after replacement is 0, `true` and `false` aside.
            if (n.kind == Tok::Ident) { i++; return S(n.text == "true" ? 1 : 0); }
            if (n.kind == Tok::String) bad("a string literal cannot be in '#if' - [cpp.cond]/1");
            bad("this condition has a stray '" + n.text + "'");
        }
        V unary() {
            if (take("!")) return S(!unary().v);
            if (take("~")) { const V a = unary(); return V{ ~a.v, a.u }; }
            if (take("-")) {
                const V a = unary();
                if (live && !a.u && a.v == LLONG_MIN) overflow("negation");
                return V{ wrap(0ULL - bits(a.v)), a.u };
            }
            if (take("+")) return unary();
            return primary();
        }
        V mul() {
            V v = unary();
            for (;;) {
                const bool times = take("*");
                const bool divide = !times && take("/");
                const bool rest = !times && !divide && take("%");
                if (!times && !divide && !rest) return v;
                const V r = unary();
                const bool u = v.u || r.u;
                if (!live) { v = V{ 0, u }; continue; }
                if (times) {
                    if (!u) {
                        const long long a = v.v, b = r.v;
                        const bool over = a != 0 && b != 0 &&
                            ((a == -1 && b == LLONG_MIN) || (b == -1 && a == LLONG_MIN) ||
                             (a != -1 && b != -1 && wrap(bits(a) * bits(b)) / b != a));
                        if (over) overflow("product");
                    }
                    v = V{ wrap(bits(v.v) * bits(r.v)), u };
                    continue;
                }
                if (r.v == 0) bad("division by zero in this condition");
                if (u) {
                    v = V{ wrap(divide ? bits(v.v) / bits(r.v) : bits(v.v) % bits(r.v)), true };
                    continue;
                }
                // [expr.mul]/4: the one quotient past the range, and its remainder with it.
                if (v.v == LLONG_MIN && r.v == -1) overflow(divide ? "quotient" : "remainder");
                v = S(divide ? v.v / r.v : v.v % r.v);
            }
        }
        V add() {
            V v = mul();
            for (;;) {
                const bool plus = take("+");
                if (!plus && !take("-")) return v;
                const V r = mul();
                const bool u = v.u || r.u;
                if (live && !u) {
                    const long long a = v.v, b = r.v;
                    const bool over = plus ? (b > 0 ? a > LLONG_MAX - b : a < LLONG_MIN - b)
                                           : (b < 0 ? a > LLONG_MAX + b : a < LLONG_MIN + b);
                    if (over) overflow(plus ? "sum" : "difference");
                }
                v = V{ wrap(plus ? bits(v.v) + bits(r.v) : bits(v.v) - bits(r.v)), u };
            }
        }
        V shift() {
            V v = add();
            for (;;) {
                const bool left = take("<<");
                if (!left && !take(">>")) return v;
                const V r = add();
                if (!live) continue;
                // [expr.shift]/1: the result has the left operand's type, and a count outside
                // [0, 63] is undefined, as a left shift of a negative value or past the range is.
                if (r.u ? bits(r.v) > 63 : (r.v < 0 || r.v > 63))
                    bad("a shift of " + (r.u ? std::to_string(bits(r.v)) : std::to_string(r.v)) +
                        " places in this condition, where a value has 64 bits - [expr.shift]/1");
                const int n = static_cast<int>(r.v);
                if (left && !v.u) {
                    if (v.v < 0)
                        bad("a negative value shifted left in this condition - [expr.shift]/2");
                    if (v.v > (LLONG_MAX >> n)) overflow("left shift");
                }
                v.v = left ? wrap(bits(v.v) << n)
                     : v.u ? wrap(bits(v.v) >> n)
                     : (v.v < 0 ? -(-(v.v + 1) >> n) - 1 : v.v >> n);
            }
        }
        // The relational and equality operators compare unsigned where either side is.
        static bool less(const V &a, const V &b) {
            return (a.u || b.u) ? bits(a.v) < bits(b.v) : a.v < b.v;
        }
        V rel() {
            V v = shift();
            for (;;) {
                if (take("<=")) { const V r = shift(); v = S(!less(r, v)); }
                else if (take(">=")) { const V r = shift(); v = S(!less(v, r)); }
                else if (take("<")) { const V r = shift(); v = S(less(v, r)); }
                else if (take(">")) { const V r = shift(); v = S(less(r, v)); }
                else return v;
            }
        }
        V eq() {
            V v = rel();
            for (;;) {
                if (take("==")) v = S(v.v == rel().v);
                else if (take("!=")) v = S(v.v != rel().v);
                else return v;
            }
        }
        V bitAnd() { V v = eq(); while (take("&")) { const V r = eq(); v = V{ v.v & r.v, v.u || r.u }; } return v; }
        V bitXor() { V v = bitAnd(); while (take("^")) { const V r = bitAnd(); v = V{ v.v ^ r.v, v.u || r.u }; } return v; }
        V bitOr()  { V v = bitXor(); while (take("|")) { const V r = bitXor(); v = V{ v.v | r.v, v.u || r.u }; } return v; }
        V land() {
            V v = bitOr();
            while (take("&&")) {
                const bool was = live;
                live = was && v.v != 0;
                const V r = bitOr();
                live = was;
                v = S(v.v != 0 && r.v != 0);
            }
            return v;
        }
        V lor() {
            V v = land();
            while (take("||")) {
                const bool was = live;
                live = was && v.v == 0;
                const V r = land();
                live = was;
                v = S(v.v != 0 || r.v != 0);
            }
            return v;
        }
        V cond() {
            const V c = lor();
            if (!take("?")) return c;
            const bool was = live;
            live = was && c.v != 0;
            const V a = comma();
            if (!take(":")) bad("this condition is missing the ':' of a '?:'");
            live = was && c.v == 0;
            const V b = cond();
            live = was;
            // [expr.cond]/5: the arms meet by the usual arithmetic conversions.
            V picked = c.v ? a : b;
            picked.u = a.u || b.u;
            return picked;
        }
        // In parentheses and between `?` and `:` an expression may hold a comma; its left operand
        // is evaluated and its value is the right one's.
        V comma() {
            V v = cond();
            while (take(",")) v = cond();
            return v;
        }
    };

    E ev{ this, e, at, 0, true };
    const long long v = ev.cond().v;
    if (ev.i != e.size())
        fail(e[ev.i], "this condition has something left over: '" + spell(e, ev.i) + "'");
    return v;
}

// **Two tokens written one after the other must read back as two** - a `+` after a `+`, a name
// after a name, a `/` before a `*` would be something else. Where they were not next to each other
// in the source, a space goes between them if they would.
bool Preprocessor::needsSpace(const std::string &before, const std::string &after) {
    if (before.empty() || after.empty()) return false;
    if (std::strchr("()[]{};,?~", after[0]) != nullptr) return false;
    const char a = before[before.size() - 1];
    if (a == '/' && (after[0] == '/' || after[0] == '*')) return true;
    const std::string s = before + after;
    Tok::Kind kind;
    return scanToken(s, 0, &kind) != before.size();
}

void Preprocessor::emit(const Tok &t) {
    if (lines_.empty() || t.file != outFile_ || t.line != outLine_) {
        if (!lines_.empty()) out_ += '\n';
        lines_.push_back(Source::Line{ t.file, t.line });
        outFile_ = t.file;
        outLine_ = t.line;
        // A line the text takes up part way along - after an invocation that spanned lines - starts
        // at the column it had.
        if (static_cast<unsigned>(t.col) > t.wsLen) out_.append(t.col - t.wsLen, ' ');
        if (t.wsLen != 0) out_.append(t.ws, t.wsLen);
    } else if (t.wsLen != 0) {
        out_.append(t.ws, t.wsLen);
    } else if (t.space || needsSpace(lastText_, t.text) ||
               (t.text[0] == '.' && out_.size() >= 2 &&
                out_.compare(out_.size() - 2, 2, "..") == 0)) {
        out_ += ' ';
    }
    out_ += t.text;
    // A raw string's new-lines are new lines of the text, each its own line of the file.
    for (char c : t.text)
        if (c == '\n') lines_.push_back(Source::Line{ t.file, ++outLine_ });
    lastText_ = t.text;
}

Source Preprocessor::run() {
    for (const std::pair<std::string, std::string> &p : predefined_) {
        Macro m;
        m.body = tokenize(p.second);
        if (!m.body.empty()) m.body[0].space = false;
        for (Tok &b : m.body) b.pasteOp = b.kind == Tok::Punct && pasteText(b.text);
        m.id = nameId(p.first);
        macros_[p.first] = std::make_shared<const Macro>(std::move(m));
    }

    files_.push_back(path_);
    enter(load(path_, 0));
    Ctx c;
    c.fromFile = true;
    for (;;) {
        const Tok t = expand(c);
        if (t.kind == Tok::End) {
            finishFile();
            if (readers_.empty()) break;
            continue;
        }
        emit(t);
    }
    Source src(path_, out_, files_, lines_);
    src.setPacks(packs_);
    return src;
}
