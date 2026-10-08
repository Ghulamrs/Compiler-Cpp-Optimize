#pragma once

#include "Source.h"

#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Preprocessor {
public:

    explicit Preprocessor(std::string path, std::vector<std::string> searchPath = {},
                          std::vector<std::pair<std::string, std::string> > predefined = {})
        : path_(std::move(path)), searchPath_(std::move(searchPath)),
          predefined_(std::move(predefined)) {}

    Source run();

private:
    struct Macro {
        std::string body;
        bool functionLike = false;
        bool variadic = false;
        std::vector<std::string> params;
        int file = 0;
        int line = 0;
    };

    struct Cond {
        bool active;
        bool taken;
        bool seenElse;
    };

    std::string path_;
    std::vector<std::string> searchPath_;
    std::vector<std::pair<std::string, std::string> > predefined_;
    std::unordered_map<std::string, Macro> macros_;

    std::string out_;
    std::vector<std::string> files_;
    // **`#pragma once` - the include guard a file writes as a pragma.**
    std::set<std::string> pragmaOnce_;
    // `#pragma pack`: the value in force, the push stack (name, value), and every
    // change with the output line it takes effect from.
    int pack_ = 0;
    std::vector<std::pair<std::string, int> > packStack_;
    std::vector<Source::Pack> packs_;
    void pragmaPack(const std::string &args, int fileIndex, int lineNo,
                    const std::string &line, std::size_t nameStart);
    std::vector<Source::Line> lines_;

    std::vector<Cond> conds_;
    bool inBlockComment_ = false;
    int depth_ = 0;

    int physLine_ = 0;
    int lineDelta_ = 0;
    int fileOverride_ = -1;
    // `__COUNTER__`, an extension clang and cl share: 0, 1, 2 ... one per expansion.
    int counter_ = 0;
    static bool isDynamicMacro(const std::string &name);
    static bool isStandardHeader(const std::string &name);

    bool emitting() const;

    void processFile(const std::string &path, int fileIndex);
    void directive(const std::string &line, int fileIndex, int lineNo);
    void emitLine(const std::string &text, int fileIndex, int lineNo);

    std::string expandLine(const std::string &line, int fileIndex, int lineNo);
    std::string expandText(const std::string &s, std::vector<std::string> &busy,
                           int fileIndex, int lineNo, bool trackComments);
    std::vector<std::string> collectArgs(const std::string &s, std::size_t &i,
                                         const std::string &name,
                                         int fileIndex, int lineNo);
    std::string substitute(const Macro &m, const std::vector<std::string> &args,
                           std::vector<std::string> &busy, int fileIndex, int lineNo);
    static std::string stringify(const std::string &arg);
    bool hasOpenCall(const std::string &s) const;

    std::string reportLine_;

    long long evalCondition(const std::string &expr, int fileIndex, int lineNo,
                       const std::string &line);
    // The `__has_*` predicates. `__has_include` is answered by the same search
    // `#include` does; the rest answer 0, which is the answer a library's `#if`
    // is written to receive.
    static bool isHasPredicate(const std::string &name);
    std::string resolveHasChecks(const std::string &expr, int fileIndex,
                                 int lineNo, const std::string &line);
    std::string resolveDefined(const std::string &expr, int fileIndex, int lineNo,
                               const std::string &line);
    bool parentEmitting() const;

    [[noreturn]] void fail(int fileIndex, int lineNo, const std::string &line,
                           std::size_t column, const std::string &message) const;

    static std::string directoryOf(const std::string &path);

    std::string resolveInclude(const std::string &name, bool angled, int fileIndex,
                               std::vector<std::string> &tried) const;
};
