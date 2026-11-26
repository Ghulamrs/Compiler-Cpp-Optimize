#pragma once

#include <set>
#include <mutex>
#include <string>
#include <vector>

// A diagnostic has been written and this translation unit is abandoned; the driver catches it per job.
struct CompileFailed {};
// Every diagnostic is written whole under this lock, so two jobs' messages never interleave.
std::mutex &diagnosticLock();
[[noreturn]] void compileFailed();

// Thrown by fail() inside a trial, and caught by whoever began it.
struct SubstitutionFailure {
    std::string why;
    // **A feature this compiler has not built is not a substitution failure.**
    bool unsupported = false;
    std::size_t pos = 0;
};

class Source {
public:
    struct Line {
        int file;
        int line;
    };
    // **`#pragma pack` in force from an output line on**: 0 is none. The
    // preprocessor records each change; the class layout asks at its keyword.
    struct Pack {
        std::size_t line;
        int value;
    };

    Source(std::string name, std::string text);
    Source(std::string name, std::string text, std::vector<std::string> files,
           std::vector<Line> lines);

    const std::string &text() const { return text_; }
    const char *begin() const { return text_.c_str(); }

    struct Place {
        int file;
        int line;
        int column;
    };
    Place locate(std::size_t pos) const;
    // `file:line:col` for a note that names a second place.
    std::string where(std::size_t pos) const;

    const std::vector<std::string> &files() const { return files_; }
    void setPacks(std::vector<Pack> packs) { packs_ = std::move(packs); }
    int packAt(std::size_t pos) const;

    static Source fromFile(const std::string &path);

    [[noreturn]] void fail(std::size_t pos, const std::string &message) const;
    // A diagnostic the compile goes on after, said once and never of a system header.
    void warn(std::size_t pos, const std::string &message) const;

    // **Every error, not the first** (Driver::reportTheRest): whether fail() ended the reading under way, how many
    // errors have been said, and the names a statement taken out would have declared - what the next reading says
    // of one of those is that statement's doing, and is not said.
    bool failed() const { return failed_; }
    void clearFailed() const { failed_ = false; }
    int errorsReported() const { return errorsReported_; }
    void quieten(const std::string &name) const { quiet_.insert(name); }

    // The instantiations under way, said under an error met inside them, innermost first. A function template's body
    // is read after the body that asked for it is done, so a specialization keeps the chain it was asked from.
    struct Context { std::size_t pos; std::string what; };
    void enterInstantiation(std::size_t pos, const std::string &what) const { chain_.push_back(Context{ pos, what }); }
    void leaveInstantiation() const { chain_.pop_back(); }
    const std::vector<Context> &instantiations() const { return chain_; }
    void setInstantiations(std::vector<Context> chain) const { chain_ = std::move(chain); }

    void setSystemDirectories(std::vector<std::string> dirs) { systemDirs_ = std::move(dirs); }

// **A trial: a stretch of parsing whose failure is an answer.** [temp.deduct]/8
// drops the specialization and issues no diagnostic. The one place here where a
// failure must not stop the compiler; everywhere else an error stops it.
    void beginTrial() const { trials_++; }
    void endTrial() const { trials_--; }
    bool inTrial() const { return trials_ > 0; }

private:
    // One diagnostic as it is printed: where, what, the line and the caret.
    std::string described(std::size_t pos, const char *kind, const std::string &message) const;

    mutable int trials_ = 0;
    mutable bool failed_ = false;
    mutable int errorsReported_ = 0;
    mutable std::set<std::string> quiet_;
    mutable std::set<std::string> said_;
    mutable std::vector<Context> chain_;
    std::vector<std::string> systemDirs_;
    std::string name_;
    std::string text_;
    std::vector<std::string> files_;
    std::vector<Line> lines_;
    std::vector<Pack> packs_;

    mutable std::vector<std::size_t> lineStarts_;
    void indexLines() const;

    void lineAt(std::size_t pos, int *line, std::size_t *start) const;
};
