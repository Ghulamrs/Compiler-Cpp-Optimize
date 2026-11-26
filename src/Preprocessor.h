#pragma once

#include "Source.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// **Translation phases 1 to 4 over preprocessing tokens** - [lex.phases]:
// characters mapped, cut into pp-tokens (a comment one space), directives and
// macros applied; one output line per source line, read by the lexer (7).
class Preprocessor {
public:

    explicit Preprocessor(std::string path, std::vector<std::string> searchPath = {},
                          std::vector<std::pair<std::string, std::string> > predefined = {})
        : path_(std::move(path)), searchPath_(std::move(searchPath)),
          predefined_(std::move(predefined)) {}

    Source run();

private:
    // A preprocessing token - [lex.pptoken].
    struct Tok {
        enum Kind : unsigned char { End, Ident, Number, Char, String, Punct, Other, Placemarker };
        Kind kind = End;
        bool space = false;      // white space before it, a comment and a new-line included
        bool bol = false;        // the first on its line: where a directive can begin
        bool fromMacro = false;  // a replacement made it
        bool painted = false;    // [cpp.rescan]/2: a name that is never replaced again
        bool pasteOp = false;    // a `##` of a replacement list, not one an argument brought
        int hide = 0;            // the macros whose replacement made it, which it does not invoke again
        // The presumed file and line it is reported at ([cpp.line]); in a replacement, the
        // invocation's - and endLine is where the outermost invocation ends, which is __LINE__ there.
        int file = 0;
        int line = 0;
        int endLine = 0;
        // Where it was read: sources_, physical line, column - what a diagnostic shows.
        int src = -1;
        int phys = 0;
        int col = 0;
        // The white space before it on its line, written out again so an unreplaced line keeps its columns.
        const char *ws = nullptr;
        unsigned wsLen = 0;
        std::string text;

        bool is(const char *s) const { return (kind == Punct || kind == Ident) && text == s; }
    };

    struct Macro {
        std::vector<Tok> body;
        std::vector<std::string> params;
        bool functionLike = false;
        bool variadic = false;
        int id = 0;
        int file = 0;
        int line = 0;
    };

    // One file after phases 1 and 2, kept to the end: a token's white space points into it.
    struct File {
        int id = 0;                          // in sources_
        int index = 0;                       // in files_
        std::string raw;                     // as read, carriage returns gone: what a raw string holds
        std::string text;                    // trigraphs replaced, splices gone
        std::vector<std::size_t> rawAt;      // for each character of text, where it is in raw
        std::vector<std::size_t> lineAt;     // where each physical line begins in text
        std::vector<std::size_t> rawLineAt;  // and in raw
    };

    // A file being read.
    struct Reader {
        File *f = nullptr;
        std::size_t pos = 0;
        std::size_t prevEnd = 0;   // where the last token ended
        int phys = 0;
        bool bol = true;
        bool hasAhead = false;
        Tok ahead;
        int lineDelta = 0;         // `#line`: presumed minus physical
        int fileOverride = -1;
        std::size_t condsAtEntry = 0;
    };

    struct Cond {
        bool active;
        bool taken;
        bool seenElse;
        Tok at;
    };

    // Where replacement reads from: what replacements gave back first, then the file or nothing.
    struct Ctx {
        std::vector<Tok> pending;   // in reverse, the next one last
        bool fromFile = false;
        bool inIf = false;          // `defined` and the `__has_*` predicates are operators here
    };

    // [cpp.rescan]: the set of macro names a token was produced by, interned.
    struct HideSets {
        std::vector<std::vector<int> > sets{ std::vector<int>() };
        std::map<std::vector<int>, int> ids{ { std::vector<int>(), 0 } };
        std::map<std::pair<int, int>, int> unions, crossings;
        int intern(const std::vector<int> &v);
        int unite(int a, int b);
        int intersect(int a, int b);
        int with(int hs, int name);
        bool has(int hs, int name) const;
    };

    std::string path_;
    std::vector<std::string> searchPath_;
    std::vector<std::pair<std::string, std::string> > predefined_;
    std::unordered_map<std::string, std::shared_ptr<const Macro> > macros_;
    std::unordered_map<std::string, int> nameIds_;
    HideSets hide_;

    std::vector<std::unique_ptr<File> > sources_;
    std::vector<Reader> readers_;

    std::string out_;
    std::vector<std::string> files_;
    std::vector<Source::Line> lines_;
    int outFile_ = -1;
    int outLine_ = 0;
    std::string lastText_;

    // **`#pragma once` - the include guard a file writes as a pragma.**
    std::set<std::string> pragmaOnce_;
    // `#pragma pack`: the value in force, the push stack (name, value), and every
    // change with the output line it takes effect from.
    int pack_ = 0;
    std::vector<std::pair<std::string, int> > packStack_;
    std::vector<Source::Pack> packs_;
    // `_Pragma` in the middle of a line: the tokens after it on that line are under it too.
    bool pragmaInLine_ = false;
    void pragmaPack(const std::string &args, const Tok &at);
    // What `#pragma` and `_Pragma("...")` both hand over: the text after the word.
    void pragma(const std::string &spec, const Tok &at);

    std::vector<Cond> conds_;
    int counter_ = 0;  // __COUNTER__ (clang and cl): 0, 1, 2 ... per expansion

    static bool isDynamicMacro(const std::string &name);
    static bool isStandardHeader(const std::string &name);
    static bool isPredefinedName(const std::string &name);

    bool emitting() const;
    bool parentEmitting() const;

    // Phases 1 and 2, and the reading of phase 3.
    File *load(const std::string &path, int index);
    void enter(File *f);
    bool skipSpace(Reader &r, bool directive, bool *space);
    Tok lex(Reader &r, bool directive);
    Tok lexRawString(Reader &r, std::size_t start, std::size_t quote);
    void place(Reader &r, Tok &t, std::size_t start);
    std::vector<Tok> restOfLine(Reader &r);
    static std::size_t scanToken(const std::string &s, std::size_t i, Tok::Kind *kind);
    static std::vector<Tok> tokenize(const std::string &text);

    // Phase 4: directives and replacement.
    Tok fileNext();
    const Tok &filePeek();
    void finishFile();
    void directive();
    void conditional(const std::string &what, const Tok &name, Reader &r);
    void define(const std::vector<Tok> &line, const Tok &at);
    void include(const Tok &name, Reader &r);

    Tok get(Ctx &c);
    const Tok &peek(Ctx &c);
    Tok expand(Ctx &c);
    std::vector<Tok> expandAll(const std::vector<Tok> &toks, bool inIf);
    bool builtin(Ctx &c, Tok &t);
    std::vector<std::vector<Tok> > collectArgs(Ctx &c, const Macro &m, const Tok &name, Tok *close);
    std::vector<Tok> substitute(const Macro &m, const std::vector<std::vector<Tok> > &args,
                                const Tok &name, const Tok &close, int hs, bool inIf);
    static Tok stringify(const std::vector<Tok> &arg);
    Tok paste(const Tok &lhs, const Tok &rhs, const Tok &at);
    int nameId(const std::string &name);
    void checkName(const Tok &n, const char *what) const;

    long long evalCondition(const std::vector<Tok> &line, const Tok &at);
    // The `__has_*` predicates. `__has_include` is answered by the same search
    // `#include` does; the rest answer 0, which is the answer a library's `#if`
    // is written to receive.
    static bool isHasPredicate(const std::string &name);
    Tok hasCheck(Ctx &c, const Tok &t);

    void emit(const Tok &t);
    static bool needsSpace(const std::string &before, const std::string &after);
    static std::string spell(const std::vector<Tok> &toks, std::size_t from = 0);

    [[noreturn]] void fail(const Tok &at, const std::string &message) const;

    static std::string directoryOf(const std::string &path);

    std::string resolveInclude(const std::string &name, bool angled, int fileIndex,
                               std::vector<std::string> &tried) const;
};
