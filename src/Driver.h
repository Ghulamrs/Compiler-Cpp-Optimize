#pragma once

#include "backend/Backend.h"

#include <string>
#include <vector>

class Driver {
public:
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
    bool objectOnly_ = false;
    unsigned threads_ = 0;
    // **Which assembler the Windows target is written for.**
    Syntax syntax_ = Syntax::Gnu;
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
    static const char *hostCompiler();
    static const char *hostAssembler(Syntax syntax);
    // The assembler for the GNU spelling of x86_64-windows, which is a
    // different program from ml64 rather than the same one with a flag.
    static const char *hostGnuAssembler();
    static const char *hostLinker();
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
};
