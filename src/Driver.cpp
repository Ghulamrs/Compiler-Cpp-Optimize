#include "Driver.h"
#include "Name.h"
#include "Version.h"
#include "backend/X86_64Windows.h"

#include "backend/Backend.h"
#include "Lexer.h"
#include "parser/Parser.h"
#include "Preprocessor.h"
#include "Source.h"
#include "Type.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <cerrno>
#include <glob.h>
#include <spawn.h>
#include <sys/wait.h>
#endif

#ifdef __linux__
#include <sched.h>
#endif

// **Where the running program is**, which is how a *released* compiler finds
// the headers it ships with: see standardIncludeDirectories(). One call each,
// and the fallback below needs none of them.
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// **The environment a spawned tool inherits.** Apple gives an executable its
// `environ` through this accessor rather than as a symbol; everything else
// declares the variable.
#ifndef _WIN32
#if defined(__APPLE__)
#include <crt_externs.h>
#define CXX1_ENVIRON (*_NSGetEnviron())
#else
extern char **environ;
#define CXX1_ENVIRON environ
#endif
#endif

namespace {

const std::size_t kThreadFrom = 4;
// How many threads a pool makes at a time: a set is at work before the next is made (runJobs).
const std::size_t kThreadSet = 3;

}

#ifndef CXX1_INCLUDE_DIR
#define CXX1_INCLUDE_DIR ""
#endif
#ifndef CXX1_WIN_INCLUDE_DIR
#define CXX1_WIN_INCLUDE_DIR ""
#endif
// **The C++ headers are a second directory, not more files in the first.**
// `include/c++` holds `<cstddef>` and `<vector>`; `include/c` holds the C
// headers those wrap, the set c90 reads too, and a program may reach either.
#ifndef CXX1_CXX_INCLUDE_DIR
#define CXX1_CXX_INCLUDE_DIR ""
#endif

// **The line every run of this compiler prints**, and the one thing in the output that is not about the program being compiled.
const char *Driver::bannerLine() { return CXX1_BANNER; }

// **The directory the running program is in.** A released compiler is unpacked
// somewhere nobody chose at build time, so the headers it ships with cannot be
// found by a path compiled into it - they are found *beside it*.
static std::string programDirectory(const std::string &argv0) {
    std::string full;
#if defined(_WIN32)
    char buf[4096];
    DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) full.assign(buf, n);
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t n = sizeof(buf);
    if (_NSGetExecutablePath(buf, &n) == 0) {
        char real[4096];
        full = realpath(buf, real) != nullptr ? real : buf;
    }
#elif defined(__linux__)
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) full.assign(buf, static_cast<std::size_t>(n));
#endif
    if (full.empty()) full = argv0;
    std::size_t cut = full.find_last_of("/\\");
    if (cut == std::string::npos) return std::string(".");
    return full.substr(0, cut);
}

static bool directoryHas(const std::string &dir, const char *name) {
    std::ifstream probe((dir + "/" + name).c_str());
    return probe.good();
}

namespace {
// **The construct that begins at `pos` taken out of the text**, an empty one left where it stood: a statement to its
// `;` or its closing brace - with the `else` or `catch` that belongs to it - or a declaration, a class's to the `;`
// after it. What a declaration there would have introduced is told to `src`. False where nothing can be taken.
bool takeOut(std::vector<Token> &tokens, std::size_t pos, const Source &src) {
    std::size_t i = 0;
    while (i < tokens.size() && tokens[i].kind != TokenKind::End && tokens[i].pos < pos) i++;
    if (i >= tokens.size() || tokens[i].kind == TokenKind::End || tokens[i].pos != pos) return false;
    const bool toSemicolon = tokens[i].is("struct") || tokens[i].is("class") || tokens[i].is("union") ||
                             tokens[i].is("enum") || tokens[i].is("typedef");
    int depth = 0;
    std::size_t last = i;
    for (;; last++) {
        const Token &t = tokens[last];
        if (t.kind == TokenKind::End) { last--; break; }
        if (t.is("(") || t.is("[") || t.is("{")) { depth++; continue; }
        if (t.is(")") || t.is("]") || t.is("}")) {
            if (depth == 0) { if (last == i) return false; last--; break; }   // what holds it ended first
            depth--;
            if (depth != 0 || !t.is("}")) continue;
            const Token &n = tokens[last + 1];
            if (n.is(";")) { last++; break; }
            if (!toSemicolon && !n.is("else") && !n.is("catch") && !n.is("while") && !n.is(",") && !n.is("{")) break;
            continue;
        }
        if (depth == 0 && t.is(";")) break;
    }
    // `int x = wrong;`: x is what the rest of the function goes on to name.
    depth = 0;
    for (std::size_t k = i; k < last; k++) {
        const Token &t = tokens[k];
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) depth--;
        else if (depth == 0 && k > i && t.kind == TokenKind::Ident) {
            const Token &b = tokens[k - 1], &n = tokens[k + 1];
            if ((b.kind == TokenKind::Ident || b.kind == TokenKind::Keyword || b.is("*") || b.is("&") || b.is(">")) &&
                (n.is("=") || n.is(";") || n.is(",") || n.is("(") || n.is("{") || n.is("[")))
                src.quieten(t.text);
        }
    }
    Token empty = tokens[i];
    empty.kind = TokenKind::Punct;
    empty.text = ";";
    tokens.erase(tokens.begin() + static_cast<long>(i) + 1, tokens.begin() + static_cast<long>(last) + 1);
    tokens[i] = empty;
    return true;
}
}   // namespace

// **Every error, not only the first.** A parser that has failed holds nothing worth going on with, so the file is
// read again by a new one without the construct that failed, each reading saying the next error. A compile that
// succeeds never comes here. It stops where nothing can be taken out - a failure with no statement or declaration
// of the program's around it - or at twenty errors; the error already said is what fails the compile.
void Driver::reportTheRest(const Source &src, const std::vector<Token> &tokens, std::size_t failedAt, const Target &target) {
    const int kMostErrors = 20;
    std::vector<Token> now = tokens;
    std::size_t taken = Parser::npos();
    for (int reading = 0; reading < 2 * kMostErrors; reading++) {
        if (failedAt == Parser::npos() || failedAt == taken || !takeOut(now, failedAt, src)) return;
        taken = failedAt;
        src.clearFailed();
        TypeTable types;
        Parser next(src, now, types, target, backend_->abi());
        try {
            (void)next.parse();
            return;
        } catch (const CompileFailed &) {
            if (!src.failed()) return;
            failedAt = next.failedAt();
        }
        if (src.errorsReported() >= kMostErrors) {
            std::fprintf(stderr, "%s: too many errors, stopping\n", src.files().front().c_str());
            return;
        }
    }
}

// **Where the standard headers are, asked in the order a release wants.**
void Driver::standardIncludeDirectories(const std::string &argv0) {
    const char *envCxx = std::getenv(program::env("INCLUDE").c_str());
    const char *envC = std::getenv(program::env("LIB").c_str());
    if (envCxx != nullptr && envCxx[0] != '\0') {
        searchPath_.push_back(envCxx);
        if (envC != nullptr && envC[0] != '\0') searchPath_.push_back(envC);
        return;
    }

    // Beside the binary or one up: include/c++, include/c, include/win
    // (docs\Runtime_Design.md 5.3); then the earlier layout, include/ being
    // this compiler's when it holds `cstddef`, C headers there or in lib/.
    const std::string here = programDirectory(argv0);
    const std::string candidates[2] = { here, here + "/.." };
    for (const std::string &at : candidates) {
        if (!directoryHas(at + "/include/c++", "cstddef") || !directoryHas(at + "/include/c", "stddef.h")) continue;
        searchPath_.push_back(at + "/include/c++");
        searchPath_.push_back(at + "/include/c");
        searchPath_.push_back(at + "/include/win");
        return;
    }
    for (const std::string &at : candidates) {
        const std::string cxxDir = at + "/include";
        const std::string cDir = at + "/lib";
        if (!directoryHas(cxxDir, "cstddef")) continue;
        searchPath_.push_back(cxxDir);
        if (!directoryHas(cxxDir, "stddef.h") && directoryHas(cDir, "stddef.h"))
            searchPath_.push_back(cDir);
        return;
    }

    if (CXX1_CXX_INCLUDE_DIR[0] != '\0')
        searchPath_.push_back(CXX1_CXX_INCLUDE_DIR);
    if (CXX1_INCLUDE_DIR[0] != '\0') searchPath_.push_back(CXX1_INCLUDE_DIR);
    if (CXX1_WIN_INCLUDE_DIR[0] != '\0') searchPath_.push_back(CXX1_WIN_INCLUDE_DIR);
}

void Driver::usage(char *file) {
    std::fprintf(stderr,
        "usage: %s <file.cpp> [more.cpp ...] [-S|-c] [-o out] [-D n[=v]] [-U n]\n"
        "               [-I dir] [-j n] [-arch a] [-masm=m] [-rts=r] [-O0|-O1|-O2|-Os] [-g] [-time]\n"
        "       with neither -S nor -c the inputs are compiled, assembled and\n"
        "         linked into a program, named by -o, or a.out - a.exe on a\n"
        "         Windows host; several inputs\n"
        "         link together\n"
        "       -c stops at one object file per input, named by -o or after the\n"
        "         input, in the current directory\n"
        "       -S stops after this compiler and writes assembly instead: one .s\n"
        "         per input, or -o to name the output of a single one\n"
        "       -D defines a macro, '-DN' meaning '-DN=1'; -U removes one, and\n"
        "         either may name one of the target's own\n"
        "       -I adds a directory to the ones <...> searches\n"
        "       -j sets how many files are compiled at once; -j 1 is serial\n"
        "       -arch picks the architecture the code is generated for - one of\n"
        "         x86_64-linux, x86_64-windows, arm64-darwin, tms6747; the host\n"
        "         by default, and another host's only reaches -S, since the\n"
        "         assembler here is this machine's; tms6747 is assembled on any\n"
        "         host by asm6x, the project's own C6000 assembler (CPP11_AS, or\n"
        "         the one beside this program), and linked into a .out by TI's\n"
        "         lnk6x where CCS is (CPP11_TI names its C6000 compiler directory,\n"
        "         CPP11_TILIB one holding rts6740_elf_eh.lib); its code uses the\n"
        "         C64x+ 16-bit compact instructions, as TI's cl6x does, and\n"
        "         --no_compress writes every instruction in 32 bits\n"
        "       -rts= picks the tms6747 run-time library: a directory holding\n"
        "         RTS6x's rts6x.lib (or the .lib itself), or 'ti' for TI's\n"
        "         rts6740. Unsaid: RTS6x from RTS6X or lib/tms6747\n"
        "         beside this program - as RIDE links - unless CPP11_TI or\n"
        "         CPP11_TILIB names TI's; TI's otherwise; the link says which\n"
        "       -masm picks the assembly syntax for x86_64-windows: 'masm' is\n"
        "         the default where this project's masm.exe is beside this\n"
        "         program (RIDE's bin), assembled by it and linked by the\n"
        "         link.exe beside it (CPP11_AS, CPP11_LD name others), COMDAT\n"
        "         and all; 'gnu' is assembled by clang, the default elsewhere\n"
        "         and for -g where clang is found; 'ml64' is the MASM syntax\n"
        "         without COMDAT, for ml64, which links one translation unit\n"
        "         at a time\n"
        "       -O1 and -O2 improve the code of each function: frame slots and\n"
        "         constants forwarded, pushes paired with their pops, dead\n"
        "         instructions removed. Every spelling; -O0 is the default\n"
        "       -Os is -O1 choosing the smaller code where the two differ:\n"
        "         on tms6747 a division by a constant stays a call, no\n"
        "         entry test is duplicated ahead of the frame; elsewhere -O1\n"
        "       -g writes a line table, so a debugger can stop on a line of C++\n"
        "         and step through it: DWARF on x86_64-linux and arm64-darwin,\n"
        "         CodeView on x86_64-windows (linked with /debug into a PDB) -\n"
        "         for clang in the gnu spelling, for GRIDE's masm as the .FILE\n"
        "         and .LINE directives it reads with -masm=masm - and cl6x's\n"
        "         .dwpsn and .dwtag on tms6747; refused for -masm=ml64, and on\n"
        "         tms6747 with -O\n"
        "       -nologo leaves out the line this compiler prints before it\n"
        "         starts, which a build script may not want\n"
        "       -version says which release this is, and which seal file\n"
        "         carries the CRC32 of the sources it was built from\n"
        "       -time reports how long each phase took\n", file);
}

static std::string workingDirectory() {
    char buf[4096];
    if (getcwd(buf, sizeof buf) == nullptr) return std::string();
    return std::string(buf);
}

static bool hostIsWindows() {
    return std::strcmp(defaultBackend().name(), "x86_64-windows") == 0;
}

#ifdef _WIN32
// vswhere's answer, fetched through a temporary file rather than a pipe. cc1 is
// itself run through a pipe by the editor and a nested _popen fails where the
// parent's stdio are not consoles, so this found Visual Studio everywhere else.
static std::string askVswhere() {
    char temp[MAX_PATH];
    char folder[MAX_PATH];
    if (GetTempPathA(MAX_PATH, folder) == 0) return std::string();
    if (GetTempFileNameA(folder, program::kName, 0, temp) == 0) return std::string();

    std::string command =
        "\"\"C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe\""
        " -latest -products * -property installationPath > \"";
    command += temp;
    command += "\"\"";
    std::string found;
    if (std::system(command.c_str()) == 0) {
        std::ifstream answer(temp);
        std::getline(answer, found);
    }
    std::remove(temp);
    while (!found.empty() && (found.back() == '\n' || found.back() == '\r')) {
        found.pop_back();
    }
    return found;
}

static std::string findVcvars() {
    std::string root = askVswhere();
    if (!root.empty()) {
        const std::string bat = root + "\\VC\\Auxiliary\\Build\\vcvars64.bat";
        std::ifstream there(bat.c_str());
        if (there) return bat;
    }
    // vswhere is itself part of an installation and can be absent. The
    // default places are worth trying before giving up on a machine that
    // plainly has the tools.
    static const char *const roots[] = {
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\"
    };
    static const char *const editions[] = {
        "Community", "Professional", "Enterprise", "BuildTools"
    };
    for (std::size_t r = 0; r < sizeof roots / sizeof roots[0]; ++r) {
        for (std::size_t e = 0; e < sizeof editions / sizeof editions[0]; ++e) {
            const std::string bat = std::string(roots[r]) + editions[e] +
                                    "\\VC\\Auxiliary\\Build\\vcvars64.bat";
            std::ifstream there(bat.c_str());
            if (there) return bat;
        }
    }
    return std::string();
}
#endif

// ml64 and link are on PATH only inside a Developer Command Prompt, which an
// editor launched from Explorer is not - so where they are unreachable the
// command runs in a shell that sourced vcvars64.bat, which sets LIB as well.
static std::string developerShell() {
#ifdef _WIN32
    const char *inside = std::getenv("VCToolsInstallDir");
    if (inside != nullptr && inside[0] != '\0') return std::string();
    static const std::string cached = findVcvars();   // asked from the tool pool: a static's initialiser is thread-safe
    return cached;
#else
    return std::string();
#endif
}

// **Per-thread, because the assembler runs on a pool now.**
static thread_local std::string lastToolCommand;

// The program a command starts: its first word, without the quotes around it.
static std::string programOf(const std::string &command) {
    const std::string::size_type start = command.find_first_not_of("\" ");
    if (start == std::string::npos) return std::string();
    const std::string::size_type end = command.find_first_of(command[0] == '"' ? "\"" : " ", start);
    return command.substr(start, end == std::string::npos ? end : end - start);
}

// **The screen does not stand still** (1.70; the user's, 2026-11-18: a compiler that says nothing for ten seconds
// reads as one that hangs). Whoever writes a line of progress notes the moment; while a Heartbeat lives, a thread
// of its own looks four times a second, and when nothing has been noted for two seconds it writes one line saying
// what is in hand - `  ... 12 s: 36 of 39 done, compiling X86.cpp 10 s` - and notes that.
typedef std::chrono::steady_clock SteadyClock;
static const SteadyClock::time_point kDriverBegan = SteadyClock::now();
static std::atomic<long long> lastSaidAt{0};
static long long sinceBeganMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - kDriverBegan).count();
}
static void noteSaid() { lastSaidAt.store(sinceBeganMs()); }

namespace {
class Heartbeat {
public:
    // `what` answers the line's text, or nothing when there is nothing to say; it is called on the heartbeat's
    // own thread with no lock held, and takes diagnosticLock() itself for whatever it reads.
    Heartbeat(bool on, std::function<std::string()> what) : stop_(false) {
        if (!on) return;
        noteSaid();
        thread_ = std::thread([this, what] {
            std::unique_lock<std::mutex> wait(sleep_);
            while (!stop_) {
                wake_.wait_for(wait, std::chrono::milliseconds(250));
                if (stop_) break;
                if (sinceBeganMs() - lastSaidAt.load() < 2000) continue;
                const std::string line = what();
                if (line.empty()) continue;
                std::lock_guard<std::mutex> hold(diagnosticLock());
                std::fprintf(stderr, "  ... %.0f s: %s\n", static_cast<double>(sinceBeganMs()) / 1000.0, line.c_str());
                noteSaid();
            }
        });
    }
    // woken at once: a heartbeat adds nothing to the time of what it watches
    ~Heartbeat() {
        { std::lock_guard<std::mutex> hold(sleep_); stop_ = true; }
        wake_.notify_all();
        if (thread_.joinable()) thread_.join();
    }
private:
    Heartbeat(const Heartbeat &);
    Heartbeat &operator=(const Heartbeat &);
    bool stop_;
    std::mutex sleep_;
    std::condition_variable wake_;
    std::thread thread_;
};
}

#ifdef _WIN32
// One line a tool wrote, written whole to the stream it wrote it to: two tools at once do not write into each
// other's lines.
static void passOn(const std::string &line, FILE *to) {
    std::lock_guard<std::mutex> hold(diagnosticLock());
    std::fwrite(line.data(), 1, line.size(), to);
    std::fflush(to);
    noteSaid();
}

// What comes down one pipe from a tool, passed on line by line as it comes, until the tool closes its end.
static void passOnPipe(HANDLE from, FILE *to) {
    std::string pending;
    char bytes[4096];
    DWORD got = 0;
    while (ReadFile(from, bytes, sizeof bytes, &got, nullptr) && got > 0) {
        pending.append(bytes, got);
        for (std::string::size_type end; (end = pending.find('\n')) != std::string::npos; ) {
            passOn(pending.substr(0, end + 1), to);
            pending.erase(0, end + 1);
        }
    }
    if (!pending.empty()) passOn(pending + "\n", to);
    CloseHandle(from);
}

// **Started directly, not through cmd.exe**: a machine may disable cmd by
// policy (GRIDE's build machine does); the command line is the one a shell
// would get - the quoted path, then the arguments - read by CreateProcess.
//
// **Each tool has pipes of its own** (1.70; the user's, 2026-11-18): what it writes to its output and to its
// error stream comes down two pipes, read here as it is written - the error stream on a thread of its own - and
// passed on to this compiler's streams a whole line at a time. A tool used to be handed this compiler's own
// streams: several assemblers at once wrote into each other's lines, and nothing here knew that one had spoken.
static int runDirect(const std::string &command) {
    static std::mutex oneAtATime;
    HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);
    {
        // One tool is made at a time: the writing ends of its pipes can be inherited for the moment it is made
        // and are closed here before another tool is, so no tool holds another's pipe open.
        std::lock_guard<std::mutex> one(oneAtATime);
        SECURITY_ATTRIBUTES inherit;
        inherit.nLength = sizeof inherit;
        inherit.lpSecurityDescriptor = nullptr;
        inherit.bInheritHandle = TRUE;
        if (!CreatePipe(&outRead, &outWrite, &inherit, 0)) return -1;
        if (!CreatePipe(&errRead, &errWrite, &inherit, 0)) { CloseHandle(outRead); CloseHandle(outWrite); return -1; }
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOA si;
        ZeroMemory(&si, sizeof si);
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = outWrite;
        si.hStdError = errWrite;
        std::vector<char> line(command.begin(), command.end());
        line.push_back('\0');
        const BOOL made = CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
        CloseHandle(outWrite);
        CloseHandle(errWrite);
        if (!made) { CloseHandle(outRead); CloseHandle(errRead); return -1; }
    }
    std::thread errors(passOnPipe, errRead, stderr);
    passOnPipe(outRead, stdout);
    errors.join();
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}
#endif

// **posix_spawn rather than system(), because system() serialises on macOS.**
static int runShell(const std::string &command) {
#ifdef _WIN32
    return runDirect(command);
#else
    const char *argv[] = { "/bin/sh", "-c", command.c_str(), nullptr };
    pid_t pid = 0;
    if (posix_spawn(&pid, "/bin/sh", nullptr, nullptr,
                    const_cast<char *const *>(argv), CXX1_ENVIRON) != 0)
        return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    return status;
#endif
}

// Runs one tool, inside a developer environment where it needs one: a tool named by its path (GRIDE's
// own masm and link) is started as it is; only a bare name - Visual Studio's ml64 and link, which reach
// PATH through vcvars64.bat - runs from a batch file that calls vcvars first, and that takes cmd.
static int runTool(const std::string &command) {
    lastToolCommand = command;
    const bool byPath = programOf(command).find_first_of("/\\") != std::string::npos;
    const std::string vcvars = byPath ? std::string() : developerShell();
    if (vcvars.empty()) return runShell(command);

#ifdef _WIN32
    char folder[MAX_PATH];
    char script[MAX_PATH];
    if (GetTempPathA(MAX_PATH, folder) == 0) return runShell(command);
    if (GetTempFileNameA(folder, program::kName, 0, script) == 0) {
        return runShell(command);
    }
    // **The reserved name is kept until the script is done**: removed first, a
    // second cpp11 running at once was handed it and overwrote this script.
    std::string batch = script;
    batch += ".cmd";

    {
        std::ofstream out(batch.c_str());
        if (!out) { std::remove(script); return runShell(command); }
        out << "@echo off\n";
        out << "call \"" << vcvars << "\" >nul 2>&1\n";
        out << command << "\n";
    }
    lastToolCommand = command + "   [inside " + vcvars + "]";
    const int rc = std::system(("\"" + batch + "\"").c_str());
    std::remove(batch.c_str());
    std::remove(script);
    return rc;
#else
    return runShell(command);
#endif
}

// When this cpp11 was compiled, DD-MM-YYYY HH:MM:SS as the seal date is written.
static std::string buildStamp() {
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__;   // "Oct  2 2026"
    const int month = static_cast<int>((std::strstr(months, std::string(d, 3).c_str()) - months) / 3) + 1;
    char out[32];
    std::snprintf(out, sizeof out, "%02d-%02d-%s %s", std::atoi(d + 4), month, d + 7, __TIME__);
    return out;
}

// Only where the tool was asked for by name, as Visual Studio's ml64 and link are: a tool named by
// its path - RIDE's own masm and link - was found and ran, and its messages above are the answer.
static void noteWindowsToolchain(const std::string &command) {
    if (!hostIsWindows()) return;
    if (programOf(command).find_first_of("/\\") != std::string::npos) return;
    std::fprintf(stderr, "  ml64 and link ship with Visual Studio and reach "
                         "PATH only after vcvars64.bat has run - a Developer "
                         "Command Prompt is that same environment.\n");
}

// **`c++` and not `cc`, because operator new lives in the C++ runtime.** The
// four allocation operators are in libc++ or libstdc++ and the C driver links
// neither; `c++` assembles a .s exactly as `cc` does, and rung 6 wants it too.
const char *Driver::hostCompiler() {
    const char *env = std::getenv(program::env("CC").c_str());
    return (env != nullptr && env[0] != '\0') ? env : "c++";
}

const char *Driver::hostAssembler(Syntax syntax) {
    const char *env = std::getenv(program::env("AS").c_str());
    if (env != nullptr && env[0] != '\0') return env;
    return syntax == Syntax::Ml64 ? "ml64.exe" : "masm.exe";
}

// **clang, and it is asked for the Microsoft target explicitly.** Its default
// target is whatever it was built for; this has to produce COFF for the linker
// beside it, and the triple is what says so.
const char *Driver::hostGnuAssembler() {
    static std::string found;
    static bool searched = false;
    if (searched) return found.c_str();
    searched = true;

    const char *env = std::getenv(program::env("AS").c_str());
    if (env != nullptr && env[0] != '\0') { found = env; return found.c_str(); }

#ifdef _WIN32
    // **Wherever Visual Studio put its clang, in any edition** - the "C++ Clang
    // tools" component - then a standalone LLVM, then PATH.
    std::vector<std::string> tries;
    const std::string llvm = "\\VC\\Tools\\Llvm\\x64\\bin\\clang.exe";
    const char *vc = std::getenv("VCINSTALLDIR");
    if (vc != nullptr && vc[0] != '\0')
        tries.push_back(std::string(vc) + "Tools\\Llvm\\x64\\bin\\clang.exe");
    const std::string vs = askVswhere();
    if (!vs.empty()) tries.push_back(vs + llvm);
    const char *pf = std::getenv("ProgramFiles");
    if (pf != nullptr && pf[0] != '\0') {
        static const char *const editions[] = {
            "Community", "Professional", "Enterprise", "BuildTools"
        };
        for (const char *e : editions)
            tries.push_back(std::string(pf) + "\\Microsoft Visual Studio\\2022\\" + e + llvm);
        tries.push_back(std::string(pf) + "\\LLVM\\bin\\clang.exe");
    }
    char onPath[MAX_PATH];
    if (SearchPathA(nullptr, "clang.exe", nullptr, MAX_PATH, onPath, nullptr) > 0)
        tries.push_back(onPath);
    for (std::size_t i = 0; i < tries.size(); i++) {
        std::ifstream probe(tries[i].c_str());
        if (probe.good()) { found = tries[i]; return found.c_str(); }
    }
    return found.c_str();   // "" - none here: the caller falls back to masm, or says so
#else
    found = "clang";
    return found.c_str();
#endif
}

const char *Driver::hostLinker() {
    const char *env = std::getenv(program::env("LD").c_str());
    return (env != nullptr && env[0] != '\0') ? env : "link.exe";
}

bool Driver::targetIsTi() const {
    return std::strcmp(backend_->name(), "tms6747") == 0;
}

// **A file name with `*` or `?` in it, expanded here**, as cl does: cmd hands the pattern through as
// written, and a POSIX shell only when it was quoted. Sorted; a file already named is not taken twice.
static bool expandPattern(const std::string &arg, std::vector<std::string> &into) {
    if (arg.find_first_of("*?") == std::string::npos || std::ifstream(arg.c_str()).good()) {
        if (std::find(into.begin(), into.end(), arg) == into.end()) into.push_back(arg);
        return true;
    }
    std::vector<std::string> found;
#ifdef _WIN32
    const std::size_t slash = arg.find_last_of("/\\");
    const std::string dir = slash == std::string::npos ? std::string() : arg.substr(0, slash + 1);
    WIN32_FIND_DATAA entry;
    HANDLE h = FindFirstFileA(arg.c_str(), &entry);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) found.push_back(dir + entry.cFileName);
        } while (FindNextFileA(h, &entry));
        FindClose(h);
    }
    std::sort(found.begin(), found.end());
#else
    glob_t g;
    if (glob(arg.c_str(), 0, nullptr, &g) == 0)
        for (std::size_t k = 0; k < g.gl_pathc; k++) found.push_back(g.gl_pathv[k]);
    globfree(&g);
#endif
    for (const std::string &name : found)
        if (std::find(into.begin(), into.end(), name) == into.end()) into.push_back(name);
    return !found.empty();
}

// **Two jobs never write one file**: -j runs them at once, and dir1/*.cpp and dir2/*.cpp may each
// hold an x.cpp whose object is x.o. Refused by name before any job starts.
static bool distinctOutputs(const char *program, const std::vector<std::string> &inputs,
                            const std::vector<std::string> &outputs) {
    for (std::size_t i = 0; i < outputs.size(); i++)
        for (std::size_t k = 0; k < i; k++)
            if (!outputs[i].empty() && outputs[i] == outputs[k]) {
                std::fprintf(stderr, "%s: %s and %s would both be written to %s\n", program,
                             inputs[k].c_str(), inputs[i].c_str(), outputs[i].c_str());
                return false;
            }
    return true;
}

// A file beside this program - RIDE lays asm6x.exe beside cpp11.exe - or
// nothing, when argv[0] was a bare name found on PATH.
static std::string besideProgram(const std::string &program, const char *leaf) {
    std::size_t slash = program.find_last_of("/\\");
    if (slash == std::string::npos) return std::string();
    std::string path = program.substr(0, slash + 1) + leaf;
    std::ifstream probe(path.c_str());
    return probe.good() ? path : std::string();
}

// **The project's own masm and LINK, beside this program** - RIDE lays them beside cpp11.exe - for
// the MASM spelling, unless CPP11_AS or CPP11_LD names another; else the names, on PATH.
std::string Driver::masmAssembler() const {
    const char *env = std::getenv(program::env("AS").c_str());
    if (env != nullptr && env[0] != '\0') return env;
    std::string own = syntax_ == Syntax::Masm ? besideProgram(program_, "masm.exe") : std::string();
    return own.empty() ? std::string(hostAssembler(syntax_)) : own;
}

std::string Driver::windowsLinker() const {
    const char *env = std::getenv(program::env("LD").c_str());
    if (env != nullptr && env[0] != '\0') return env;
    std::string own = syntax_ == Syntax::Masm ? besideProgram(program_, "link.exe") : std::string();
    return own.empty() ? std::string(hostLinker()) : own;
}

// **asm6x, the project's C6000 assembler, which runs on every host.** CPP11_AS
// names another; else the one beside this program; else the name, on PATH.
std::string Driver::tiAssembler() const {
    const char *env = std::getenv(program::env("AS").c_str());
    if (env != nullptr && env[0] != '\0') return env;
    std::string found = besideProgram(program_, "asm6x.exe");
    if (found.empty()) found = besideProgram(program_, "asm6x");
    return found.empty() ? std::string("asm6x") : found;
}

// **TI's linker, which only a machine with CCS has.** CPP11_LD names it; else
// CPP11_TI names the C6000 compiler directory, the one with bin\lnk6x and
// lib\rts6740_elf.lib; else lnk6x is asked for by name.
std::string Driver::tiLinker() const {
    const char *env = std::getenv(program::env("LD").c_str());
    if (env != nullptr && env[0] != '\0') return env;
    const char *ti = std::getenv(program::env("TI").c_str());
    if (ti != nullptr && ti[0] != '\0') {
        std::string dir = std::string(ti) + (hostIsWindows() ? "\\bin\\" : "/bin/");
        std::ifstream exe((dir + "lnk6x.exe").c_str());
        if (exe.good()) return dir + "lnk6x.exe";
        return dir + "lnk6x";
    }
    std::string own = besideProgram(program_, "lnk6x.exe");
    if (own.empty()) own = besideProgram(program_, "lnk6x");
    return own.empty() ? std::string("lnk6x") : own;
}

// The per-job folders the temporaries sit in, removed once their files are.
static std::vector<std::string> &temporaryFolders() {
    static std::vector<std::string> folders;
    return folders;
}

static void removeFolders() {
    std::vector<std::string> &folders = temporaryFolders();
#ifdef _WIN32
    for (const std::string &d : folders) _rmdir(d.c_str());
#else
    for (const std::string &d : folders) rmdir(d.c_str());
#endif
    folders.clear();
}

// **A temporary named after its source**, in a folder of its own: the assembler records its input's
// name in the object, so isalpha.cpp assembles as isalpha.s and two builds give the same bytes.
std::string Driver::temporaryName(int index, const std::string &source) {
    std::string stem = source;
    std::size_t slash = stem.find_last_of(hostIsWindows() ? "/\\" : "/");
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    std::size_t dot = stem.rfind('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    std::string name = temporaryName(index);
    if (stem.empty()) return name;
    std::string folder = name.substr(0, name.size() - 2);
#ifdef _WIN32
    if (_mkdir(folder.c_str()) != 0) return name;
#else
    if (mkdir(folder.c_str(), 0700) != 0) return name;
#endif
    temporaryFolders().push_back(folder);
    return folder + (hostIsWindows() ? "\\" : "/") + stem + ".s";
}

std::string Driver::temporaryName(int index) {
    const char *dir = std::getenv("TMPDIR");
    if (dir == nullptr || dir[0] == '\0') dir = std::getenv("TEMP");
    if (dir == nullptr || dir[0] == '\0') dir = std::getenv("TMP");
    std::string base = (dir != nullptr && dir[0] != '\0') ? dir : "/tmp";
    while (!base.empty() && (base[base.size() - 1] == '/' ||
                             base[base.size() - 1] == '\\'))
        base.erase(base.size() - 1);
    return base + (hostIsWindows() ? "\\" : "/") + program::kName + "-" +
           std::to_string(static_cast<long>(getpid())) + "-" +
           std::to_string(index) + ".s";
}

static std::vector<std::string> &temporaryNames() {
    static std::vector<std::string> names;
    return names;
}

void Driver::removeTemporaries() {
    std::vector<std::string> &names = temporaryNames();
    for (const std::string &t : names) std::remove(t.c_str());
    names.clear();
    removeFolders();
    temporaries_.clear();
}

static std::string shellQuote(const std::string &s) {
    if (hostIsWindows()) {
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else          out += c;
        }
        return out + "\"";
    }
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    return out + "'";
}

void Driver::addMacroEdit(const char *text, bool undef) {
    std::string s(text);
    std::size_t eq = s.find('=');
    if (undef || eq == std::string::npos)
        macroEdits_.push_back(MacroEdit{ s, undef ? "" : "1", undef });
    else
        macroEdits_.push_back(MacroEdit{ s.substr(0, eq), s.substr(eq + 1), false });
}

std::vector<std::pair<std::string, std::string> > Driver::macrosFor() const {
    std::vector<std::pair<std::string, std::string> > macros =
        predefinedMacros(*backend_);

    for (const MacroEdit &e : macroEdits_) {
        for (std::size_t i = macros.size(); i-- > 0; )
            if (macros[i].first == e.name)
                macros.erase(macros.begin() + static_cast<long>(i));
        if (!e.undef) macros.push_back(std::make_pair(e.name, e.value));
    }
    return macros;
}

bool Driver::assembleObjects() {
    // Built first and run afterwards: composing a command touches this object's
    // state, and only the running has to be concurrent.
    std::vector<std::string> commands;
    commands.reserve(temporaries_.size());
    for (std::size_t i = 0; i < temporaries_.size(); i++) {
        std::string command;
        if (targetIsTi()) {
            command = shellQuote(tiAssembler());
            if (!asmCompress_.empty()) command += " " + asmCompress_;
            command += " " + shellQuote(temporaries_[i]);
            command += " -o " + shellQuote(objects_[i]);
        } else if (hostIsWindows() && syntax_ == Syntax::Gnu) {
            // **The GNU spelling is assembled by clang, not by ml64.** ml64
            // has no COMDAT directive, so every mergeable definition - a
            // vtable, an inline member, a template's.
            command = shellQuote(hostGnuAssembler());
            command += " -target x86_64-pc-windows-msvc -c ";
            command += shellQuote(temporaries_[i]);
            command += " -o " + shellQuote(objects_[i]);
        } else if (hostIsWindows()) {
            command = shellQuote(masmAssembler());
            command += " /nologo /c /Fo " + shellQuote(objects_[i]);
            command += " " + shellQuote(temporaries_[i]);
        } else {
            command = shellQuote(hostCompiler());

            if (debug_) command += " -g";
            command += " -c " + shellQuote(temporaries_[i]);
            command += " -o " + shellQuote(objects_[i]);
        }
        commands.push_back(command);
    }
    return runCommands(commands);
}

// The linker command file lnk6x needs: one flat memory for the C6747 and
// every section the compilers and TI's runtime write placed in it - the same
// file RIDE and VM6747's tests link with. **An 8 MB stack, as the other three targets give**: 16 KB
// put a 100,000-deep recursion into the sections below it (op11 of the suite).
static const char *const kTiLinkCommands =
    "--rom_model\n"
    "--stack_size=0x800000\n"
    "--heap_size=0x100000\n"
    "MEMORY\n"
    "{\n"
    "    RAM : origin = 0xC0000000, length = 0x04000000\n"
    "}\n"
    "SECTIONS\n"
    "{\n"
    "    .text        > RAM\n"
    "    .const       > RAM\n"
    "    .data        > RAM\n"
    "    .bss         > RAM\n"
    "    .far         > RAM\n"
    "    .fardata     > RAM\n"
    "    .neardata    > RAM\n"
    "    .rodata      > RAM\n"
    "    .cinit       > RAM\n"
    "    .init_array  > RAM\n"
    "    .switch      > RAM\n"
    "    .cio         > RAM\n"
    "    .stack       > RAM\n"
    "    .sysmem      > RAM\n"
    "    .vm6747.eh   > RAM\n"
    "}\n";

static bool fileExists(const std::string &path) {
    std::ifstream probe(path.c_str());
    return probe.good();
}

// **A TI program: every .s through asm6x, the objects through lnk6x against
// TI's runtime** - the exception-handling build where CPP11_TILIB names one,
// since CCS ships only the other and a C++ program that throws needs it.
bool Driver::linkTi() {
    std::vector<std::string> objects, steps;
    for (const std::string &t : temporaries_) {
        std::size_t dot = t.rfind('.');
        std::string obj = (dot == std::string::npos ? t : t.substr(0, dot)) + ".obj";
        steps.push_back(shellQuote(tiAssembler()) + (asmCompress_.empty() ? "" : " " + asmCompress_) + " " + shellQuote(t) +
                        " -o " + shellQuote(obj));
        objects.push_back(obj);
        temporaryNames().push_back(obj);
    }
    if (!runCommands(steps)) return false;

    std::string cmdfile = temporaryName(static_cast<int>(temporaries_.size()));
    cmdfile.replace(cmdfile.size() - 2, 2, ".cmd");
    {
        std::ofstream out(cmdfile.c_str());
        if (!out) {
            std::fprintf(stderr, "%s: cannot write %s\n", program_.c_str(), cmdfile.c_str());
            return false;
        }
        out << kTiLinkCommands;
    }
    temporaryNames().push_back(cmdfile);

    const std::string sep = hostIsWindows() ? "\\" : "/";
    std::vector<std::string> libraryDirs;
    const char *ti = std::getenv(program::env("TI").c_str());
    const char *tilib = std::getenv(program::env("TILIB").c_str());
    const bool tiNamed = (ti != nullptr && ti[0] != '\0') || (tilib != nullptr && tilib[0] != '\0');
    // **RTS6x, the project's own runtime, as RIDE links it** (review D9): -rts= names it or asks for TI's;
    // else RTS6X, or lib/tms6747 beside this program (lib/rts6x-tms6747 in an earlier installation), unless CPP11_TI
    // or CPP11_TILIB names TI's.
    std::string ours, rts;
    if (rtsChoice_ != "ti") {
        std::string want = rtsChoice_;
        const char *env = std::getenv("RTS6X");
        if (want.empty() && !tiNamed && env != nullptr && env[0] != '\0') want = env;
        if (want.empty() && !tiNamed) {
            std::string beside = besideProgram(program_, ("lib" + sep + "tms6747" + sep + "rts6x.lib").c_str());
            if (beside.empty()) beside = besideProgram(program_, ("lib" + sep + "rts6x-tms6747" + sep + "rts6x.lib").c_str());
            if (!beside.empty()) want = beside.substr(0, beside.size() - std::strlen("rts6x.lib") - 1);
        }
        if (!want.empty()) {
            const bool named = want.size() > 4 && want.compare(want.size() - 4, 4, ".lib") == 0;
            const std::size_t cut = want.find_last_of("/\\");
            ours = named ? (cut == std::string::npos ? std::string(".") : want.substr(0, cut)) : want;
            rts = named ? want.substr(cut == std::string::npos ? 0 : cut + 1) : std::string("rts6x.lib");
            if (!fileExists(ours + sep + rts)) {
                std::fprintf(stderr, "%s: no %s in %s - -rts= names RTS6x's directory or its .lib, "
                                     "or -rts=ti asks for TI's rts6740\n", program_.c_str(), rts.c_str(), ours.c_str());
                return false;
            }
        }
    }
    if (!ours.empty()) {
        libraryDirs.push_back(ours);
    } else {
        if (ti != nullptr && ti[0] != '\0') libraryDirs.push_back(std::string(ti) + sep + "lib");
        if (tilib != nullptr && tilib[0] != '\0') libraryDirs.push_back(tilib);
        rts = "rts6740_elf.lib";
        for (const std::string &d : libraryDirs)
            if (fileExists(d + sep + "rts6740_elf_eh.lib")) rts = "rts6740_elf_eh.lib";
    }
    if (!quiet_)
        std::fprintf(stderr, "%s: linking against %s%s%s\n", program_.c_str(),
                     ours.empty() ? "TI's " : "RTS6x, ", ours.empty() ? rts.c_str() : (ours + sep + rts).c_str(),
                     ours.empty() && libraryDirs.empty() ? ", on lnk6x's own search path" : "");

    std::string command = shellQuote(tiLinker()) + " -mv6740 --abi=eabi";
    for (const std::string &d : libraryDirs) command += " -i " + shellQuote(d);
    command += " " + shellQuote(cmdfile);
    for (const std::string &o : objects) command += " " + shellQuote(o);
    for (const std::string &o : alreadyObjects_) command += " " + shellQuote(o);
    command += " -l " + rts + " -o " + shellQuote(linkTo_);

    // a link that takes long says so while it does (1.70)
    const std::string linking = "linking " + linkTo_;
    int rc;
    {
        Heartbeat beat(!quiet_, [&linking]() -> std::string { return linking; });
        rc = runTool(command);
    }
    if (rc != 0) {
        std::fprintf(stderr, "%s: the linker failed - the command was:\n  %s\n",
                     program_.c_str(), command.c_str());
        if (ours.empty())
            std::fprintf(stderr, "  rts6740_elf.lib is TI's, under CCS's C6000 "
                                 "compiler directory: CPP11_TI names it, CPP11_TILIB a "
                                 "directory holding rts6740_elf_eh.lib, CPP11_LD the "
                                 "linker; -rts= names RTS6x in its place.\n");
        return false;
    }
    return true;
}

bool Driver::link() {
    if (targetIsTi()) return linkTi();
    std::string command;
    if (hostIsWindows()) {

        // Windows assembles each file itself before linking, where the Unix
        // path hands every .s to one `c++` invocation. Same batch, same pool.
        std::vector<std::string> objects, steps;
        objects.reserve(temporaries_.size());
        steps.reserve(temporaries_.size());
        for (const std::string &t : temporaries_) {
            std::size_t dot = t.rfind('.');
            std::string obj = (dot == std::string::npos ? t : t.substr(0, dot))
                              + ".obj";
            // The same choice assembleObjects makes, and for the same reason -
            // ml64 cannot mark a mergeable definition COMDAT and clang can.
            std::string step;
            if (syntax_ == Syntax::Gnu) {
                step = shellQuote(hostGnuAssembler());
                step += " -target x86_64-pc-windows-msvc -c " + shellQuote(t);
                step += " -o " + shellQuote(obj);
            } else {
                step = shellQuote(masmAssembler());
                step += " /nologo /c /Fo " + shellQuote(obj) + " " + shellQuote(t);
            }
            steps.push_back(step);
            objects.push_back(obj);
            // Recorded first, so a failure still cleans up what was written
            temporaryNames().push_back(obj);
        }
        if (!runCommands(steps)) return false;

        command = shellQuote(windowsLinker());
        // **An 8 MB stack, which is what the other two targets already give.**
        command += " /nologo /subsystem:console /stack:8388608 /out:"
                 + shellQuote(linkTo_);
        // -g's CodeView goes into a PDB beside the program, which is where cdb looks.
        if (debug_) command += " /debug";
        for (const std::string &o : objects) command += " " + shellQuote(o);
        for (const std::string &o : alreadyObjects_) command += " " + shellQuote(o);

        // **GRIDE's runtime when it is installed beside this program** (lib\x86_64-windows\grtc.lib,
        // docs\Runtime_Design.md 13.1): its five layered libraries and kernel32.lib by path, the d variants under
        // -g, and no LIB to find - so a program built here runs where there is no Visual Studio, as one built by
        // GRIDE does. Else Microsoft's static CRT through LIB: libcpmt holds std::set_new_handler and
        // get_new_handler, and no other static library does.
        const std::string grtc = besideProgram(program_, "lib\\x86_64-windows\\grtc.lib");
        if (!grtc.empty()) {
            const std::string dir = grtc.substr(0, grtc.size() - std::strlen("grtc.lib"));
            const char *variant = debug_ ? "d" : "";
            static const char *const layers[] = { "grtstart", "grtcompat", "grtcxx", "grtc", "grtwin" };
            for (const char *layer : layers) command += " " + shellQuote(dir + layer + variant + ".lib");
            command += " " + shellQuote(dir + "kernel32.lib");
            if (!quiet_)
                std::fprintf(stderr, "%s: linking against GRIDE's runtime in %s\n", program_.c_str(), dir.c_str());
        } else {
            command += " libcmt.lib libcpmt.lib libucrt.lib libvcruntime.lib kernel32.lib"
                       " legacy_stdio_definitions.lib oldnames.lib";
        }
    } else {
        command = shellQuote(hostCompiler());

        if (debug_) command += " -g";
        for (const std::string &t : temporaries_) command += " " + shellQuote(t);
        for (const std::string &o : alreadyObjects_) command += " " + shellQuote(o);
        command += " -o " + shellQuote(linkTo_);

        command += " -lm";
    }

    // a link that takes long says so while it does (1.70)
    const std::string linking = "linking " + linkTo_;
    int rc;
    {
        Heartbeat beat(!quiet_, [&linking]() -> std::string { return linking; });
        rc = runTool(command);
    }
    if (rc != 0) {
        std::fprintf(stderr, "%s: the assembler or linker failed - the command "
                             "was:\n  %s\n", program_.c_str(), command.c_str());
        noteWindowsToolchain(command);
        return false;
    }
    return true;
}

std::string Driver::assemblyNameFor(const std::string &source) {
    std::size_t dot = source.rfind('.');
    std::size_t slash = source.find_last_of('/');
    bool hasSuffix = dot != std::string::npos &&
                     (slash == std::string::npos || dot > slash);
    return hasSuffix ? source.substr(0, dot) + ".s" : source + ".s";
}

std::string Driver::objectNameFor(const std::string &source) const {
    std::size_t slash = source.find_last_of(hostIsWindows() ? "/\\" : "/");
    std::string base = slash == std::string::npos ? source
                                                  : source.substr(slash + 1);
    std::size_t dot = base.rfind('.');
    // .obj for the TI target, which is what asm6x and TI's tools call one
    return (dot == std::string::npos ? base : base.substr(0, dot)) +
           (targetIsTi() ? ".obj" : ".o");
}

bool Driver::parseArguments(int argc, char **argv) {
    std::vector<std::string> inputs;
    std::string output;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-o") == 0) {
            if (++i == argc) {
                std::fprintf(stderr, "%s: -o needs a file name\n", argv[0]);
                return false;
            }
            output = argv[i];
        } else if (std::strncmp(argv[i], "-I", 2) == 0) {
            const char *dir = argv[i][2] != '\0' ? argv[i] + 2 : nullptr;
            if (!dir) {
                if (++i == argc) {
                    std::fprintf(stderr, "%s: -I needs a directory\n", argv[0]);
                    return false;
                }
                dir = argv[i];
            }
            searchPath_.push_back(dir);
        } else if (std::strncmp(argv[i], "-j", 2) == 0) {
            const char *n = argv[i][2] != '\0' ? argv[i] + 2 : nullptr;
            if (!n) {
                if (++i == argc) {
                    std::fprintf(stderr, "%s: -j needs a number\n", argv[0]);
                    return false;
                }
                n = argv[i];
            }
            char *end = nullptr;
            long value = std::strtol(n, &end, 10);
            if (*n == '\0' || (end && *end != '\0') || value < 1) {
                std::fprintf(stderr,
                    "%s: -j needs a positive number of jobs, not '%s'\n", argv[0], n);
                return false;
            }
            threads_ = static_cast<unsigned>(value);
        } else if (std::strncmp(argv[i], "-masm=", 6) == 0) {
            const char *want = argv[i] + 6;
            syntaxNamed_ = true;
            if (std::strcmp(want, "gnu") == 0) {
                syntax_ = Syntax::Gnu;
            } else if (std::strcmp(want, "masm") == 0 ||
                       std::strcmp(want, "intel") == 0) {
                syntax_ = Syntax::Masm;
            } else if (std::strcmp(want, "ml64") == 0) {
                syntax_ = Syntax::Ml64;
            } else {
                std::fprintf(stderr,
                    "%s: -masm= takes 'gnu', 'masm' or 'ml64', not '%s'\n", argv[0], want);
                return false;
            }
        } else if (std::strncmp(argv[i], "-arch", 5) == 0) {
            const char *name = argv[i][5] == '=' ? argv[i] + 6 : nullptr;
            if (!name) {
                if (++i == argc) {
                    std::fprintf(stderr, "%s: -arch needs a name - one of %s\n",
                                 argv[0], backendNames().c_str());
                    return false;
                }
                name = argv[i];
            }
            backend_ = findBackend(name);
            if (backend_ == nullptr) {
                std::fprintf(stderr, "%s: unknown architecture '%s' - one of %s\n",
                             argv[0], name, backendNames().c_str());
                return false;
            }
            if (!backend_->emits()) {
                std::fprintf(stderr, "%s: the %s backend is not written yet - it "
                             "knows what its types measure but has no instructions\n",
                             argv[0], backend_->name());
                return false;
            }
        } else if (std::strncmp(argv[i], "-D", 2) == 0 ||
                   std::strncmp(argv[i], "-U", 2) == 0) {
            bool undef = argv[i][1] == 'U';
            const char *text = argv[i][2] != '\0' ? argv[i] + 2 : nullptr;
            if (!text) {
                if (++i == argc) {
                    std::fprintf(stderr, "%s: -%c needs a name\n",
                                 argv[0], undef ? 'U' : 'D');
                    return false;
                }
                text = argv[i];
            }
            if (text[0] == '\0' || text[0] == '=') {
                std::fprintf(stderr, "%s: -%c needs a name before the '='\n",
                             argv[0], undef ? 'U' : 'D');
                return false;
            }
            addMacroEdit(text, undef);
        } else if (std::strcmp(argv[i], "-E") == 0) {
            preprocessOnly_ = true;
        } else if (std::strcmp(argv[i], "-S") == 0) {
            assemblyOnly_ = true;
        } else if (std::strcmp(argv[i], "-c") == 0) {
            objectOnly_ = true;
        } else if (std::strcmp(argv[i], "-time") == 0) {
            timing_ = true;
        } else if (std::strcmp(argv[i], "-version") == 0 ||
                   std::strcmp(argv[i], "--version") == 0) {
            // Printed on stdout, unlike the banner: a version somebody asked
            // for is the answer to the command, not an aside beside it.
            // __DATE__ and __TIME__ are the build machine's local clock, whose zone is not known here.
            // The first line names the program and its number, as every program of the toolchain does (Version.h).
            std::printf("%s %s - %s\n%s\nSealed %s\nBuilt %s, the build machine's local time\n", program::kName,
                        CXX1_VERSION, CXX1_WHAT, CXX1_OWNER, CXX1_SEAL_DATE, buildStamp().c_str());
            std::exit(0);
        } else if (std::strncmp(argv[i], "-rts=", 5) == 0 && argv[i][5] != '\0') {
            rtsChoice_ = argv[i] + 5;
        } else if (std::strcmp(argv[i], "--no_compress") == 0 || std::strcmp(argv[i], "--compress") == 0) {
            asmCompress_ = argv[i];
        } else if (std::strcmp(argv[i], "-nologo") == 0) {
            quiet_ = true;
        } else if (std::strcmp(argv[i], "-O0") == 0 || std::strcmp(argv[i], "-O1") == 0 ||
                   std::strcmp(argv[i], "-O2") == 0) {
            optimize_ = argv[i][2] - '0';
            forSize_ = false;
        } else if (std::strcmp(argv[i], "-Os") == 0) {
            optimize_ = 1;
            forSize_ = true;
        } else if (std::strcmp(argv[i], "-g") == 0) {
            debug_ = true;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            std::fprintf(stderr, "%s: unknown option %s\n", argv[0], argv[i]);
            return false;
        } else if (!expandPattern(argv[i], inputs)) {
            std::fprintf(stderr, "%s: no file matches %s\n", argv[0], argv[i]);
            return false;
        }
    }

    // C++ first, then C.
    const std::size_t givenDirectories = searchPath_.size();
    standardIncludeDirectories(argv[0]);
    systemPath_.assign(searchPath_.begin() + static_cast<long>(givenDirectories), searchPath_.end());

    if (inputs.empty()) { usage(argv[0]); return false; }

    // -E: each input after phase 4, on stdout, and nothing compiled.
    if (preprocessOnly_) {
        try {
            for (const std::string &in : inputs)
                std::fputs(Preprocessor(in, searchPath_, macrosFor()).run().text().c_str(), stdout);
        } catch (const CompileFailed &) {
            std::exit(1);  // the diagnostic is written; one thread here, so the exit is the verdict
        }
        std::exit(0);
    }

    // **The project's own masm and LINK when they are beside this program, and no spelling asked
    // for**: they take COMDAT and a throw, so clang is wanted only for -g's line table.
    if (hostIsWindows() && !syntaxNamed_ && std::strcmp(backend_->name(), "x86_64-windows") == 0 &&
        !besideProgram(program_, "masm.exe").empty() && !(debug_ && hostGnuAssembler()[0] != '\0'))
        syntax_ = Syntax::Masm;

    // No clang and no masm beside this program: masm by name, on PATH, rather than every compile failing.
    if (hostIsWindows() && syntax_ == Syntax::Gnu && !syntaxNamed_ && !assemblyOnly_ &&
        std::strcmp(backend_->name(), "x86_64-windows") == 0 && hostGnuAssembler()[0] == '\0') {
        syntax_ = Syntax::Masm;
        std::fprintf(stderr, "%s: no clang and no masm.exe beside this program - assembling with the "
                             "masm.exe on PATH; -g, which needs clang, is not available\n", argv[0]);
    }

    if (hostIsWindows() && syntax_ == Syntax::Gnu && syntaxNamed_ && !assemblyOnly_ &&
        std::strcmp(backend_->name(), "x86_64-windows") == 0 && hostGnuAssembler()[0] == '\0') {
        std::fprintf(stderr, "%s: -masm=gnu is assembled by clang, and none was found - install "
                             "Visual Studio's \"C++ Clang tools\" component, name one with %s, "
                             "or use -masm=masm\n", argv[0], program::env("AS").c_str());
        return false;
    }

    if (debug_ && optimize_ > 0 && std::strcmp(backend_->name(), "tms6747") == 0) {
        std::fprintf(stderr, "%s: -g with -O is not supported yet for tms6747 - the C6000 scheduler does not "
                             "carry a source position through its packets; compile with -g alone\n", argv[0]);
        return false;
    }
    if (debug_ && !backend_->emitsLineTable(syntax_)) {
        std::fprintf(stderr,
                     "%s: -g asks where each line of C++ went, and this compiler "
                     "writes no such thing for %s with -masm=ml64: MASM carries no "
                     "line table and ml64 builds none from it. -masm=masm writes the "
                     ".FILE and .LINE directives GRIDE's masm reads, -masm=gnu a line "
                     "table clang assembles; or compile without -g.\n",
                     argv[0], backend_->name());
        return false;
    }

    // **An object file is an input too**, and it is not compiled: it is handed
    // to the linker with everything this run produces.
    {
        std::vector<std::string> sources;
        for (std::size_t i = 0; i < inputs.size(); i++) {
            const std::string &in = inputs[i];
            const std::size_t dot = in.find_last_of('.');
            const std::string ext = dot == std::string::npos
                                  ? std::string() : in.substr(dot);
            if (ext == ".o" || ext == ".obj" || ext == ".a" || ext == ".lib")
                alreadyObjects_.push_back(in);
            else
                sources.push_back(in);
        }
        inputs.swap(sources);
        if (!alreadyObjects_.empty() && (assemblyOnly_ || objectOnly_)) {
            std::fprintf(stderr,
                "%s: %s was given with %s, and there is nothing to compile in "
                "it - an object file is an input to the link step only\n",
                argv[0], alreadyObjects_[0].c_str(),
                assemblyOnly_ ? "-S" : "-c");
            return false;
        }
        if (inputs.empty() && alreadyObjects_.empty()) { usage(argv[0]); return false; }
    }

    // A .c is turned away by name - the mirror of cc1 refusing a .cpp. cxx1
    // compiles C++, and C read as C++ miscompiles where they disagree rather
    // than stopping; say so and point at cc1. (.cpp/.cc/.cxx are cxx1's own.)
    for (std::size_t k = 0; k < inputs.size(); k++) {
        const std::size_t dot = inputs[k].find_last_of('.');
        if (dot != std::string::npos && inputs[k].substr(dot) == ".c") {
            std::fprintf(stderr,
                "%s: %s looks like C (.c), and %s compiles C++, not C - "
                "compile it with c90\n",
                argv[0], inputs[k].c_str(), program::kName);
            return false;
        }
    }

    if (!asmCompress_.empty() && !targetIsTi()) {
        std::fprintf(stderr, "%s: %s is for -arch tms6747, whose compact instructions it turns on or off\n",
                     argv[0], asmCompress_.c_str());
        return false;
    }
    if (assemblyOnly_ && objectOnly_) {
        std::fprintf(stderr, "%s: -S and -c ask for different things - -S stops "
                             "at assembly, -c goes one step further to an "
                             "object\n", argv[0]);
        return false;
    }

    if (assemblyOnly_) {
        if (!output.empty() && inputs.size() > 1) {
            std::fprintf(stderr,
                "%s: -o names a single output, but %zu inputs were given\n",
                argv[0], inputs.size());
            return false;
        }
        std::vector<std::string> outs;
        for (const std::string &in : inputs) {
            if (!output.empty()) jobs_.push_back(Job{ in, output });
            else if (toStdout_)  jobs_.push_back(Job{ in, "" });
            else                 jobs_.push_back(Job{ in, assemblyNameFor(in) });
            outs.push_back(jobs_.back().output);
        }
        return distinctOutputs(argv[0], inputs, outs);
    }

    if (backend_ != &defaultBackend() && !targetIsTi()) {
        std::fprintf(stderr,
            "%s: cannot assemble %s code on this machine, which is %s - use -S "
            "to write the assembly and take it there\n",
            argv[0], backend_->name(), defaultBackend().name());
        return false;
    }

    if (objectOnly_ && !output.empty() && inputs.size() > 1) {
        std::fprintf(stderr,
            "%s: -o names a single object, but %zu inputs were given\n",
            argv[0], inputs.size());
        return false;
    }

    if (!objectOnly_)
        linkTo_ = !output.empty() ? output
                : (hostIsWindows() && !targetIsTi() ? "a.exe" : "a.out");

    for (std::size_t i = 0; i < inputs.size(); i++) {
        std::string temp = temporaryName(static_cast<int>(i), inputs[i]);
        temporaries_.push_back(temp);
        temporaryNames().push_back(temp);
        jobs_.push_back(Job{ inputs[i], temp });
        if (objectOnly_) objects_.push_back(output.empty()
                                            ? objectNameFor(inputs[i]) : output);
    }
    return !objectOnly_ || distinctOutputs(argv[0], inputs, objects_);
}

bool Driver::compile(const Job &job) {
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };

    const Target &target = backend_->target();
    TypeTable types;

    auto t0 = Clock::now();
    Source src = Preprocessor(job.input, searchPath_, macros_).run();
    auto t1 = Clock::now();

    std::vector<Token> tokens = Lexer(src).tokenize();
    auto t2 = Clock::now();

    const std::vector<Token> probeTokens = tokens;
    src.setSystemDirectories(systemPath_);
    Parser parser(src, std::move(tokens), types, target, backend_->abi());
    Program program;
    try {
        program = parser.parse();
    } catch (const CompileFailed &) {
        if (src.failed()) reportTheRest(src, probeTokens, parser.failedAt(), target);
        throw;
    }
    // **[temp.res]/8-10: what a template the program never instantiates says wrong of itself** - made with
    // `int` and with `double` for its type parameters, each in a parser of its own; a failure both readings
    // meet in the same words at the same place, in a statement that names nothing dependent, is reported.
    if (parser.probeCandidates() > 0) {
        std::set<std::size_t> skip;
        for (int round = 0; round < 6; round++) {
            Parser::ProbeResult r[2];
            for (int v = 0; v < 2; v++) {
                TypeTable probeTypes;
                Parser probe(src, probeTokens, probeTypes, target, backend_->abi());
                probe.setProbe(v, skip);
                (void)probe.parse();
                r[v] = probe.probeResult();
            }
            if (!r[0].failed && !r[1].failed) break;
            if (r[0].failed && r[1].failed && r[0].report && r[1].report && r[0].pos == r[1].pos &&
                r[0].why == r[1].why)
                src.fail(r[0].pos, r[0].why);
            bool more = false;
            for (int v = 0; v < 2; v++)
                if (r[v].failed && skip.insert(r[v].templateAt).second) more = true;
            if (!more) break;
        }
    }
    auto t3 = Clock::now();

    // **A file that writes `volatile` is compiled without -O**, said as a
    // `file:line:col: warning:` at the first one (the passes do not ask it);
    // where the backend skips such functions one by one (x86_64, arm64,
    // tms6747), it keeps -O.
    const bool downgrade = program.usesVolatile && !backend_->optimizesAroundVolatile();
    const int level = downgrade ? 0 : optimize_;
    if (optimize_ > 0 && downgrade) {
        volatileDowngrades_++;
        std::fprintf(stderr, "%s: warning: 'volatile' accesses are not yet told apart from plain "
                     "ones by the optimiser, so %s is compiled without -O%d\n",
                     program.volatileAt.empty() ? job.input.c_str() : program.volatileAt.c_str(),
                     job.input.c_str(), optimize_);
    }

    bool ok = true;
    std::ostringstream made;
    {
        std::unique_ptr<CodeGen> gen = backend_->codegen(made, syntax_);
        if (debug_) gen->setLineSource(&src, workingDirectory());
        gen->setOptimize(level);
        gen->setOptimizeForSize(forSize_ && level > 0);
        gen->run(program);
    }
    std::string text = made.str();
    const std::string refused = spellWideNames(*backend_, &text);
    if (!refused.empty()) {
        std::fprintf(stderr, "%s: %s: %s\n", program_.c_str(), job.input.c_str(), refused.c_str());
        return false;
    }
    if (job.output.empty()) {
        std::cout << text;
    } else {
        // Binary, so the assembly is byte-identical on every host: text mode wrote CRLF on Windows (C5).
        std::ofstream file(job.output, std::ios::binary);
        if (!file) {
            std::fprintf(stderr, "%s: cannot write %s\n", program_.c_str(),
                         job.output.c_str());
            return false;
        }
        file << text;
    }
    auto t4 = Clock::now();

    if (timing_) {
        double read = ms(t0, t1), lex = ms(t1, t2), parse = ms(t2, t3), gen = ms(t3, t4);
        double all = ms(t0, t4);
        std::fprintf(stderr,
            "%s: read+pp %.2f  lex %.2f  parse %.2f  codegen %.2f  total %.2f ms"
            "   (front end %.0f%%)\n",
            job.input.c_str(), read, lex, parse, gen, all,
            all > 0 ? 100.0 * (read + lex + parse) / all : 0.0);
    }
    return ok;
}

unsigned Driver::availableCores() {
#ifdef __linux__
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof allowed, &allowed) == 0) {
        std::vector<std::pair<long, long>> cores;
        for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
            if (!CPU_ISSET(cpu, &allowed)) continue;
            std::string base = "/sys/devices/system/cpu/cpu" +
                               std::to_string(cpu) + "/topology/";
            std::ifstream pkgFile(base + "physical_package_id");
            std::ifstream coreFile(base + "core_id");
            long pkg = 0, core = cpu;
            if (!(pkgFile >> pkg) || !(coreFile >> core)) { pkg = 0; core = cpu; }

            std::pair<long, long> id(pkg, core);
            bool seen = false;
            for (const std::pair<long, long> &k : cores)
                if (k == id) { seen = true; break; }
            if (!seen) cores.push_back(id);
        }
        if (!cores.empty()) return static_cast<unsigned>(cores.size());
    }
#endif
    unsigned n = std::thread::hardware_concurrency();
    return n != 0 ? n : 1;
}

unsigned Driver::threadCount(std::size_t items) const {
    if (threads_ == 1) return 1;

    unsigned want;
    if (threads_ != 0) {
        want = threads_;
    } else {
        if (items < kThreadFrom) return 1;
        want = availableCores();
    }
    if (want > items) want = static_cast<unsigned>(items);
    return want < 1 ? 1 : want;
}

unsigned Driver::threadCount() const { return threadCount(jobs_.size()); }

// **The assembler is a job like any other, and it was the only serial phase
// left.**
bool Driver::runCommands(const std::vector<std::string> &commands) {
    if (commands.empty()) return true;
    const unsigned n = threadCount(commands.size());

    if (timing_)
        std::fprintf(stderr, "%s: %zu tool run%s on %u thread%s\n",
                     program_.c_str(), commands.size(),
                     commands.size() == 1 ? "" : "s", n, n == 1 ? "" : "s");

    // Said when there are several, as the compiles are, and while they run and the screen stands still (1.70):
    // cppparse's 39 assemblies took nine seconds without a word.
    std::atomic<std::size_t> finished{0};
    const bool several = !quiet_ && commands.size() > 1;
    if (several) {
        std::lock_guard<std::mutex> hold(diagnosticLock());
        std::fprintf(stderr, "%zu assembler runs, %u thread%s\n", commands.size(), n, n == 1 ? "" : "s");
        noteSaid();
    }
    Heartbeat beat(several, [&finished, &commands]() -> std::string {
        return "assembling, " + std::to_string(finished.load()) + " of " + std::to_string(commands.size()) + " done";
    });

    std::mutex say;
    std::string failed;
    std::atomic<std::size_t> next{0};
    std::atomic<bool> ok{true};

    // **The first failure by index, not by arrival.**
    std::size_t failedAt = commands.size();

    std::atomic<unsigned> atWork{0};       // threads that have taken their first command, or found none left
    auto work = [&] {
        bool counted = false;
        for (;;) {
            std::size_t i = next.fetch_add(1);
            if (!counted) { atWork.fetch_add(1); counted = true; }
            if (i >= commands.size()) return;
            if (!ok.load()) return;          // somebody failed; stop starting more
            if (runTool(commands[i]) == 0) { finished.fetch_add(1); continue; }
            std::lock_guard<std::mutex> hold(say);
            if (i < failedAt) { failedAt = i; failed = lastToolCommand; }
            ok.store(false);
            return;
        }
    };

    if (n <= 1) {
        work();
    } else {
        // made kThreadSet at a time, each set at work before the next is made - and no more once the work is handed out
        std::vector<std::thread> pool;
        pool.reserve(n);
        while (pool.size() < n && next.load() < commands.size()) {
            const std::size_t upTo = pool.size() + kThreadSet < n ? pool.size() + kThreadSet : n;
            while (pool.size() < upTo) pool.emplace_back(work);
            while (atWork.load() < pool.size()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (std::thread &t : pool) t.join();
    }

    if (!ok.load()) {
        std::fprintf(stderr, "%s: the assembler failed - the command was:\n"
                             "  %s\n", program_.c_str(), failed.c_str());
        noteWindowsToolchain(failed);
        return false;
    }
    return true;
}

bool Driver::compileCaught(const Job &job) {
    try {
        return compile(job);
    } catch (const CompileFailed &) {
        if (!job.output.empty()) std::remove(job.output.c_str());
        return false;
    }
}

// **Every job runs to its end, a failed one included**, so each file's diagnostic is printed; the
// verdict is given after the threads are joined, never by an exit from inside one (review P1).
bool Driver::runJobs() {
    unsigned n = threadCount();
    macros_ = macrosFor();

    if (timing_)
        std::fprintf(stderr, "%s: %zu jobs on %u thread%s\n", program_.c_str(),
                     jobs_.size(), n, n == 1 ? "" : "s");

    if (n <= 1) {
        // **The same line for one thread**, because saying so is the point: below `kThreadFrom`
        // files this compiler does the work in the thread it was started on, and a report that
        // showed threads it did not use would be worse than none. `-j n` overrides the threshold.
        bool all = true;
        for (const Job &job : jobs_) {
            if (!quiet_ && jobs_.size() > 1)
                std::fprintf(stderr, "  [thread 1] %s\n", job.input.c_str());
            if (!compileCaught(job)) all = false;
        }
        return all;
    }

    std::atomic<std::size_t> next{0};
    std::atomic<bool> ok{true};

    // **Which thread took which file, said as it happens - and when it is done with it** (1.70). A pool that
    // hands out the next index has no fixed assignment - the numbers are the threads and not the files - so this
    // is the only place the work can be seen. Until 1.70 a file's start was all that was said: cppparse's 39
    // files were all started within five seconds and the screen then stood still for ten more, until the last
    // had ended - which reads as a compiler that hangs. Now a file says when it ends, with its seconds and the
    // count so far, and while files are still compiling and nothing has been written for two seconds, a line
    // says how many are done and which are being compiled, the longest first.
    typedef SteadyClock Clock;
    const Clock::time_point began = Clock::now();
    auto secondsSince = [](Clock::time_point from) {
        return std::chrono::duration<double>(Clock::now() - from).count();
    };
    struct Working { bool busy; std::size_t job; Clock::time_point since; };
    std::vector<Working> working(n, Working{ false, 0, began });      // under diagnosticLock()
    std::size_t done = 0;                                             // under diagnosticLock()
    std::atomic<unsigned> atWork{0};       // threads that have their first file in hand, or found none left

    // **The threads are made a few at a time, and a set is at work before the next is made** (the user's,
    // 2026-11-18): a loop that made twenty threads at once had ended before the first of them had run, the files
    // were taken in whatever order the system woke the threads, and a thread was made for work its elders had
    // already done. Now kThreadSet are made, each takes its file and says so, and only then the next set - and
    // none once every file is handed out.
    // While the pool works and the screen stands still: how many are done and which are in hand, the longest first.
    Heartbeat beat(!quiet_, [this, n, &working, &done, secondsSince]() -> std::string {
        std::lock_guard<std::mutex> hold(diagnosticLock());
        std::vector<std::pair<double, std::size_t> > inHand;
        for (unsigned t = 0; t < n; t++)
            if (working[t].busy) inHand.push_back(std::make_pair(secondsSince(working[t].since), working[t].job));
        if (inHand.empty()) return std::string();
        std::sort(inHand.begin(), inHand.end(), [](const std::pair<double, std::size_t> &a,
                                                   const std::pair<double, std::size_t> &b) { return a.first > b.first; });
        std::string names;
        const std::size_t shown = inHand.size() < 3 ? inHand.size() : 3;
        for (std::size_t k = 0; k < shown; k++) {
            const std::string &in = jobs_[inHand[k].second].input;
            const std::size_t slash = in.find_last_of("/\\");
            char took[32];
            std::snprintf(took, sizeof took, " %.0f s", inHand[k].first);
            names += (k ? ", " : "") + (slash == std::string::npos ? in : in.substr(slash + 1)) + took;
        }
        if (inHand.size() > shown) names += ", and " + std::to_string(inHand.size() - shown) + " more";
        return std::to_string(done) + " of " + std::to_string(jobs_.size()) + " done, compiling " + names;
    });
    std::vector<std::thread> pool;
    pool.reserve(n);
    auto work = [this, &next, &ok, &working, &done, &atWork, secondsSince](unsigned t) {
        bool counted = false;
        for (;;) {
            std::size_t i = next.fetch_add(1);
            if (i >= jobs_.size()) break;
            {
                std::lock_guard<std::mutex> hold(diagnosticLock());
                working[t].busy = true;
                working[t].job = i;
                working[t].since = Clock::now();
                if (!quiet_) {
                    std::fprintf(stderr, "  [thread %u] %s\n", t + 1, jobs_[i].input.c_str());
                    noteSaid();
                }
            }
            // at work: its first file is in hand and said - the thread that makes the pool may make the next set
            if (!counted) { atWork.fetch_add(1); counted = true; }
            const bool compiled = compileCaught(jobs_[i]);
            if (!compiled) ok.store(false);
            {
                std::lock_guard<std::mutex> hold(diagnosticLock());
                working[t].busy = false;
                done++;
                if (!quiet_) {
                    std::fprintf(stderr, "  [thread %u] %s %s - %.1f s, %zu of %zu\n", t + 1,
                                 compiled ? "done" : "failed", jobs_[i].input.c_str(),
                                 secondsSince(working[t].since), done, jobs_.size());
                    noteSaid();
                }
            }
        }
        if (!counted) atWork.fetch_add(1);       // it found every file taken
    };
    while (pool.size() < n && next.load() < jobs_.size()) {
        const std::size_t upTo = pool.size() + kThreadSet < n ? pool.size() + kThreadSet : n;
        while (pool.size() < upTo) {
            const unsigned t = static_cast<unsigned>(pool.size());
            pool.emplace_back(work, t);
        }
        while (atWork.load() < pool.size()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (std::thread &t : pool) t.join();
    return ok.load();
}

std::vector<std::string> Driver::produced() const {
    std::vector<std::string> out;
    if (assemblyOnly_) {
        if (!toStdout_)
            for (const Job &job : jobs_) out.push_back(job.output);
    } else if (objectOnly_) {
        out = objects_;
    } else {
        out.push_back(linkTo_);
    }
    return out;
}

int Driver::run(int argc, char **argv) {
    program_ = argv[0];

    int inputs = 0;
    bool sawO = false, sawS = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-o") == 0) { sawO = true; i++; }
        else if (std::strcmp(argv[i], "-I") == 0) i++;
        else if (std::strcmp(argv[i], "-j") == 0) i++;
        else if (std::strcmp(argv[i], "-arch") == 0) i++;
        else if (std::strcmp(argv[i], "-D") == 0) i++;
        else if (std::strcmp(argv[i], "-U") == 0) i++;
        else if (std::strcmp(argv[i], "-S") == 0) sawS = true;
        else if (argv[i][0] != '-')
            inputs += std::strpbrk(argv[i], "*?") != nullptr ? 2 : 1;  // a pattern is a set
    }
    toStdout_ = (sawS && inputs == 1 && !sawO);

    if (!parseArguments(argc, argv)) return 1;

    // **Printed once the arguments are known to be good**, so a usage message
    // is not preceded by a banner nobody asked for, and before any work so it
    // is the first thing on the screen. `-nologo` is the way out.
    saysDone_ = !quiet_;
    if (!quiet_) {
        std::fprintf(stderr, "%s\n", bannerLine());
        const unsigned n = threadCount();
        if (jobs_.size() > 1)
            std::fprintf(stderr, "%zu source files, %u compilation thread%s\n",
                         jobs_.size(), n, n == 1 ? "" : "s");
    }

    std::atexit([] {
        std::vector<std::string> &names = temporaryNames();
        for (const std::string &t : names) std::remove(t.c_str());
        names.clear();
        removeFolders();
    });

    if (!runJobs()) { removeTemporaries(); return 1; }
    if (assemblyOnly_) return 0;

    bool ok = objectOnly_ ? assembleObjects() : link();
    removeTemporaries();
    return ok ? 0 : 1;
}
