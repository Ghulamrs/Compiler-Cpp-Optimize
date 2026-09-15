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

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>

#include <unistd.h>

#ifndef _WIN32
#include <cerrno>
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

}

#ifndef CXX1_INCLUDE_DIR
#define CXX1_INCLUDE_DIR ""
#endif
// **The C++ headers are a second directory, not more files in the first.**
// `include/` holds `<cstddef>` and `<vector>`; `lib/` holds the C headers
// those wrap, and a program may reach either.
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

// **Where the standard headers are, asked in the order a release wants.**
void Driver::standardIncludeDirectories(const std::string &argv0) {
    const char *envCxx = std::getenv(program::env("INCLUDE").c_str());
    const char *envC = std::getenv(program::env("LIB").c_str());
    if (envCxx != nullptr && envCxx[0] != '\0') {
        searchPath_.push_back(envCxx);
        if (envC != nullptr && envC[0] != '\0') searchPath_.push_back(envC);
        return;
    }

    // Beside the binary or one directory up. include/ is this compiler's when it
    // holds `cstddef`, a name only this library spells; the C headers <cstddef>
    // reaches are there too (an installation) or in lib/ (a checkout, or c90's).
    const std::string here = programDirectory(argv0);
    const std::string candidates[2] = { here, here + "/.." };
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
}

void Driver::usage(char *file) {
    std::fprintf(stderr,
        "usage: %s <file.cpp> [more.cpp ...] [-S|-c] [-o out] [-D n[=v]] [-U n]\n"
        "               [-I dir] [-j n] [-arch a] [-masm=m] [-O0|-O1|-O2] [-g] [-time]\n"
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
        "         x86_64-linux, x86_64-windows, arm64-darwin; the host by default,\n"
        "         and another one only reaches -S, since the assembler here is\n"
        "         this machine's\n"
        "       -masm picks the assembly syntax for x86_64-windows: 'gnu' is\n"
        "         the default and is assembled by clang, and the only one\n"
        "         that carries a line table; 'masm' is for this project's\n"
        "         assembler (masm.exe, or CXX1_AS), which needs no clang and\n"
        "         takes COMDAT; 'ml64' is the same syntax without COMDAT,\n"
        "         for ml64, which links one translation unit at a time\n"
        "       -O1 and -O2 improve the code of each function: frame slots and\n"
        "         constants forwarded, pushes paired with their pops, dead\n"
        "         instructions removed. Every spelling; -O0 is the default\n"
        "       -g writes a line table, so a debugger can stop on a line of C++\n"
        "         and step through it; x86_64-linux and arm64-darwin only\n"
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
    static bool asked = false;
    static std::string cached;
    if (!asked) { asked = true; cached = findVcvars(); }
    return cached;
#else
    return std::string();
#endif
}

// **Per-thread, because the assembler runs on a pool now.**
static thread_local std::string lastToolCommand;

// Runs one tool, inside a developer environment where the machine needs one.
// Through a batch file and not a /c string: cmd strips the outer quotes of a
// command that has them, and a file with the command on its own line has none.
#ifdef _WIN32
static std::string forCmd(const std::string &command) {
    return "\"" + command + "\"";
}
#endif

// **posix_spawn rather than system(), because system() serialises on macOS.**
static int runShell(const std::string &command) {
#ifdef _WIN32
    return std::system(forCmd(command).c_str());
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

static int runTool(const std::string &command) {
    lastToolCommand = command;
    const std::string vcvars = developerShell();
    if (vcvars.empty()) return runShell(command);

#ifdef _WIN32
    char folder[MAX_PATH];
    char script[MAX_PATH];
    if (GetTempPathA(MAX_PATH, folder) == 0) return runShell(command);
    if (GetTempFileNameA(folder, program::kName, 0, script) == 0) {
        return runShell(command);
    }
    std::string batch = script;
    std::remove(batch.c_str());
    batch += ".cmd";

    {
        std::ofstream out(batch.c_str());
        if (!out) return runShell(command);
        out << "@echo off\n";
        out << "call \"" << vcvars << "\" >nul 2>&1\n";
        out << command << "\n";
    }
    lastToolCommand = command + "   [inside " + vcvars + "]";
    const int rc = std::system(("\"" + batch + "\"").c_str());
    std::remove(batch.c_str());
    return rc;
#else
    return runShell(command);
#endif
}

static void noteWindowsToolchain() {
    if (!hostIsWindows()) return;
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
    if (!found.empty()) return found.c_str();

    const char *env = std::getenv(program::env("AS").c_str());
    if (env != nullptr && env[0] != '\0') { found = env; return found.c_str(); }

#ifdef _WIN32
    std::vector<std::string> tries;
    // Visual Studio's own, which vcvars64 names the root of.
    const char *vc = std::getenv("VCINSTALLDIR");
    if (vc != nullptr && vc[0] != '\0')
        tries.push_back(std::string(vc) + "Tools\\Llvm\\x64\\bin\\clang.exe");
    const char *pf = std::getenv("ProgramFiles");
    if (pf != nullptr && pf[0] != '\0') {
        tries.push_back(std::string(pf) +
                        "\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools"
                        "\\Llvm\\x64\\bin\\clang.exe");
        tries.push_back(std::string(pf) + "\\LLVM\\bin\\clang.exe");
    }
    for (std::size_t i = 0; i < tries.size(); i++) {
        std::ifstream probe(tries[i].c_str());
        if (probe.good()) { found = tries[i]; return found.c_str(); }
    }
#endif
    found = "clang";
    return found.c_str();
}

const char *Driver::hostLinker() {
    const char *env = std::getenv(program::env("LD").c_str());
    return (env != nullptr && env[0] != '\0') ? env : "link.exe";
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
        if (hostIsWindows() && syntax_ == Syntax::Gnu) {
            // **The GNU spelling is assembled by clang, not by ml64.** ml64
            // has no COMDAT directive, so every mergeable definition - a
            // vtable, an inline member, a template's.
            command = shellQuote(hostGnuAssembler());
            command += " -target x86_64-pc-windows-msvc -c ";
            command += shellQuote(temporaries_[i]);
            command += " -o " + shellQuote(objects_[i]);
        } else if (hostIsWindows()) {
            command = shellQuote(hostAssembler(syntax_));
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

bool Driver::link() {
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
                step = shellQuote(hostAssembler(syntax_));
                step += " /nologo /c /Fo " + shellQuote(obj) + " " + shellQuote(t);
            }
            steps.push_back(step);
            objects.push_back(obj);
            // Recorded before the run, so a failure still cleans up whatever
            // the batch managed to write.
            temporaryNames().push_back(obj);
        }
        if (!runCommands(steps)) return false;

        command = shellQuote(hostLinker());
        // **An 8 MB stack, which is what the other two targets already give.**
        command += " /nologo /subsystem:console /stack:8388608 /out:"
                 + shellQuote(linkTo_);
        for (const std::string &o : objects) command += " " + shellQuote(o);
        for (const std::string &o : alreadyObjects_) command += " " + shellQuote(o);

        command += " libcmt.lib libucrt.lib libvcruntime.lib kernel32.lib"
                   " legacy_stdio_definitions.lib";
    } else {
        command = shellQuote(hostCompiler());

        if (debug_) command += " -g";
        for (const std::string &t : temporaries_) command += " " + shellQuote(t);
        for (const std::string &o : alreadyObjects_) command += " " + shellQuote(o);
        command += " -o " + shellQuote(linkTo_);

        command += " -lm";
    }

    int rc = runTool(command);
    if (rc != 0) {
        std::fprintf(stderr, "%s: the assembler or linker failed - the command "
                             "was:\n  %s\n", program_.c_str(), command.c_str());
        noteWindowsToolchain();
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

std::string Driver::objectNameFor(const std::string &source) {
    std::size_t slash = source.find_last_of('/');
    std::string base = slash == std::string::npos ? source
                                                  : source.substr(slash + 1);
    std::size_t dot = base.rfind('.');
    return (dot == std::string::npos ? base : base.substr(0, dot)) + ".o";
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
            std::printf("%s\nVersion %s, sealed %s\n", CXX1_BANNER,
                        CXX1_VERSION, CXX1_SEAL_DATE);
            std::exit(0);
        } else if (std::strcmp(argv[i], "-nologo") == 0) {
            quiet_ = true;
        } else if (std::strcmp(argv[i], "-O0") == 0 || std::strcmp(argv[i], "-O1") == 0 ||
                   std::strcmp(argv[i], "-O2") == 0) {
            optimize_ = argv[i][2] - '0';
        } else if (std::strcmp(argv[i], "-g") == 0) {
            debug_ = true;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            std::fprintf(stderr, "%s: unknown option %s\n", argv[0], argv[i]);
            return false;
        } else {
            inputs.push_back(argv[i]);
        }
    }

    // C++ first, then C.
    standardIncludeDirectories(argv[0]);

    if (inputs.empty()) { usage(argv[0]); return false; }

    if (debug_ && !backend_->emitsLineTable(syntax_)) {
        std::fprintf(stderr,
                     "%s: -g asks where each line of C++ went, and this compiler "
                     "writes no such thing for %s in the MASM spelling: MASM "
                     "carries no line table and ml64 builds none from it, and "
                     "a native Windows debugger wants CodeView rather than "
                     "DWARF. Add -masm=gnu, which does carry one, or compile "
                     "without -g.\n",
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
                "%s: %s looks like C (.c), and cxx1 compiles C++, not C - "
                "compile it with cc1\n",
                argv[0], inputs[k].c_str());
            return false;
        }
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
        for (const std::string &in : inputs) {
            if (!output.empty()) jobs_.push_back(Job{ in, output });
            else if (toStdout_)  jobs_.push_back(Job{ in, "" });
            else                 jobs_.push_back(Job{ in, assemblyNameFor(in) });
        }
        return true;
    }

    if (backend_ != &defaultBackend()) {
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
        linkTo_ = !output.empty() ? output : (hostIsWindows() ? "a.exe" : "a.out");

    for (std::size_t i = 0; i < inputs.size(); i++) {
        std::string temp = temporaryName(static_cast<int>(i));
        temporaries_.push_back(temp);
        temporaryNames().push_back(temp);
        jobs_.push_back(Job{ inputs[i], temp });
        if (objectOnly_) objects_.push_back(output.empty()
                                            ? objectNameFor(inputs[i]) : output);
    }
    return true;
}

bool Driver::compile(const Job &job) {
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };

    const Target &target = backend_->target();
    TypeTable types;

    auto t0 = Clock::now();
    Source src = Preprocessor(job.input, searchPath_, macrosFor()).run();
    auto t1 = Clock::now();

    std::vector<Token> tokens = Lexer(src).tokenize();
    auto t2 = Clock::now();

    Parser parser(src, std::move(tokens), types, target, backend_->abi());
    Program program = parser.parse();
    auto t3 = Clock::now();

    // **A file that writes `volatile` is compiled without -O**, and says so: the
    // type system drops the qualifier, so an optimizer would take a volatile
    // read for a plain one and keep it in a register or delete it.
    const int level = program.usesVolatile ? 0 : optimize_;
    if (optimize_ > 0 && program.usesVolatile)
        std::fprintf(stderr, "%s: %s uses 'volatile', which this compiler cannot keep apart "
                     "from plain memory; compiled without -O%d\n", program_.c_str(),
                     job.input.c_str(), optimize_);

    bool ok = true;
    if (job.output.empty()) {
        std::unique_ptr<CodeGen> gen = backend_->codegen(std::cout, syntax_);
        if (debug_) gen->setLineSource(&src, workingDirectory());
        gen->setOptimize(level);
        gen->run(program);
    } else {
        std::ofstream file(job.output);
        if (!file) {
            std::fprintf(stderr, "%s: cannot write %s\n", program_.c_str(),
                         job.output.c_str());
            return false;
        }
        std::unique_ptr<CodeGen> gen = backend_->codegen(file, syntax_);
        if (debug_) gen->setLineSource(&src, workingDirectory());
        gen->setOptimize(level);
        gen->run(program);
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

    std::mutex say;
    std::string failed;
    std::atomic<std::size_t> next{0};
    std::atomic<bool> ok{true};

    // **The first failure by index, not by arrival.**
    std::size_t failedAt = commands.size();

    auto work = [&] {
        for (;;) {
            std::size_t i = next.fetch_add(1);
            if (i >= commands.size()) return;
            if (!ok.load()) return;          // somebody failed; stop starting more
            if (runTool(commands[i]) == 0) continue;
            std::lock_guard<std::mutex> hold(say);
            if (i < failedAt) { failedAt = i; failed = lastToolCommand; }
            ok.store(false);
            return;
        }
    };

    if (n <= 1) {
        work();
    } else {
        std::vector<std::thread> pool;
        pool.reserve(n);
        for (unsigned t = 0; t < n; t++) pool.emplace_back(work);
        for (std::thread &t : pool) t.join();
    }

    if (!ok.load()) {
        std::fprintf(stderr, "%s: the assembler failed - the command was:\n"
                             "  %s\n", program_.c_str(), failed.c_str());
        noteWindowsToolchain();
        return false;
    }
    return true;
}

bool Driver::runJobs() {
    unsigned n = threadCount();

    if (timing_)
        std::fprintf(stderr, "%s: %zu jobs on %u thread%s\n", program_.c_str(),
                     jobs_.size(), n, n == 1 ? "" : "s");

    if (n <= 1) {
        // **The same line for one thread**, because saying so is the point: below `kThreadFrom`
        // files this compiler does the work in the thread it was started on, and a report that
        // showed threads it did not use would be worse than none. `-j n` overrides the threshold.
        for (const Job &job : jobs_) {
            if (!quiet_ && jobs_.size() > 1)
                std::fprintf(stderr, "  [thread 1] %s\n", job.input.c_str());
            if (!compile(job)) return false;
        }
        return true;
    }

    std::atomic<std::size_t> next{0};
    std::atomic<bool> ok{true};

    // **Which thread took which file, said as it happens.** A pool that hands
    // out the next index has no fixed assignment - the numbers are the threads
    // and not the files - so this is the only place the work can be seen.
    std::vector<std::thread> pool;
    pool.reserve(n);
    for (unsigned t = 0; t < n; t++) {
        pool.emplace_back([this, t, &next, &ok] {
            for (;;) {
                std::size_t i = next.fetch_add(1);
                if (i >= jobs_.size()) return;
                if (!quiet_)
                    std::fprintf(stderr, "  [thread %u] %s\n", t + 1,
                                 jobs_[i].input.c_str());
                if (!compile(jobs_[i])) { ok.store(false); return; }
            }
        });
    }
    for (std::thread &t : pool) t.join();
    return ok.load();
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
        else if (argv[i][0] != '-') inputs++;
    }
    toStdout_ = (sawS && inputs == 1 && !sawO);

    if (!parseArguments(argc, argv)) return 1;

    // **Printed once the arguments are known to be good**, so a usage message
    // is not preceded by a banner nobody asked for, and before any work so it
    // is the first thing on the screen. `-nologo` is the way out.
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
    });

    if (!runJobs()) { removeTemporaries(); return 1; }
    if (assemblyOnly_) return 0;

    bool ok = objectOnly_ ? assembleObjects() : link();
    removeTemporaries();
    return ok ? 0 : 1;
}
