#include "Arm64Darwin.h"
#include "Backend.h"
#include "Tms6747.h"
#include "X86_64Linux.h"
#include "X86_64Windows.h"

#include <cctype>
#include <ctime>
#include <ostream>
#include <string>

std::unique_ptr<CodeGen> X86_64LinuxBackend::codegen(std::ostream &sink, Syntax) const {
    return std::unique_ptr<CodeGen>(new X86_64Linux(sink, target_, abi()));
}

static const X86_64LinuxBackend kLinux;
static const X86_64WindowsBackend kWindows;
static const Arm64DarwinBackend kDarwin;
static const Tms6747Backend kTms6747;

static const Backend *const kBackends[] = { &kLinux, &kWindows, &kDarwin, &kTms6747 };

// **A name past ASCII is quoted, as clang writes it**, `"_Z4f\u00e9ei"` and `"_caf\u00e9"@PAGE` in
// UTF-8 - the one place every name passes; tms6747, whose assemblers take no such name, gets its
// reason back. Text with no byte past 0x7F is never touched, and Masm.cpp refuses its own.
std::string spellWideNames(const Backend &b, std::string *text) {
    const std::string &in = *text;
    std::size_t first = 0;
    while (first < in.size() && static_cast<unsigned char>(in[first]) < 0x80) first++;
    if (first == in.size()) return std::string();
    const bool tms = std::string(b.name()) == "tms6747";
    auto nameChar = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$' ||
               static_cast<unsigned char>(c) >= 0x80;
    };
    std::string out;
    bool quoted = false;
    for (std::size_t i = 0; i < in.size(); ) {
        const char c = in[i];
        if (c == '\n') quoted = false;
        if (quoted || !nameChar(c)) {
            out += c;
            if (quoted && c == '\\' && i + 1 < in.size()) out += in[++i];
            else if (c == '"') quoted = !quoted;
            i++;
            continue;
        }
        std::size_t end = i;
        bool wide = false;
        while (end < in.size() && nameChar(in[end]))
            wide = static_cast<unsigned char>(in[end++]) >= 0x80 || wide;
        const std::string run = in.substr(i, end - i);
        i = end;
        if (!wide) { out += run; continue; }
        // [lex.name] gives the name; the C6000's assemblers take no byte past ASCII in one.
        if (tms)
            return "a name past ASCII, '" + run + "', is not supported yet on tms6747 - "
                   "ASM6x's names are letters, digits, '_', '$' and '.'";
        out += '"';
        out += run;
        out += '"';
    }
    text->swap(out);
    return std::string();
}

static std::string twoDigits(int n) {
    const std::string digits = std::to_string(n);
    return digits.size() < 2 ? "0" + digits : digits;
}

static void addTranslationTime(std::vector<std::pair<std::string, std::string> > &out) {
    static const char *const kMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    const std::time_t now = std::time(nullptr);
    const std::tm *when = std::localtime(&now);
    if (when == nullptr) return;

    const std::string day = std::to_string(when->tm_mday);
    std::string date = kMonths[when->tm_mon % 12];
    date += " ";
    date += (day.size() < 2 ? " " + day : day);
    date += " " + std::to_string(when->tm_year + 1900);

    const std::string time = twoDigits(when->tm_hour) + ":" + twoDigits(when->tm_min) +
                             ":" + twoDigits(when->tm_sec);

    out.push_back(std::make_pair("__DATE__", "\"" + date + "\""));
    out.push_back(std::make_pair("__TIME__", "\"" + time + "\""));
}

std::vector<std::pair<std::string, std::string> > predefinedMacros(const Backend &b) {
    const Target &t = b.target();
    std::vector<std::pair<std::string, std::string> > out;
    auto add = [&out](const std::string &k, const std::string &v) {
        out.push_back(std::make_pair(k, v));
    };
    auto width = [&t](Kind k) { return std::to_string(t.sizeOf(k)); };

    add("__STDC__", "1");
    // The language this compiler reads is C++11, and a program is entitled to
    // ask. 201103L is the value the standard fixes for it.
    add("__cplusplus", "201103L");
    add("__STDC_HOSTED__", b.hosted() ? "1" : "0");
    // [cpp.predefined]/2: 1 if and only if a program can have more than one thread of execution - the hosted
    // targets, where <thread> starts one; not the C6747, which runs one (IMPLEMENTATION-DEFINED.md). Since 1.68.
    if (b.hosted()) add("__STDCPP_THREADS__", "1");
    addTranslationTime(out);
    add("__CHAR_BIT__", "8");
    add("__SIZEOF_SHORT__", width(Kind::Short));
    add("__SIZEOF_INT__", width(Kind::Int));
    add("__SIZEOF_LONG__", width(Kind::Long));
    add("__SIZEOF_LONG_LONG__", width(Kind::LongLong));
    add("__SIZEOF_FLOAT__", width(Kind::Float));
    add("__SIZEOF_DOUBLE__", width(Kind::Double));
    add("__SIZEOF_POINTER__", width(Kind::Pointer));

    for (const char *const *m = b.identityMacros(); *m != nullptr; m++) {
        std::string s(*m);
        std::size_t eq = s.find('=');
        add(s.substr(0, eq), s.substr(eq + 1));
    }
    return out;
}

static bool relocates(const Global &g) {
    for (const GlobalPiece &p : g.init)
        if (!p.symbol.empty()) return true;
    return false;
}

Segment segmentFor(const Global &g) {
    if (g.isThreadLocal) {
        for (const GlobalPiece &p : g.init)
            if (p.value != 0 || !p.symbol.empty()) return Segment::TlsData;
        return Segment::TlsBss;
    }
    if (g.isConst) return relocates(g) ? Segment::ConstRelocated : Segment::Const;
    if (!g.hasInit) return Segment::Bss;

    for (const GlobalPiece &p : g.init)
        if (p.value != 0 || !p.symbol.empty()) return Segment::Data;
    return Segment::Bss;
}

static std::string tlsEncoding(const std::string &symbol) {
    if (symbol.compare(0, 2, "_Z") == 0) return symbol.substr(2);
    return std::to_string(symbol.size()) + symbol;
}
std::string tlsWrapperName(const std::string &symbol) { return "_ZTW" + tlsEncoding(symbol); }
std::string tlsInitName(const std::string &symbol) { return "_ZTH" + tlsEncoding(symbol); }

const Backend *findBackend(const std::string &name) {
    for (const Backend *b : kBackends)
        if (name == b->name()) return b;
    return nullptr;
}

const Backend &defaultBackend() {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    return kDarwin;
#elif defined(_WIN32)
    return kWindows;
#else
    return kLinux;
#endif
}

std::string backendNames() {
    std::string all;
    for (const Backend *b : kBackends) {
        if (!all.empty()) all += ", ";
        all += b->name();
    }
    return all;
}
