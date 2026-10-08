#pragma once

#include "backend/Backend.h"

#include <atomic>
#include <string>
#include <vector>

class Driver {
public:
    // After run() returned 0: whether to say so - a compile was begun and -nologo was not given.
    bool saysDone() const { return saysDone_; }
    // What this run made, for the line above the one that says it finished: the program, the objects or the assembly.
    std::vector<std::string> produced() const;
    // How many files were compiled without the -O asked for because they write `volatile`.
    int volatileDowngrades() const { return volatileDowngrades_.load(); }
    int run(int argc, char **argv);

private:
    struct Job {
        std::string input;
        std::string output;
    };

    std::string program_;
    std::vector<Job> jobs_;
    std::vector<std::string> searchPath_;
    // Inputs that are already objects: not compiled, handed to the linker.
    std::vector<std::string> alreadyObjects_;
    const Backend *backend_ = &defaultBackend();
    bool toStdout_ = false;
    bool timing_ = false;
    bool assemblyOnly_ = false;
    bool debug_ = false;
    int optimize_ = 0;
    bool forSize_ = false;      // -Os: -O1, the smaller code chosen where a backend offers the choice
    bool objectOnly_ = false;
    unsigned threads_ = 0;
    // **Which assembler the Windows target is written for.**
    Syntax syntax_ = Syntax::Gnu;
    bool syntaxNamed_ = false;   // -masm= was given; the clang fallback asks this
    std::string linkTo_;
    std::vector<std::string> temporaries_;
    std::vector<std::string> objects_;

    struct MacroEdit {
        std::string name;
        std::string value;
        bool undef;
    };
    std::vector<MacroEdit> macroEdits_;

    bool parseArguments(int argc, char **argv);

    bool compile(const Job &job);

    bool runJobs();
    unsigned threadCount() const;
    unsigned threadCount(std::size_t items) const;
    // Run a batch of tool invocations on that many threads, reporting the
    // first failure with the command that produced it.
    bool runCommands(const std::vector<std::string> &commands);
    bool link();
    bool assembleObjects();
    void removeTemporaries();
    std::vector<std::pair<std::string, std::string> > macrosFor() const;
    void addMacroEdit(const char *text, bool undef);

    static unsigned availableCores();

    static std::string assemblyNameFor(const std::string &source);
    std::string objectNameFor(const std::string &source) const;
    static std::string temporaryName(int index);
    static std::string temporaryName(int index, const std::string &source);
    static const char *hostCompiler();
    static const char *hostAssembler(Syntax syntax);
    // The assembler for the GNU spelling of x86_64-windows, which is a
    // different program from ml64 rather than the same one with a flag.
    static const char *hostGnuAssembler();
    static const char *hostLinker();
    std::string masmAssembler() const;
    std::string windowsLinker() const;
    // **The tms6747 target is assembled and linked on any host**: by asm6x,
    // the project's own C6000 assembler, and by TI's lnk6x where CCS is.
    bool targetIsTi() const;
    std::string tiAssembler() const;
    std::string tiLinker() const;
    bool linkTi();
    static void usage(char *);
    // The one line every run prints, and the switch that stops it - see
    // Driver.cpp, where both are explained.
    static const char *bannerLine();
    void standardIncludeDirectories(const std::string &argv0);
    bool quiet_ = false;
    bool saysDone_ = false;  // a compile was begun, and the line saying it finished is wanted
    std::atomic<int> volatileDowngrades_{0};
    // One job, its diagnostic caught: false where it failed, its half-written output removed.
    bool compileCaught(const Job &job);
    // --compress or --no_compress, handed to asm6x for tms6747; empty is asm6x's default, compressed.
    std::string asmCompress_;
    // -rts=: empty for the choice made at the link, "ti" for TI's rts6740, else RTS6x's directory or .lib.
    std::string rtsChoice_;
    // The predefined macros, made once on the main thread: __DATE__ and __TIME__ call localtime (review P4).
    std::vector<std::pair<std::string, std::string> > macros_;
};
