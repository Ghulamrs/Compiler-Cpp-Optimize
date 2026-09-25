#include "Arm64Darwin.h"
#include "Tms6747.h"
#include "Backend.h"
#include "X86_64Linux.h"
#include "X86_64Windows.h"

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
    add("__STDC_HOSTED__", "1");
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
    if (g.isConst) return relocates(g) ? Segment::ConstRelocated : Segment::Const;
    if (!g.hasInit) return Segment::Bss;

    for (const GlobalPiece &p : g.init)
        if (p.value != 0 || !p.symbol.empty()) return Segment::Data;
    return Segment::Bss;
}

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
