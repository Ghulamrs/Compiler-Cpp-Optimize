// compilerpp_amalgamated.cpp
//
// The whole of Compiler++ in one translation unit, generated from the files in
// Compiler++/ by dist/amalgamate.py.  It exists so the compiler can be built on
// a machine that has nothing but a C++ compiler -- no project file, no CMake,
// no copying twenty files across.
//
//   Windows (MSVC):   cl /W4 /EHsc /Fe:compilerpp.exe compilerpp_amalgamated.cpp
//   Windows (MinGW):  g++ -std=c++98 -Wall -Wextra -o compilerpp.exe compilerpp_amalgamated.cpp
//   Linux / macOS:    g++ -std=c++98 -Wall -Wextra -o compilerpp compilerpp_amalgamated.cpp
//
//   Run:              compilerpp -ast -layout path\\to\\input.cpp
//
// Define COMPILERPP_NO_MAIN to leave main() out, which is what an application
// embedding the compiler wants: it has a main() already, and a second one does
// not link.  Everything else -- the parser, the analyser, the lowering and the
// VM -- is unchanged and is the whole of what an embedder calls.
//
// DO NOT EDIT.  Edit the files in Compiler++/ and regenerate.
// C++98 only.

// ======================================================================
// HEADERS
// ======================================================================

// ---------- Token.h ----------
// Token.h -- the token set, shared by both layers.
//
// C++98 only.  No feature from C++11 or later is used anywhere in this project.

#ifndef TOKEN_H
#define TOKEN_H

#include <string>

enum TokenKind {
    TOK_EOF,
    TOK_IDENTIFIER,
    TOK_NUMBER,         // integer literal
    TOK_FLOATLIT,       // 1.5, 1e3, 1.5f
    TOK_CHARLIT,        // 'A'
    TOK_STRINGLIT,      // "text"

    // --- keywords the C layer needs ---
    TOK_INT,
    TOK_CHAR,
    TOK_VOID,
    TOK_BOOL,
    TOK_SHORT,
    TOK_LONG,
    TOK_SIGNED,
    TOK_UNSIGNED,
    TOK_FLOAT,
    TOK_DOUBLE,
    TOK_CONST,
    TOK_DO,
    TOK_SWITCH,
    TOK_CASE,
    TOK_DEFAULT,
    TOK_RETURN,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_BREAK,
    TOK_CONTINUE,

    // --- keywords the C++ layer adds ---
    TOK_CLASS,
    TOK_STRUCT,
    TOK_PUBLIC,
    TOK_PRIVATE,
    TOK_PROTECTED,
    TOK_VIRTUAL,
    TOK_NEW,
    TOK_DELETE,
    TOK_THIS,
    TOK_TRUE,
    TOK_FALSE,

    // --- punctuation ---
    TOK_SEMI,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_COLON,      // :
    TOK_COLONCOLON, // ::
    TOK_DOT,        // .
    TOK_ARROW,      // ->
    TOK_TILDE,      // ~   (destructor names)
    // Recognised only so the parser can name the feature they belong to.
    TOK_ELLIPSIS,   // ...  variadic parameters
    TOK_HASH,       // #    a preprocessor directive

    // --- operators ---
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_ASSIGN,     // =
    TOK_PLUSPLUS,   // ++
    TOK_MINUSMINUS, // --
    TOK_PLUSEQ,     // +=
    TOK_MINUSEQ,    // -=
    TOK_STAREQ,     // *=
    TOK_SLASHEQ,    // /=
    TOK_PERCENTEQ,  // %=
    TOK_EQ,         // ==
    TOK_NE,         // !=
    TOK_LT,         // <
    TOK_GT,         // >
    TOK_LE,         // <=
    TOK_GE,         // >=
    TOK_ANDAND,     // &&
    TOK_OROR,       // ||
    TOK_NOT,        // !
    TOK_AMP,        // &
    TOK_SHL,        // <<
    TOK_SHR,        // >>

    // Operators of real C++ that this subset leaves out.  They are lexed for
    // the same reason TOK_RESERVED exists: without a token, `a ? 1 : 2` and
    // `a | b` reached the parser as an unknown character and were reported as
    // punctuation trouble, which reads as a broken compiler rather than as a
    // language that is smaller than expected.
    TOK_QUESTION,   // ?   the conditional operator
    TOK_PIPE,       // |   bitwise or
    TOK_CARET,      // ^   bitwise xor

    // A keyword of real C++ that this subset leaves out.  It is lexed rather
    // than left as an identifier so the parser can say WHICH feature is
    // missing, once, instead of failing its way through the construct.  The
    // set spans both layers -- goto and sizeof are C's, template and throw are
    // C++'s -- so it belongs to the shared lexer, not to either grammar.
    TOK_RESERVED,

    TOK_UNKNOWN
};

// The position is copied onto AST nodes as they are built, so the semantic
// pass can point at source just as a syntax error does.
struct Token {
    TokenKind kind;
    std::string text;       // identifier spelling, or a string literal's body
    long numberValue;       // integer and character literals
    double floatValue;      // floating literals
    bool isFloatSuffixed;   // 1.5f rather than 1.5
    int line;
    int col;
    Token()
        : kind(TOK_UNKNOWN), numberValue(0), floatValue(0.0),
          isFloatSuffixed(false), line(0), col(0) {}
};

// Human-readable spelling, for "expected X, found Y" messages.
const char *tokenName(TokenKind k);

// Why a reserved word is not available, and what to do instead.  Returns 0
// when the word is not one of them.
const char *reservedWordHelp(const std::string &word);

#endif

// ---------- Lexer.h ----------
// Lexer.h -- one lexer for both layers, so it stays at global scope.
//
// C++98 only.

#ifndef LEXER_H
#define LEXER_H

#include <cctype>
#include <cstddef>
#include <string>

class Lexer {
public:
    Lexer(const std::string &s) : src(s), pos(0), line(1), col(1) {}
    Token nextToken();

    // Rewind, for the parser's speculation.  Line and column travel with the
    // offset, or later tokens would report the wrong place.
    struct Position {
        std::size_t offset;
        int line;
        int col;
    };
    Position tell() const;
    void seek(const Position &p);

private:
    std::string src;
    std::size_t pos;
    int line;
    int col;

    char peek() const { return pos < src.size() ? src[pos] : '\0'; }
    char peekAt(std::size_t ahead) const {
        return pos + ahead < src.size() ? src[pos + ahead] : '\0';
    }
    // The only place the position advances, so line and column cannot drift.
    char get();
    void skipWhitespaceAndComments();

    Token makeToken(TokenKind k, int startLine, int startCol);
};

Lexer *createLexer(const std::string &s);

// Object-like macros, expanded before anything is lexed.
//
//     #define PI 3.14159
//
// is a textual substitution and nothing more -- which is all a constant needs,
// and is why it can live here rather than in a pass of its own.  A directive
// line is blanked rather than removed, so every later line keeps its number.
// Function-like macros and conditional compilation are refused by name.
class Diagnostics;
std::string expandDefines(const std::string &src, Diagnostics &diag);

// <iostream>, written in this language and prepended when a program includes
// it.  There is no library to link, so the header IS its implementation --
// which is also a fair test of whether the language can express one.
//
// Returns the prelude text, or an empty string when the source includes no
// header that needs it.  `lines` receives how many lines it occupies, so
// diagnostics can subtract them and still point at the user's own line.
std::string preludeFor(const std::string &src, int &lines);

#endif

// ---------- Diagnostics.h ----------
// Diagnostics.h -- one place for every complaint the compiler makes.
//
// Reporting is separate from deciding what to do next: the parser reports and
// then recovers, the semantic pass reports and carries on, and main() asks at
// the end whether anything went wrong.  The file:line:col format is the one
// Xcode parses into clickable issues.
//
// C++98 only.

#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <string>

class Diagnostics {
public:
    explicit Diagnostics(const std::string &sourceName);

    void error(int line, int col, const std::string &msg);
    void warning(int line, int col, const std::string &msg);

    int errorCount() const { return errors; }
    int warningCount() const { return warnings; }
    bool hadError() const { return errors > 0; }

    // Printed once at the end of a run.
    void printSummary() const;

    // Past this many errors the rest are almost always cascade, and a wall of
    // them is worse than silence.  Counting continues; printing stops.
    static const int MaxReported = 20;

    // Lines of prelude prepended before the user's own first line.  Subtracted
    // from every report, so a program that includes <iostream> still sees its
    // own line numbers.
    void setLineOffset(int n) { lineOffset = n; }

private:
    std::string name;
    int errors;
    int warnings;
    bool capped;            // errors have stopped being reported
    bool warningsCapped;    // and warnings, separately -- one cannot end the other
    int lineOffset;

    void report(const char *level, int line, int col, const std::string &msg);

    // not copyable (C++98 way: declared private, never defined)
    Diagnostics(const Diagnostics &);
    Diagnostics &operator=(const Diagnostics &);
};

#endif

// ---------- Bytecode.h ----------
// Bytecode.h -- PASS 6a, the instruction set the compiler targets.
//
// A stack machine, chosen so the compiler can RUN what it produces on the
// machine that built it.  Emitting native code for two ABIs would be more
// authentic and less useful: nothing about a calling convention teaches
// anything about C++, and cross-compiled output cannot be watched.
//
// One flat byte memory holds everything, so an address is an address whatever
// it points at -- a global, a string, a local, or the heap:
//
//     [0 .. 8)          reserved, so the null pointer is address 0
//     [static data]     globals, vtables, string literals
//     [frame stack]     one frame per active call
//     [heap]            new and delete
//
// Values on the operand stack are 8 bytes and hold either a long or a double;
// memory keeps each type at its declared width, so LOAD and STORE carry a size.
//
// C++98 only.

#ifndef BYTECODE_H
#define BYTECODE_H

#include <string>
#include <vector>

// The VM's machine word.
//
// This compiler's type model fixes `long` at 8 bytes -- see the note on the
// builtin table in AST.h -- and the VM's memory, addresses and operand stack
// are all defined in those terms.  The HOST's `long` is not the same thing:
// it is 8 bytes on macOS and Linux (LP64) but 4 on Windows (LLP64), and using
// it here silently gave the VM a 32-bit word on MSVC.  Every value wider than
// 32 bits was then truncated -- doubles through a register lost half their
// bit pattern, and `(1UL << 32) - 1` became 0, so every 4-byte integer resize
// masked its value away to nothing.
//
// C++98 has no <cstdint>, so the width is pinned per compiler.  Both spellings
// are pre-C++11 extensions their compilers have always accepted.
#if defined(_MSC_VER)
typedef __int64          vmword;
typedef unsigned __int64 uvmword;
#else
typedef long long          vmword;
typedef unsigned long long uvmword;
#endif

// How much memory the machine has.  It lives here, with the word width, rather
// than in VM.cpp, because it is not only the VM's business: an object too big
// to fit is a thing the FRONT END should refuse, at the declaration, with a
// line number -- and it cannot refuse what it cannot see.  Two constants that
// had to agree would be one more thing to drift.
const vmword MachineMemory   = 4L * 1024 * 1024;
const vmword MachineStack    = 1L * 1024 * 1024;
const vmword MachineMaxSteps = 50L * 1000 * 1000;

// The three numbers above, as a thing that can be handed to a machine rather
// than compiled into it.  A command line wants the defaults and never thinks
// about them; an application embedding this compiler has different problems.
//
// `memory` is claimed whole on every run and is most of what the process is
// holding while a program runs, so a host that runs small programs wants it
// small.  `maxSteps` is what stops `while(1){}` -- and the default is tighter
// than it looks: a plain million-iteration loop spends 49 million steps, so a
// host that wants real loops to finish must raise it and offer a Stop instead.
// `callStack` is the room for frames, taken out of `memory` before the heap
// starts.
//
// They are one struct because they are one decision: memory has to exceed the
// call stack plus whatever the program's statics need, and a host setting one
// without looking at the others gets a machine that cannot start.
struct MachineLimits {
    vmword memory;
    vmword callStack;
    vmword maxSteps;
    MachineLimits()
        : memory(MachineMemory), callStack(MachineStack), maxSteps(MachineMaxSteps) {}
};

enum OpCode {
    // --- operand stack ---
    OP_PushConst,       // push imm
    OP_PushFConst,      // push fimm
    OP_LoadReg,         // push the value of frame register imm
    OP_StoreReg,        // pop into frame register imm
    OP_Pop,

    // --- addresses ---
    OP_LocalAddr,       // push the address of local slot imm
    OP_StaticAddr,      // push imm, an absolute address in static data
    OP_FieldAddr,       // pop a, push a + imm
    OP_FuncAddr,        // push imm, a function index

    // --- memory (imm = width in bytes; `b` = 1 when sign-extending) ---
    OP_Load,
    OP_Store,
    OP_MemCopy,     // imm = byte count; pops src then dst

    // --- integer arithmetic ---
    OP_Add, OP_Sub, OP_Mul, OP_Div, OP_Mod, OP_UDiv, OP_UMod,
    OP_Shl, OP_Shr, OP_UShr,
    OP_Neg, OP_Not,

    // --- floating arithmetic ---
    OP_FAdd, OP_FSub, OP_FMul, OP_FDiv, OP_FNeg,

    // --- comparison ---
    OP_CmpEQ, OP_CmpNE, OP_CmpLT, OP_CmpGT, OP_CmpLE, OP_CmpGE,
    OP_UCmpLT, OP_UCmpGT, OP_UCmpLE, OP_UCmpGE,
    OP_FCmpEQ, OP_FCmpNE, OP_FCmpLT, OP_FCmpGT, OP_FCmpLE, OP_FCmpGE,

    // --- conversions ---
    OP_IntToFloat,      // imm = 1 when the source is unsigned
    OP_FloatToInt,
    OP_FloatResize,     // imm = target width
    OP_IntResize,       // imm = target width, b = 1 when sign-extending

    // --- control ---
    OP_Jump,            // goto imm
    OP_BranchZero,      // pop; goto imm if zero
    OP_BranchNZ,
    OP_Call,            // imm = function index, b = argument count
    OP_CallIndirect,    // pop a function index, b = argument count
    OP_VTableLoad,      // pop an object address, push its vtable's slot imm
    OP_Native,          // imm = native index, b = argument count
    OP_Return,          // pop a value and return it
    OP_ReturnVoid,

    // --- free store ---
    OP_Alloc,           // push the address of imm fresh bytes; b = 1 for new[]
    OP_Free,            // pop an address and release it; b = 1 for delete[]

    OP_Halt,

    // Appended here rather than beside OP_Alloc so that no existing opcode's
    // value moves: a .cxb written by an earlier build still means what it
    // meant, and the malformed images in tests/images stay the exact bytes
    // they were recorded as.
    OP_AllocN,          // pop a size (and, when b = 1, an element count first),
                        // push the address
    OP_ArrayCount,      // pop an address from new[], push its element count

    // Not an instruction: the count, so a byte read out of a file can be
    // checked against the set of opcodes that actually exist.
    OP_Count
};

const char *opCodeName(OpCode op);

struct Instr {
    OpCode op;
    vmword imm;
    vmword b;
    double fimm;
    int line;
    Instr(OpCode o = OP_Halt) : op(o), imm(0), b(0), fimm(0.0), line(0) {}
};

// One frame's shape.  Locals keep their declared widths; every virtual
// register is 8 bytes, because a register holds a long or a double and nothing
// smaller is worth the arithmetic.
struct FuncImage {
    std::string name;
    int paramCount;
    int frameSize;                  // bytes of locals
    int registerCount;
    std::vector<int> localOffset;   // slot -> byte offset within the frame
    std::vector<int> localSize;
    // Parallel to localSize: 1 when the slot holds a float or double, so the
    // VM writes an incoming argument with the right representation.
    std::vector<unsigned char> localFloat;
    // 1 when the slot is a by-value object: the argument is an address and the
    // VM copies localSize bytes from it.
    std::vector<unsigned char> localObject;
    std::vector<Instr> code;
    FuncImage() : paramCount(0), frameSize(0), registerCount(0) {}
};

// Everything the VM needs to run: the code, and the bytes that exist before it
// starts.
struct Image {
    std::vector<FuncImage> functions;
    std::vector<unsigned char> staticData;
    int entry;                      // index of main, or -1
    // Run after the entry function returns: global objects are destroyed
    // there, because there is no scope in the program that owns them.
    int fini;
    Image() : entry(-1), fini(-1) {}

    void disassemble() const;

    // --- the object file -------------------------------------------------
    // A compiled program has to outlive the process that made it, so the
    // image is written whole to a .cxb file: a magic word, a version, the
    // static data, then every function.  Little-endian and fixed-width
    // throughout, so a file written on one machine loads on another.
    bool write(const std::string &path, std::string &error) const;
    bool read(const std::string &path, std::string &error);

    static const unsigned long Magic   = 0x31425843UL;  // "CXB1"
    // v2 localFloat, v3 localObject, v4 fini, v5 the maths natives grew.
    // A native is called by its INDEX, so inserting one renumbers every native
    // after it: a v4 image would still load and would then call the wrong
    // function.  The bump makes it say so instead.
    static const unsigned long Version = 5;
};

// Functions the VM supplies.  A program gets them by DECLARING one without a
// body -- there is no header file to include, so the declaration is the
// binding.  Without these a compiled program could compute but never show
// anything, which would make running it pointless.
enum NativeId {
    NAT_PrintInt,
    NAT_PrintChar,
    NAT_PrintDouble,
    NAT_PrintString,
    NAT_PrintLine,
    // The error stream, so `cerr` is actually diagnosable output and survives
    // redirecting stdout.
    NAT_ErrInt, NAT_ErrChar, NAT_ErrDouble, NAT_ErrString, NAT_ErrLine,
    // Maths.  There is no <cmath> to include, so these are declared the same
    // way everything else is: `double sqrt(double);` with no body.
    NAT_Sqrt, NAT_Sin, NAT_Cos, NAT_Tan,
    NAT_Asin, NAT_Acos, NAT_Atan, NAT_Atan2,
    NAT_Sinh, NAT_Cosh, NAT_Tanh,
    NAT_Pow, NAT_Fabs, NAT_Floor, NAT_Ceil,
    // `%` needs integer operands and says so, which left no way at all to take
    // a remainder of two doubles.  fmod is that operation.
    NAT_Fmod,
    // Rounding to a whole number in the two directions floor and ceil cannot
    // express: toward zero, and to the nearest.
    NAT_Trunc, NAT_Round,
    NAT_Log, NAT_Log10, NAT_Exp,
    NAT_Abs,                    // the one that is integer in and integer out
    // Input.  A read that fails leaves the destination alone and turns
    // NAT_InputGood false, which is what `cin.good()` reports -- there are no
    // exceptions to throw and no stream state object to carry one.
    NAT_PrintPointer, NAT_ErrPointer,   // an address, which no other native takes
    NAT_ReadInt, NAT_ReadDouble, NAT_ReadChar,
    NAT_ReadString,             // one whitespace-delimited word
    NAT_ReadLine,               // the rest of the line
    NAT_InputGood,
    NAT_Count
};

// Returns NAT_Count when the name is not a native.
NativeId nativeByName(const std::string &name);
const char *nativeName(NativeId id);
int nativeArgCount(NativeId id);
// True when the native hands back a double rather than an integer.
bool nativeReturnsFloat(NativeId id);
// The most any native takes; the VM keeps that many argument slots.
const int NativeMaxArgs = 2;

#endif

// ---------- AST.h ----------
// AST.h -- LAYER 1, the C layer, namespace `cc`.
//
// ============================ LAYERING MODEL ============================
//
//   Two layers, and the second inherits the first:   cxx::X : public cc::X
//
//     cc::   AST.h  / AST.cpp  / Parser.h  / Parser.cpp    -- what C has
//     cxx::  AST1.h / AST1.cpp / Parser1.h / Parser1.cpp   -- what C++ adds
//
//   Layer 2 never duplicates layer 1.  The test for where a node belongs is
//   simply: does C already have this?
//
//   cc::ASTNode                                   (the single root)
//     |-- cc::Type
//     |     |-- cc::BuiltinType | cc::PointerType        int, T*
//     |     '-- cxx::ReferenceType | cxx::ClassType      T&, Point
//     |-- cc::Expr
//     |     |-- cc::NumberExpr | cc::IdentExpr
//     |     |-- cc::UnaryExpr | cc::BinaryExpr | cc::CallExpr
//     |     '-- cxx::MemberAccessExpr | cxx::ThisExpr
//     |         cxx::NewExpr | cxx::DeleteExpr
//     |-- cc::Stmt
//     |     |-- cc::CompoundStmt      { ... }, also the unit of scope
//     |     |-- cc::DeclStmt | cc::ExprStmt | cc::ReturnStmt
//     |     |-- cc::IfStmt | cc::WhileStmt | cc::ForStmt
//     |     '-- cc::BreakStmt | cc::ContinueStmt
//     '-- cc::Decl
//           |-- cc::VarDecl | cc::Function
//           |-- cxx::FieldDecl  : cc::Decl        adds access
//           |-- cxx::MethodDecl : cc::Function    adds access, virtual, ctor
//           '-- cxx::ClassDecl  : cc::Decl
//
//   Parser: cc::Parser holds the grammar; cxx::Parser derives from it and
//   answers the virtual hooks -- parseDeclaration, parseStatement, parseType,
//   parsePrimary, parseMemberSuffix, parseFunctionTail, parseVarInitializer.
//   The hooks run both ways: parsePostfix() asks parseMemberSuffix(), so
//   p.getX().y parses in one loop; parseStatement() asks parseType(), so a C
//   rule declares C++ types (Point p;) with no C++ statement code.
//
//   The result is ONE tree mixing both namespaces, which is why the semantic
//   pass is not split in two -- it walks the tree and uses dynamic_cast.
//
//   Passes: Parser -> Semantic (+ SymbolTable) -> Layout -> Lower (+ IR).
//
//   C++98 only, everywhere.  No `override` keyword exists, so a derived layer
//   re-declares a virtual with an exactly matching signature or silently hides.
//
// ========================================================================

#ifndef AST_H
#define AST_H

#include <cstddef>
#include <string>
#include <vector>
#include <iostream>

namespace cc {

struct ASTNode {
    int line;                   // copied off the token that started this node
    int col;

    ASTNode() : line(0), col(0) {}
    virtual ~ASTNode() {}
    virtual void print(int indent = 0) = 0;

protected:
    void printIndent(int n) {
        for (int i = 0; i < n; ++i) std::cout << "  ";
    }
};

// --- Types ------------------------------------------------------------

struct Type : public ASTNode {
    // `const` on a variable: it may not be assigned to, incremented, or
    // decremented after it is initialised.  It rides on the type because that
    // is where the parser puts it and where every check can reach it.
    bool isConst;
    Type() : isConst(false) {}
    virtual ~Type() {}
};

// The builtin types, as a kind rather than a name.  Every conversion rule in
// the language is stated in terms of three facts about a type -- is it integer
// or floating, is it signed, and what is its rank -- and a string carries none
// of them.  Sizes are this compiler's model, not the host's: `long` is 8 bytes
// here, as on Linux and macOS, even when targeting Windows.
//
// There is no `bool` here.  C89 has none, so it belongs to the C++ layer --
// see cxx::BoolType in AST1.h.  Nothing in this table needs an entry for it,
// because bool promotes to int the moment it enters arithmetic and never
// survives as the type of a computation.
enum BuiltinKind {
    BK_Void,
    BK_Char, BK_SChar, BK_UChar,
    BK_Short, BK_UShort,
    BK_Int, BK_UInt,
    BK_Long, BK_ULong,
    BK_Float, BK_Double
};

const char *builtinName(BuiltinKind k);
int  builtinSize(BuiltinKind k);
// Conversion rank: char < short < int < long < float < double.  Two types of
// equal rank differ only in signedness.
int  builtinRank(BuiltinKind k);
bool builtinIsInteger(BuiltinKind k);
bool builtinIsFloating(BuiltinKind k);
bool builtinIsSigned(BuiltinKind k);
bool builtinIsArithmetic(BuiltinKind k);   // anything but void

struct BuiltinType : public Type {
    BuiltinKind kind;
    BuiltinType(BuiltinKind k) : kind(k) {}
    const char *name() const { return builtinName(kind); }
    void print(int indent);
};

// T[n].  An array is NOT a pointer: it owns its elements and knows how many
// there are.  It only becomes a pointer when used in an expression, which the
// semantic pass calls decay.
struct ArrayType : public Type {
    Type *element;
    long count;
    ArrayType(Type *e, long n) : element(e), count(n) {}
    ~ArrayType() { delete element; }
    void print(int indent);
};

// Pointer type T*
struct PointerType : public Type {
    Type *base;
    PointerType(Type *b) : base(b) {}
    ~PointerType() { delete base; }
    void print(int indent);
};

// --- Expressions ------------------------------------------------------

// Enums rather than characters: == and && do not fit in a char.
enum BinaryOp {
    BIN_Add, BIN_Sub, BIN_Mul, BIN_Div, BIN_Mod,
    BIN_Assign,
    // a += b evaluates a ONCE, so it is its own operator rather than sugar for
    // a = a + b, which would evaluate a twice.
    BIN_AddAssign, BIN_SubAssign, BIN_MulAssign, BIN_DivAssign, BIN_ModAssign,
    BIN_EQ, BIN_NE, BIN_LT, BIN_GT, BIN_LE, BIN_GE,
    BIN_LAnd, BIN_LOr,
    BIN_Shl, BIN_Shr
};
const char *binaryOpText(BinaryOp op);
bool binaryOpIsComparison(BinaryOp op);
bool binaryOpIsLogical(BinaryOp op);
// True for = and for every compound form.
bool binaryOpIsAssignment(BinaryOp op);
// The arithmetic behind a compound assignment: += yields +.
BinaryOp binaryOpUnderlying(BinaryOp op);

enum UnaryOp {
    UN_Neg, UN_Not, UN_Deref, UN_AddrOf,
    // Prefix yields the new value, postfix the old one -- the only difference
    // between them, and the reason they are four operators and not two.
    UN_PreInc, UN_PreDec, UN_PostInc, UN_PostDec
};
bool unaryOpIsIncDec(UnaryOp op);
const char *unaryOpText(UnaryOp op);

struct Type;

// Every expression carries the type the semantic pass computed for it.
// Lowering used to work this out again from scratch, with a weaker algorithm,
// and the two disagreed -- which is how a float multiply came to be emitted as
// an integer one.  Semantic decides; lowering reads.  Owned by the analyzer.
struct Expr : public ASTNode {
    Type *resolvedType;
    Expr() : resolvedType(0) {}
};

// An integer or character literal.  Both are integer values; only their type
// differs, which is why one node carries them.
struct NumberExpr : public Expr {
    long value;
    BuiltinKind kind;
    NumberExpr(long v, BuiltinKind k = BK_Int) : value(v), kind(k) {}
    void print(int indent);
};

struct FloatExpr : public Expr {
    double value;
    BuiltinKind kind;           // BK_Float or BK_Double
    FloatExpr(double v, BuiltinKind k) : value(v), kind(k) {}
    void print(int indent);
};

// "text" -- lowered to a pointer into the module's string data.
struct StringExpr : public Expr {
    std::string value;
    StringExpr(const std::string &v) : value(v) {}
    void print(int indent);
};

struct IdentExpr : public Expr {
    std::string name;
    IdentExpr(const std::string &n) : name(n) {}
    void print(int indent);
};

struct Function;                    // an overloaded operator resolves to one

struct UnaryExpr : public Expr {
    UnaryOp op;
    Expr *operand;
    Function *resolvedOperator;         // an overloaded unary '-'; not owned
    UnaryExpr(UnaryOp o, Expr *e) : op(o), operand(e), resolvedOperator(0) {}
    ~UnaryExpr() { delete operand; }
    void print(int indent);
};

// a[i].  Kept as a node rather than desugared to *(a+i) in the parser,
// because a class may overload it and the parser does not know types.  For a
// pointer or an array it lowers to exactly that sum, so nothing else changes.
struct IndexExpr : public Expr {
    Expr *base;
    Expr *index;
    Function *resolvedOperator;     // a class's operator[]; 0 otherwise
    IndexExpr(Expr *b, Expr *i) : base(b), index(i), resolvedOperator(0) {}
    ~IndexExpr();
    void print(int indent);
};

struct BinaryExpr : public Expr {
    BinaryOp op;
    Expr *lhs;
    Expr *rhs;
    // An overloaded operator, chosen by the semantic pass exactly as a call's
    // overload is.  When set, this expression IS a call; not owned.
    Function *resolvedOperator;
    BinaryExpr(BinaryOp o, Expr *l, Expr *r)
        : op(o), lhs(l), rhs(r), resolvedOperator(0) {}
    ~BinaryExpr();
    void print(int indent);
};

// (T)expr
struct CastExpr : public Expr {
    Type *type;
    Expr *expr;
    CastExpr(Type *t, Expr *e) : type(t), expr(e) {}
    ~CastExpr();
    void print(int indent);
};


struct CallExpr : public Expr {
    Expr *callee;
    std::vector<Expr*> args;
    // Which function this call resolved to, chosen by the semantic pass once
    // overloading made the name alone insufficient.  Lowering uses it rather
    // than resolving again; not owned.
    Function *resolved;
    CallExpr(Expr *c) : callee(c), resolved(0) {}
    ~CallExpr();
    void print(int indent);
};

// --- Declarations -----------------------------------------------------

struct Decl : public ASTNode {
    virtual ~Decl() {}
};

// One node for a variable wherever it appears: at file scope, or inside a
// block wrapped in a DeclStmt.
struct VarDecl : public Decl {
    Type *type;
    std::string name;
    Expr *init;                 // for  = expr ; 0 otherwise
    // Direct initialisation, Point q(1, 2) -- an alternative to init, never both.
    std::vector<Expr*> ctorArgs;
    bool hasCtorArgs;           // true even for  Point q();
    // Which constructor those arguments selected.  Chosen by the semantic
    // pass, exactly as a call's overload is; not owned.
    Function *resolvedCtor;
    VarDecl(Type *t, const std::string &n, Expr *i)
        : type(t), name(n), init(i), hasCtorArgs(false), resolvedCtor(0) {}
    ~VarDecl();
    void print(int indent);
};

struct Stmt;
struct CompoundStmt;

struct Function : public Decl {
    Type *retType;
    std::string name;
    std::vector<VarDecl*> params;
    CompoundStmt *body;         // 0 when this is only a declaration
    Function(Type *r, const std::string &n) : retType(r), name(n), body(0) {}
    ~Function();
    void print(int indent);
    virtual void printSignature(int indent);
    virtual void printBodyPrefix(int indent);   // a ctor's initialiser list
};

// --- Statements -------------------------------------------------------
struct Stmt : public ASTNode {};

// Also the unit of scope, which is why it is a node.
struct CompoundStmt : public Stmt {
    std::vector<Stmt*> body;
    // Class-typed locals in REVERSE declaration order: the destructors the
    // lowering phase runs on every path out of this block.  Filled in by the
    // semantic pass; aliases into the block's own DeclStmts, not owned.
    std::vector<VarDecl*> destroyAtExit;
    ~CompoundStmt();
    void print(int indent);
};

struct DeclStmt : public Stmt {
    VarDecl *var;
    DeclStmt(VarDecl *v) : var(v) {}
    ~DeclStmt() { delete var; }
    void print(int indent);
};

struct ExprStmt : public Stmt {
    Expr *expr;
    ExprStmt(Expr *e) : expr(e) {}
    ~ExprStmt() { delete expr; }
    void print(int indent);
};

struct ReturnStmt : public Stmt {
    Expr *expr;                 // 0 for a bare  return;
    ReturnStmt(Expr *e) : expr(e) {}
    ~ReturnStmt() { delete expr; }
    void print(int indent);
};

struct IfStmt : public Stmt {
    Expr *cond;
    Stmt *thenBranch;
    Stmt *elseBranch;           // may be 0
    IfStmt(Expr *c, Stmt *t, Stmt *e) : cond(c), thenBranch(t), elseBranch(e) {}
    ~IfStmt();
    void print(int indent);
};

struct WhileStmt : public Stmt {
    Expr *cond;
    Stmt *body;
    WhileStmt(Expr *c, Stmt *b) : cond(c), body(b) {}
    ~WhileStmt();
    void print(int indent);
};

// do body while (cond);  -- the body runs before the condition is first tested.
struct DoWhileStmt : public Stmt {
    Stmt *body;
    Expr *cond;
    DoWhileStmt(Stmt *b, Expr *c) : body(b), cond(c) {}
    ~DoWhileStmt();
    void print(int indent);
};

// A case label is a LABEL, not a block: control enters at the matching one and
// runs on through the rest until a break.  Modelling it as a statement inside
// the switch body is what makes fall-through work by construction.
struct CaseStmt : public Stmt {
    long value;
    bool isDefault;
    CaseStmt(long v, bool d) : value(v), isDefault(d) {}
    void print(int indent);
};

struct SwitchStmt : public Stmt {
    Expr *cond;
    CompoundStmt *body;
    SwitchStmt(Expr *c, CompoundStmt *b) : cond(c), body(b) {}
    ~SwitchStmt();
    void print(int indent);
};

// any of init, cond and step may be 0
struct ForStmt : public Stmt {
    Stmt *init;
    Expr *cond;
    Expr *step;
    Stmt *body;
    ForStmt(Stmt *i, Expr *c, Expr *s, Stmt *b) : init(i), cond(c), step(s), body(b) {}
    ~ForStmt();
    void print(int indent);
};

struct BreakStmt : public Stmt {
    void print(int indent);
};

struct ContinueStmt : public Stmt {
    void print(int indent);
};

} // namespace cc

#endif

// ---------- AST1.h ----------
// AST1.h -- LAYER 2, the C++ layer, namespace `cxx`.
//
// Declares only what C++ adds to C: reference and class types, classes and
// their members, qualified names, member access, `this` and the free store.
// Everything else is used directly from cc.  See AST.h for the layering model.
//
// C++98 only.

#ifndef AST1_H
#define AST1_H

#include <cstddef>
#include <string>
#include <vector>
#include <iostream>


namespace cxx {

// The SAME types as in cc, not copies -- pulled in so this layer can spell
// them unqualified.
using cc::Type;
using cc::BuiltinType;
using cc::PointerType;
using cc::Decl;
using cc::VarDecl;
using cc::Function;

// `protected` differs from `private` only once inheritance exists.
enum Access { ACC_Public, ACC_Private, ACC_Protected };
const char *accessText(Access a);

// --- Types added by C++ -----------------------------------------------

struct ReferenceType : public Type {
    Type *base;
    ReferenceType(Type *b) : base(b) {}
    ~ReferenceType() { delete base; }
    void print(int indent);
};

// bool -- C89 has none, so it is C++'s, exactly like T& and class types.
//
// It is a node rather than another cc::BuiltinKind because that enum is the C
// layer's, and adding to it would put a C++ type in C's table.  Nothing is
// lost by keeping it out: bool promotes to int on entering any arithmetic, so
// the rank table never needs to name it -- only the edges do, where a value is
// converted to or from bool.
struct BoolType : public Type {
    void print(int indent);
};

struct ClassType : public Type {
    std::string className;
    ClassType(const std::string &n) : className(n) {}
    void print(int indent);
};

// --- Declarations added by C++ ----------------------------------------

// A declaration with an access level and an owning class, which a plain
// cc::VarDecl has neither of.
struct FieldDecl : public Decl {
    Type *type;
    std::string name;
    Access access;
    std::string ownerClass;
    FieldDecl(Type *t, const std::string &n, Access a)
        : type(t), name(n), access(a) {}
    ~FieldDecl() { delete type; }
    void print(int indent);
};

// One entry in a constructor's initialiser list: x(1) or Base(a, b).  Which
// kind it is, is decided by the semantic pass and recorded in `isBase`.
struct MemberInit {
    std::string name;
    std::vector<cc::Expr*> args;
    bool isBase;
    int line;
    int col;
    cc::Function *resolvedCtor;         // chosen by the semantic pass; not owned
    MemberInit() : isBase(false), line(0), col(0), resolvedCtor(0) {}
};

// A method IS a function that also knows its access, its class and whether it
// is virtual -- so it derives from cc::Function instead of restating it.
// Constructors and destructors are methods too: no return type and a special
// name, but otherwise nothing different.
struct MethodDecl : public Function {
    Access access;
    std::string ownerClass;
    // Written `virtual`, or inherited by overriding a base virtual -- so the
    // semantic pass sets this as well as the parser.
    bool isVirtual;
    MethodDecl *overrides;      // 0 when this overrides nothing
    bool isConstructor;
    bool isDestructor;
    // `int get() const` -- the const applies to *this, so the method promises
    // not to modify the object, and only such a method may be called on one.
    bool isConstMethod;
    // Generated by the compiler rather than written: a destructor a class
    // needed because it owns something destructible.  Advice aimed at code the
    // user wrote should stay quiet about it.
    bool isImplicit;
    std::vector<MemberInit> memberInits;    // constructors only

    MethodDecl(Type *r, const std::string &n, Access a)
        : Function(r, n), access(a), isVirtual(false), overrides(0),
          isConstructor(false), isDestructor(false), isConstMethod(false),
          isImplicit(false) {}
    ~MethodDecl();
    // Only the first printed line differs from a plain function.
    void printSignature(int indent);
    void printBodyPrefix(int indent);
};

// SINGLE inheritance by design: with one base the derived object is the base
// object with fields appended, so an upcast is a no-op and the vptr is shared.
// The parser rejects a second base by name rather than as a syntax error.
struct ClassDecl : public Decl {
    std::string name;
    std::string baseName;       // empty when the class has no base
    Access baseAccess;          // public/private/protected inheritance
    ClassDecl *base;            // resolved by the semantic pass; not owned
    std::vector<Decl*> members;
    // Aliases into `members`, which owns them.  Constructors are indexed apart
    // because they share one name; they are selected by argument count.
    std::vector<MethodDecl*> ctors;
    MethodDecl *dtor;               // 0 when the class declares none
    // The functions granted access to the private parts, by NAME AND
    // SIGNATURE: `friend int peek(Box);` grants nothing to `peek(Box, int)`.
    // Aliases only -- a prototype that has no definition to belong to is kept
    // in friendProtos, which is what this class deletes.
    std::vector<cc::Function*> friends;
    std::vector<cc::Function*> friendProtos;
    ClassDecl(const std::string &n)
        : name(n), baseAccess(ACC_Public), base(0), dtor(0) {}
    ~ClassDecl();
    void print(int indent);
};

// --- Qualified name  A::B ---------------------------------------------
struct QualifiedName : public cc::ASTNode {
    std::vector<std::string> parts;
    QualifiedName() {}
    void print(int indent);
};

// --- Expressions added by C++ -----------------------------------------
// The cc:: forms are used directly, not redeclared.  Only new forms appear
// here, each deriving from cc::Expr so both share one tree:  (a.b + 1) * 2

struct MemberAccessExpr : public cc::Expr {
    cc::Expr *base;
    std::string member;
    bool isArrow;
    MemberAccessExpr(cc::Expr *b, const std::string &m, bool arrow)
        : base(b), member(m), isArrow(arrow) {}
    ~MemberAccessExpr() { delete base; }
    void print(int indent);
};

struct ThisExpr : public cc::Expr {
    void print(int indent);
};

// true and false
struct BoolExpr : public cc::Expr {
    bool value;
    BoolExpr(bool v) : value(v) {}
    void print(int indent);
};

// T(args) -- an unnamed object built where it stands.  `return V(x + o.x);`
// is the ordinary body of an overloaded operator, so this is not a luxury.
struct TempExpr : public cc::Expr {
    Type *type;                         // owned
    std::vector<cc::Expr*> args;        // owned
    cc::Function *resolvedCtor;         // chosen by the semantic pass; not owned
    TempExpr(Type *t) : type(t), resolvedCtor(0) {}
    ~TempExpr();
    void print(int indent);
};

struct NewExpr : public cc::Expr {
    Type *allocType;
    // new T[n]: the count, owned, and 0 for the single-object form.  It is an
    // EXPRESSION, not a constant -- the bound of a heap array is a value the
    // program computes, which is the whole reason to want one.
    cc::Expr *count;
    std::vector<cc::Expr*> args;
    cc::Function *resolvedCtor;         // chosen by the semantic pass; not owned
    NewExpr(Type *t) : allocType(t), count(0), resolvedCtor(0) {}
    ~NewExpr();
    void print(int indent);
};

struct DeleteExpr : public cc::Expr {
    cc::Expr *operand;
    // delete[] -- it must match the new that made the block, and here that is
    // checked rather than assumed: the allocator records which form was used.
    bool isArray;
    DeleteExpr(cc::Expr *e) : operand(e), isArray(false) {}
    ~DeleteExpr() { delete operand; }
    void print(int indent);
};

} // namespace cxx

#endif

// ---------- SymbolTable.h ----------
// SymbolTable.h -- scopes and the names in them.
//
// A stack of Scopes: the innermost is searched first and lookup walks outward.
// Class members get a scope of their own, pushed while a method body is
// analysed, which is what makes an unqualified `x` find the class's field.
//
// C++98 only.

#ifndef SYMBOLTABLE_H
#define SYMBOLTABLE_H

#include <cstddef>
#include <map>
#include <string>
#include <vector>

// The AST lives in two namespaces: the C layer is `cc`, the C++ layer is
// `cxx` (see the layering model at the top of AST.h).  Only the C layer's
// root and type nodes are needed here, so they are forward declared.
namespace cc {
    struct ASTNode;
    struct Type;
}

enum SymbolKind {
    SYM_Var,
    SYM_Type,
    SYM_Method,
    SYM_Field
};

struct Symbol {
    SymbolKind kind;
    std::string name;
    // Any AST node may own a symbol: cc::VarDecl, cc::Function, cxx::FieldDecl.
    // cc::ASTNode is the common root of all of them.
    cc::ASTNode *decl;
    cc::Type *type;         // not owned
    Symbol(SymbolKind k, const std::string &n, cc::ASTNode *d, cc::Type *t)
        : kind(k), name(n), decl(d), type(t) {}
};

class Scope {
public:
    Scope() {}
    ~Scope();
    // Returns false if the name is already declared in THIS scope.
    bool insert(const std::string &name, Symbol *sym);
    Symbol *lookup(const std::string &name) const;

private:
    std::map<std::string, Symbol*> table;

    Scope(const Scope &);
    Scope &operator=(const Scope &);
};

class SymbolTable {
public:
    SymbolTable();
    ~SymbolTable();

    void pushScope();
    void popScope();

    bool insert(const std::string &name, Symbol *sym);
    // Innermost outward.
    Symbol *lookup(const std::string &name) const;
    // Current scope only -- the redeclaration test.
    Symbol *lookupLocal(const std::string &name) const;

private:
    std::vector<Scope*> stack;

    SymbolTable(const SymbolTable &);
    SymbolTable &operator=(const SymbolTable &);
};

#endif

// ---------- Parser.h ----------
// Parser.h -- LAYER 1, the C layer's parser, namespace `cc`.
//
// The base of the layered parser.  Every point where C++ extends C is virtual;
// see AST.h for the model and Parser1.h for what the C++ layer does with them.
//
// Errors are reported, never fatal: the parser tells Diagnostics and then
// resynchronises, so one mistake does not hide the next.
//
// C++98 only.

#ifndef PARSER_H
#define PARSER_H


#include <cstddef>
#include <string>
#include <vector>

namespace cc {

class Parser {
public:
    Parser(const std::string &s, Diagnostics &d);
    virtual ~Parser();               // virtual: this class is a base

    // The whole file: a list of declarations.  Virtual dispatch inside means a
    // cxx::Parser instance parses classes here too.
    std::vector<Decl*> parseTranslationUnit();

    // Convenience for the C layer on its own: parse a single function.
    Function *parseSingleFunction();

protected:
    void advance();
    Token cur;
    ::Lexer *lexer;
    Diagnostics &diag;

    // --- error reporting and recovery ------------------------------------
    void errorAtCurrent(const std::string &msg);
    bool expect(TokenKind k, const char *context);  // reports if it does not match
    bool match(TokenKind k);                        // consume if it matches
    void synchronize();         // panic-mode: skip to a plausible restart point
    // Reports the one message a reserved keyword deserves and skips the whole
    // construct, so an unsupported feature costs one diagnostic and not a
    // cascade of them.  Returns true when it consumed something.
    bool skipReservedConstruct();
    // Skips to the end of the current declaration or statement, counting
    // braces so a body is stepped over whole.
    void skipConstruct();
    // Steps over a balanced ( ... ), for a reserved word used as an operator.
    // A declaration made as a side effect of another: a friend function
    // defined inside a class body belongs at FILE scope, not in the class.
    std::vector<Decl*> pending;

    void skipParenGroup();
    bool peekIsStar();
    // `vector<int> v;` named as a template rather than read as a comparison.
    bool skipTemplateDeclaration();
    // Set by skipReservedConstruct so the caller does not resynchronise on top
    // of a skip that already landed cleanly.
    bool suppressSync;

    // --- speculation ------------------------------------------------------
    // Point p; and p.x = 1; both start with an identifier, so parseStatement()
    // tries the declaration rule and rewinds if it does not fit.
    struct State {
        Token cur;
        ::Lexer::Position lexPos;
    };
    State save() const;
    void restore(const State &st);

    // --- declarations -----------------------------------------------------
    // The return type and name are already consumed by the caller.
    Function *parseFunctionRest(Type *retType, const std::string &name);
    // Fills an already-created node, so the C++ layer can pass a MethodDecl --
    // which is a cc::Function -- and have this layer populate it.
    void parseFunctionParamsAndBody(Function *fn);
    // nameLine/nameCol point a diagnostic at the variable, not its type keyword.
    VarDecl *parseVarDeclTail(Type *type, const std::string &name,
                              int nameLine, int nameCol);

    // --- statements -------------------------------------------------------
    CompoundStmt *parseBlock();
    Stmt *parseIf();
    Stmt *parseWhile();
    Stmt *parseFor();
    Stmt *parseReturn();
    Stmt *parseDoWhile();
    Stmt *parseSwitch();
    Stmt *parseExprStatement();

    // --- the precedence chain, defined ONCE, here -------------------------
    Expr *parseExpression();
    Expr *parseAssign();
    Expr *parseLogicalOr();
    Expr *parseLogicalAnd();
    Expr *parseEquality();
    Expr *parseRelational();
    Expr *parseShift();
    Expr *parseAddSub();
    Expr *parseMulDiv();
    Expr *parseUnary();
    // (T)expr, told from a parenthesised expression by trying the type rule
    // and rewinding when it does not fit.
    Expr *parseCastOrParen();
    Expr *parsePostfix();
    Expr *parseCallSuffix(Expr *callee);
    // a[i] is desugared to *(a + i), which is what it means in C -- so it
    // needs no node, no type rule and no lowering of its own.
    Expr *parseIndexSuffix(Expr *base);

    // C's type grammar:  int   int*   int**
    Type *parsePointerSuffixes(Type *base);
    // In C the array part follows the NAME -- int a[10] -- so it is applied by
    // the declarator, not by parseType.  Dimensions nest inside out:
    // int a[3][4] is 3 arrays of 4 ints.
    Type *parseArraySuffixes(Type *element);

    // Recursive descent has exactly one failure mode a program can reach from
    // outside: nest deeply enough and the C++ stack runs out before the parse
    // does.  A limit turns a crash into a diagnostic.
    //
    // The number is not the parser's to choose.  What the parser accepts, the
    // semantic pass, the lowering and the AST's destructor each walk again,
    // recursively, with much larger frames -- analyzeExprImpl alone is 514
    // lines -- so the limit has to be one those passes survive on the smallest
    // stack this compiler is expected to run on.  Measured, on a chain of
    // `cout << x`, which is the most expensive link a program is likely to
    // write: 512KB dies between 60 and 80, 1MB between 120 and 160.  1MB is
    // the default main-thread stack on Windows and on iOS, so 100 is the
    // number, with the margin on the side of the constrained host.  256 was
    // chosen when the parser was the only thing counting, and a program at 256
    // crashed everything downstream on both.
    static const int MaxNesting = 100;
    int nesting;
    bool nestingReported;
    bool tooDeep();                 // reports once, then stays quiet

    // The same failure, reached the other way.  `a + b + c + ...` is parsed by
    // a LOOP, so the parser's own recursion never grows -- but the tree it
    // builds is one level deeper per operator, and every pass after the parser
    // walks that tree recursively.  So the parse survived 20,000 terms and the
    // semantic pass, the lowering and the AST's own destructor did not.
    //
    // Counted per outermost EXPRESSION, because the depth that matters is one
    // expression's, not a function's: four hundred short statements are fine
    // and a program made of them must stay fine.  `nesting` cannot answer that
    // question -- a block bumps it too, so inside any function it is never
    // zero and the count would run on across every statement in the body.
    int exprNesting;
    long chainLinks;
    bool chainReported;
    bool chainTooDeep();            // counts one link, reports once

    // --- extension points overridden by the C++ layer ---------------------
    virtual Decl *parseDeclaration();
    virtual Stmt *parseStatement();
    // The body of it.  parseStatement itself is only the depth count, so that
    // one place answers for every shape of nesting a statement can have.
    Stmt *parseStatementImpl();
    virtual Expr *parsePrimary();
    virtual Type *parseType();
    virtual Expr *parseMemberSuffix(Expr *base);    // 0 if not a member access
    virtual void parseFunctionTail(Function *fn);   // a ctor's initialiser list
    virtual void parseVarInitializer(VarDecl *vd);  // = expr, and C++'s (args)

private:
    // not copyable (C++98 way: declared private, never defined)
    Parser(const Parser &);
    Parser &operator=(const Parser &);
};

} // namespace cc

#endif

// ---------- Parser1.h ----------
// Parser1.h -- LAYER 2, the C++ layer's parser, namespace `cxx`.
//
// Derives from cc::Parser.  The expression chain, control flow and function
// bodies are inherited, not copied; this class adds classes, base clauses,
// constructors, initialiser lists, and the C++ forms of type, primary and
// postfix expression.
//
// C++98 only.

#ifndef PARSER1_H
#define PARSER1_H


#include <set>
#include <string>
#include <vector>

namespace cxx {

class Parser : public cc::Parser {
public:
    Parser(const std::string &s, Diagnostics &d);

private:
    // new in the C++ layer
    ClassDecl *parseClass();
    // int Point::getX() { ... } at file scope: the body of a method declared
    // inside the class.  Returns 0 when the declaration is not qualified.
    Decl *parseOutOfLineDefinition();
    Decl *parseMemberDecl(const std::string &className, Access access);
    QualifiedName *parseQualifiedName();
    // Is the token stream sitting on  ClassName (  -- i.e. a constructor?
    // Needs two tokens of lookahead, which the inherited save()/restore()
    // rewind provides.
    bool looksLikeConstructor(const std::string &className);

    // Every class name seen so far.  Without it `(x)` reads as a cast to a
    // class named x, and plain C code like `return (x);` stops compiling.
    // A name is recorded as soon as it is parsed, so a class may mention
    // itself: `Node *next;`.
    std::set<std::string> classNames;
    bool namesAClass(const std::string &n) const;
    // The name of an operator member, read after the `operator` keyword.
    std::string operatorMemberName();
    // `friend` grants access; it declares no member.  An inline definition is
    // hoisted to file scope, which is where the function actually lives.
    void parseFriend();
    // V operator*(int k, V v) { ... } at file scope.  Only a non-member can
    // put the class on the RIGHT of the operator, which is the whole reason
    // this form exists.
    Decl *parseOperatorFunction();
    ClassDecl *classBeingParsed;        // set while a class body is parsed

    // extension points taken over from the C layer.
    // C++98 has no `override` keyword -- each signature must match
    // cc::Parser's exactly or this would silently hide instead of override.
    virtual cc::Decl *parseDeclaration();
    virtual cc::Expr *parsePrimary();
    virtual cc::Expr *parseMemberSuffix(cc::Expr *base);
    virtual cc::Type *parseType();
    virtual void parseFunctionTail(cc::Function *fn);
    virtual void parseVarInitializer(cc::VarDecl *vd);
};

} // namespace cxx

#endif

// ---------- Semantic.h ----------
// Semantic.h -- PASS 3, semantic analysis.
//
// Walks the one tree the layered parser produced, which mixes cc:: and cxx::
// nodes, so this pass is deliberately not split in two.
//
// Checks: names resolve and nothing is declared twice; reference initialisers
// and assignment targets are lvalues; types match on initialisation,
// assignment, arithmetic, calls and return; access control including protected
// through derivation; overrides, hiding and the virtual-destructor rule;
// initialiser-list membership, duplication and declaration order.
//
// C++98 only.

#ifndef SEMANTIC_H
#define SEMANTIC_H

#include <map>
#include <string>
#include <vector>


class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(Diagnostics &d);
    ~SemanticAnalyzer();

    // entry point: a whole translation unit
    void analyze(const std::vector<cc::Decl*> &units);

    // The resolved hierarchy, for the layout pass -- one answer to "what is
    // this class's base", with the cycles already broken.
    const std::map<std::string, cxx::ClassDecl*> &classMap() const { return classes; }

private:
    SymbolTable symbols;
    Diagnostics &diag;

    std::map<std::string, cxx::ClassDecl*> classes;
    // A name no longer identifies a function, so every declaration of a name
    // is kept and the call site chooses between them.
    std::map<std::string, std::vector<cc::Function*> > overloads;

    // Context for the function being analysed.
    cc::Type *currentReturnType;
    std::string currentClass;       // empty outside a method body
    // The function being analysed.  Access control needs it: a friend is
    // granted by name AND signature, so knowing the name is not enough.
    cc::Function *currentFunction;
    bool isFriendOf(cxx::ClassDecl *owner) const;
    // A ctor/dtor has no return type at all, which is not the same as void.
    bool currentIsCtorOrDtor;
    // Inside a const member function every field is const, because *this is.
    bool currentMethodIsConst;
    int loopDepth;                  // break/continue legality
    int switchDepth;                // `break` is legal inside a switch too

    // Types the analyzer forms itself belong to no AST node, so it owns them
    // and frees them in its destructor.  A formed type must own every node in
    // it -- borrowing a subtree the AST will delete leaves a dangling pointer.
    std::vector<cc::Type*> ownedTypes;
    cc::Type *makeBuiltin(cc::BuiltinKind k);
    cc::Type *cloneType(cc::Type *t);
    cc::Type *cloneTypeShape(cc::Type *t);
    cc::Type *makePointerTo(cc::Type *t);

    // passes
    void collectClasses(const std::vector<cc::Decl*> &units);
    // Runs before anything looks a member up: member lookup walks this chain.
    void resolveBases();
    // Attaches each out-of-line definition to the declaration inside its class.
    void attachOutOfLineDefinitions(const std::vector<cc::Decl*> &units);
    void resolveOverrides(cxx::ClassDecl *cd);
    void analyzeMemberInits(cxx::MethodDecl *ctor, cxx::ClassDecl *cd);
    void checkClassInvariants(cxx::ClassDecl *cd);
    // By argument count.  0 means the class declares no constructors, which is
    // legal and means there is nothing to call.
    cxx::MethodDecl *selectConstructor(cxx::ClassDecl *cd,
                                       const std::vector<cc::Expr*> &args,
                                       cc::ASTNode *at, const std::string &what);
    void recordScopeExitDestruction(cc::CompoundStmt *block,
                                    const std::vector<cc::VarDecl*> &declared);
    bool hasDestructor(cc::Type *t);
    // A bodyless declaration of a built-in name IS the binding to it, so it
    // has to agree with the machine's own signature.
    void checkNativeDeclaration(cc::Function *fn);
    // Names a parameter in a diagnostic, by name or, having none, by position.
    static std::string parameterText(cc::Function *fn, std::size_t i);
    bool needsDestructor(cxx::ClassDecl *cd);
    // A class that owns something destructible gets a destructor whether or
    // not one was written -- otherwise nothing runs its members' destructors.
    void synthesiseDestructors();
    // The copy constructor a class declares, or 0.
    // Exact enough to win outright, in overload resolution only.
    bool exactForOverload(cc::Type *arg, cc::Type *param);
    cxx::MethodDecl *copyConstructorOf(cxx::ClassDecl *cd);
    // A copy is memberwise: the base is copied by ITS copy constructor and so
    // is every member that has one.  A class that declares none of its own
    // still needs one when anything it is made of has one, or the copy is
    // done with memcpy and those constructors never run.
    bool needsCopyConstructor(cxx::ClassDecl *cd);
    // Whether one CAN be generated: every base and class-typed member the
    // generated list names must itself be constructible from one argument.
    bool canSynthesiseCopy(cxx::ClassDecl *cd, int depth = 0);
    // Generates it for cd, and first for every part cd's list will name.
    bool synthesiseCopyFor(cxx::ClassDecl *cd);
    void synthesiseCopyConstructors();
    void declareTopLevel(const std::vector<cc::Decl*> &units);
    void analyzeDecl(cc::Decl *d);
    void analyzeClass(cxx::ClassDecl *cd);
    void analyzeFunction(cc::Function *fn);
    void analyzeStmt(cc::Stmt *s);
    void analyzeSwitch(cc::SwitchStmt *s);
    void analyzeBlock(cc::CompoundStmt *block);
    void analyzeVarDecl(cc::VarDecl *vd, bool declareIt);
    // analyzeExpr is a thin wrapper that records the result on the node;
    // analyzeExprImpl is the analysis itself.
    cc::Type *analyzeExpr(cc::Expr *e, bool &isLValue);
    cc::Type *analyzeExprImpl(cc::Expr *e, bool &isLValue);

    // member lookup
    cxx::ClassDecl *findClass(const std::string &name);
    // Overloaded operators: the member an expression calls, and the check that
    // its one argument fits.
    // The function an operator expression calls: a member when the LEFT
    // operand is the object, otherwise a non-member -- which is the only form
    // that can put the class on the right, as in  3 * v.
    cc::Function *findOperator(cc::Expr *lhs, cc::Type *lt,
                               cc::Expr *rhs, cc::Type *rt,
                               cc::BinaryOp op, cc::ASTNode *at);
    cxx::MethodDecl *findCallOperator(cc::Type *ot, cc::CallExpr *call);
    cxx::MethodDecl *findIndexOperator(cxx::ClassDecl *cd, cc::Expr *index,
                                       cc::Type *it, bool objectConst, cc::ASTNode *at);
    cc::Function *findUnaryMinusOperator(cc::Expr *operand, cc::Type *t,
                                         cc::ASTNode *at);
    cxx::MethodDecl *findMemberOperator(cc::Type *lt, cc::BinaryOp op,
                                        cc::Expr *rhs, cc::Type *rt, cc::ASTNode *at);
    cc::Function *findFreeOperator(cc::Expr *lhs, cc::Type *lt,
                                   cc::Expr *rhs, cc::Type *rt,
                                   const std::string &name);
    bool isClassType(cc::Type *t);
    // Does this expression name something declared const?  A member of a const
    // object is const too, which is what stops  a.x = 1  through a const A&.
    bool isConstExpr(cc::Expr *e);
    static bool isNonConstReferenceTo(cc::Type *t);
    bool objectIsConst(cxx::MemberAccessExpr *ma);
    // One const check for every form of member call.
    void checkConstUse(cxx::MethodDecl *m, bool objectConst, cc::ASTNode *at);
    // Const may be added by a conversion, never removed.
    bool constQualificationOk(cc::Type *from, cc::Type *to);
    // Walks the base chain, most derived first -- which IS name hiding.
    // `foundIn` receives the class it was found in, for the diagnostic.
    cc::Decl *findMember(cxx::ClassDecl *cd, const std::string &member,
                         cxx::ClassDecl **foundIn = 0);
    static bool isDerivedFrom(cxx::ClassDecl *derived, cxx::ClassDecl *base);
    // public everywhere, protected inside the class or anything derived from
    // it, private only inside the class itself.
    bool memberIsAccessible(cc::Decl *m, cxx::ClassDecl *owner) const;
    cxx::ClassDecl *ownerClassOf(cc::Decl *m);
    static bool sameSignature(cc::Function *a, cc::Function *b);
    static cxx::Access memberAccess(cc::Decl *m);
    static cc::Type *memberType(cc::Decl *m);
    // Pushes a scope holding cd's members, so a method body sees them unqualified.
    void pushClassScope(cxx::ClassDecl *cd);

    // helpers
    void error(cc::ASTNode *at, const std::string &msg);
    static cc::Type *stripReference(cc::Type *t);
    // An array used in an expression becomes a pointer to its first element.
    // Only & and a declaration see the array type itself.
    cc::Type *decay(cc::Type *t);
    static std::string describe(cc::Type *t);
    static bool sameType(cc::Type *a, cc::Type *b);
    // Two questions that are not the same one: sameType asks whether two
    // VALUES have the same type (references stripped, const ignored), which is
    // what conversions need; sameDeclaredType asks whether two DECLARATIONS
    // are identical, which is what a signature is.  Conflating them let a
    // friend grant to peek(const A&) reach peek(A&).
    static bool sameDeclaredType(cc::Type *a, cc::Type *b);
    // sameType plus the upcasts single inheritance makes free.
    bool canConvert(cc::Type *from, cc::Type *to);
    // canConvert, plus the rule needing the EXPRESSION: literal 0 is the null
    // pointer constant, though its type is int.
    bool convertible(cc::Expr *fromExpr, cc::Type *from, cc::Type *to);
    static bool isNullPointerConstant(cc::Expr *e);
    cxx::ClassDecl *classOf(cc::Type *t);   // through one pointer or reference
    // The class a MEMBER is made of, through any number of array dimensions.
    // Not through a pointer: a pointer member is copied as the value it is.
    cxx::ClassDecl *memberClassOf(cc::Type *t);
    static bool isVoid(cc::Type *t);
    // The builtin kind a type names, or BK_Void when it names none.
    static bool builtinKindOf(cc::Type *t, cc::BuiltinKind &out);
    static bool isBoolType(cc::Type *t);
    bool isTestable(cc::Type *t);   // usable as a condition
    // The kind a type contributes to arithmetic.  bool answers BK_Int, because
    // that is what it promotes to -- which is why the C layer's kind table
    // needs no entry for a C++ type.
    static bool arithmeticKind(cc::Type *t, cc::BuiltinKind &out);
    cc::Type *makeBool();
    // Integral promotion: anything of rank below int becomes int.
    static cc::BuiltinKind promote(cc::BuiltinKind k);
    // The usual arithmetic conversions -- the common type two operands meet in.
    static cc::BuiltinKind usualArithmetic(cc::BuiltinKind a, cc::BuiltinKind b);
    // Would converting `from` to `to` lose information?
    static bool isNarrowing(cc::BuiltinKind from, cc::BuiltinKind to);
    // Legal conversions that may lose the value are allowed but reported.
    void warnIfNarrowing(cc::Expr *e, cc::Type *from, cc::Type *to,
                         cc::ASTNode *at, const std::string &what);
    // A literal whose value fits the target is not narrowing -- otherwise
    // `short s = 1;` would warn, and nothing small would ever be assignable.
    static bool literalFitsIn(cc::Expr *e, cc::BuiltinKind to);
    // A stream rather than sprintf: no fixed buffer, and MSVC stays quiet.
    static std::string countText(std::size_t n);
    // false when the type names a class that was never declared -- in which
    // case the caller skips further checks rather than cascading.
    bool checkTypeIsKnown(cc::Type *t, cc::ASTNode *at, const std::string &where);
    void checkCallArgs(cc::CallExpr *call, cc::Function *fn);
    // Picks the overload a call names.  An exact match wins outright; failing
    // that a single convertible candidate wins; anything else is an error the
    // programmer has to resolve.
    cc::Function *resolveOverload(const std::vector<cc::Function*> &candidates,
                                  cc::CallExpr *call, const std::string &name);
    // Do two declarations of one name describe the same function?
    static bool sameParams(cc::Function *a, cc::Function *b);
    // Every method of this name in the class and its bases, most derived
    // first -- the candidate set a member call chooses from.
    std::vector<cc::Function*> findMethods(cxx::ClassDecl *cd, const std::string &name);

    // not copyable (C++98 way: declared private, never defined)
    SemanticAnalyzer(const SemanticAnalyzer &);
    SemanticAnalyzer &operator=(const SemanticAnalyzer &);
};

#endif

// ---------- Layout.h ----------
// Layout.h -- PASS 4, the object model.
//
// Where each field sits, how big an object is, and which function a virtual
// call reaches.  Single inheritance makes the rules fit in three sentences:
//
//   * A derived object begins with its base subobject, so a Derived* and its
//     Base* are the same address and an upcast costs nothing.
//   * If a class or any base has a virtual function, the object starts with a
//     pointer to its vtable; a base that already has one shares it.
//   * A vtable is its base's, copied, with overridden slots replaced and new
//     virtuals appended -- so a slot index means the same thing all the way
//     down the chain.
//
// Multiple inheritance would break all three at once.
//
// C++98 only.

#ifndef LAYOUT_H
#define LAYOUT_H

#include <map>
#include <set>
#include <string>
#include <vector>


struct FieldLayout {
    std::string name;
    std::string ownerClass;     // the class that declared it
    cc::Type *type;             // not owned
    int offset;
    int size;
};

// One step of building or taking apart an object.  The plan is computed per
// class and is the same for every constructor of it, apart from which body
// runs -- because the ORDER is fixed by the class, never by the constructor.
struct InitStep {
    enum Kind { StepBase, StepVPtr, StepField, StepBody };
    Kind kind;
    std::string name;
    InitStep(Kind k, const std::string &n) : kind(k), name(n) {}
};

struct ClassLayout {
    std::string name;
    int size;                   // bytes, including the vptr and any padding
    int align;
    bool hasVPtr;
    int firstOwnField;          // index into `fields` where this class's own start
    std::vector<FieldLayout> fields;             // base fields first
    std::vector<cxx::MethodDecl*> vtable;        // slot -> final override
    // Base first, then this class's own fields in DECLARATION order, then the
    // constructor body.  Destruction is this list reversed, exactly.
    std::vector<InitStep> constructionPlan;
    std::vector<InitStep> destructionPlan;
    bool hasDtor;                                // this class or any base
    bool hasCtor;                                // this class declares one
    ClassLayout()
        : size(0), align(1), hasVPtr(false), firstOwnField(0),
          hasDtor(false), hasCtor(false) {}
};

class Layout {
public:
    explicit Layout(Diagnostics &d);

    // The machine an object has to fit in.  Defaults to MachineMemory, which
    // is what the VM defaults to; a host that shrinks one must shrink both, or
    // the front end accepts an array the machine will refuse to load.
    void setMemoryLimit(vmword bytes) { memoryLimit = bytes; }

    // Computes a layout for every class, base classes first.
    void computeAll(const std::map<std::string, cxx::ClassDecl*> &classes);

    const ClassLayout *forClass(const std::string &name) const;

    // Byte size of any type; 0 and a diagnostic for something with no size,
    // or for an object too large for the machine to hold.
    int sizeOf(cc::Type *t) const;
    int alignOf(cc::Type *t) const;

    void print() const;

    // Builtin sizes live in the type model (builtinSize in AST.h); only the
    // pointer size is Layout's own, because no builtin type describes one.
    static const int PointerSize = 8;
    static const int IntSize = 4;

private:
    Diagnostics &diag;
    // sizeOf is const and is called for every type in the program, so an
    // oversized array would otherwise be reported once per mention.  One
    // mistake costs one line.
    mutable bool reportedOversize;
    vmword memoryLimit;
    std::map<std::string, ClassLayout> layouts;

    // Needed during the recursion: a class-typed FIELD has to be laid out
    // before its container, and the map is the only way from a name to a
    // declaration.  Without it the answer depended on the alphabetical order
    // computeAll happened to walk.
    const std::map<std::string, cxx::ClassDecl*> *classIndex;
    std::set<std::string> inProgress;           // catches A-contains-B-contains-A

    // Depth-first, so a base -- and any class-typed member -- is always laid
    // out before the class that needs its size.
    void computeFor(cxx::ClassDecl *cd);
    // The class a field's type names, or 0 when the field is not a class.
    cxx::ClassDecl *classDeclOf(cc::Type *t) const;
    static int roundUp(int value, int alignment);

    Layout(const Layout &);
    Layout &operator=(const Layout &);
};

#endif

// ---------- IR.h ----------
// IR.h -- PASS 5a, the intermediate representation.
//
// There is no IR1.h, and that is the point.  Lowering's whole job is to erase
// the C++ layer -- a method becomes a function taking `this`, a reference
// becomes a pointer, a field becomes an address plus an offset, a virtual call
// becomes load-vptr / index / call.  By the time anything reaches this file
// there are no classes, references, inheritance or dispatch left.  So the two
// lowering PASSES are layered as usual (Lower.h, Lower1.h) but both target
// this one C-level instruction set.
//
// Shape: a flat list of three-address instructions per function over unlimited
// virtual registers, with labels rather than basic blocks.  Linear is easier
// to read and lowers directly to either a stack VM or emitted C.
//
// C++98 only.

#ifndef IR_H
#define IR_H

#include <cstddef>
#include <string>
#include <vector>


// No register allocation here -- that is the code generator's problem.
typedef int IRReg;
const IRReg IR_NoReg = -1;

enum IROp {
    // --- values -------------------------------------------------------
    IR_Const,        // dest = imm
    IR_FConst,       // dest = fimm
    IR_StringAddr,   // dest = address of string constant `sym`
    IR_Move,         // dest = a

    // --- integer arithmetic -------------------------------------------
    IR_Add, IR_Sub, IR_Mul, IR_Div, IR_Mod,
    IR_UDiv, IR_UMod,       // unsigned division differs from signed
    IR_Shl, IR_Shr,  // dest = a << b, a >> b  (>> is arithmetic when signed)
    IR_UShr,         // logical shift right, for an unsigned left operand
    IR_Neg,          // dest = -a
    IR_LogicalNot,   // dest = (a == 0)

    // --- floating arithmetic ------------------------------------------
    // Separate opcodes rather than a flag: float add and integer add are
    // different machine operations, and a dump that hides that is lying.
    IR_FAdd, IR_FSub, IR_FMul, IR_FDiv, IR_FNeg,

    // --- comparison (all yield 0 or 1) --------------------------------
    IR_CmpEQ, IR_CmpNE, IR_CmpLT, IR_CmpGT, IR_CmpLE, IR_CmpGE,
    IR_UCmpLT, IR_UCmpGT, IR_UCmpLE, IR_UCmpGE,     // unsigned orderings
    IR_FCmpEQ, IR_FCmpNE, IR_FCmpLT, IR_FCmpGT, IR_FCmpLE, IR_FCmpGE,

    // --- conversions --------------------------------------------------
    // Every one of these is a real machine operation, so lowering emits them
    // explicitly rather than letting a size mismatch pass silently.
    IR_IntToFloat,   // imm = 1 when the source is unsigned
    IR_FloatToInt,
    IR_FloatResize,  // float <-> double
    IR_IntResize,    // imm = target size in bytes; b = 1 when sign-extending

    // --- addresses ----------------------------------------------------
    IR_LocalAddr,    // dest = address of local slot imm
    IR_GlobalAddr,   // dest = address of global `sym`
    IR_FieldAddr,    // dest = a + imm        -- a field at a constant offset
    IR_FuncAddr,     // dest = address of function `sym`

    // --- memory -------------------------------------------------------
    IR_Load,         // dest = *a             (imm = size in bytes)
    IR_Store,        // *a = b                (imm = size in bytes)
    // An object is copied whole.  A class does not fit in a register, so
    // load-then-store would shift its bytes off the end of one.
    IR_MemCopy,      // *a = *b               (imm = size in bytes)

    // --- calls --------------------------------------------------------
    IR_Call,         // dest = sym(args...)
    IR_CallIndirect, // dest = (*a)(args...)  -- this is a virtual call

    // --- dispatch -----------------------------------------------------
    // One opcode rather than three loads, so a dump stays readable; the code
    // generator expands it.
    IR_VCallTarget,  // dest = (*(vptr of a))[imm]

    // --- free store ---------------------------------------------------
    IR_Alloc,        // dest = allocate imm bytes, or a bytes when a is a register;
                     // b, when set, is the element count of a new[]
    IR_ArrayCount,   // dest = how many elements the new[] block at a holds
    IR_Free,         // release a

    // --- control ------------------------------------------------------
    IR_Label,        // imm = label id
    IR_Jump,         // goto imm
    IR_BranchZero,   // if a == 0 goto imm
    IR_BranchNZ,     // if a != 0 goto imm
    IR_Return        // return a, or return nothing when a is IR_NoReg
};

const char *irOpName(IROp op);

struct IRInstr {
    IROp op;
    IRReg dest;
    IRReg a;
    IRReg b;
    long imm;
    double fimm;            // IR_FConst
    // A load or store of a floating value moves different bits than an integer
    // one of the same width -- a 4-byte float is not the low half of a double.
    bool isFloat;
    // IR_Free: which FORM of delete this is.  The allocator records the form in
    // the block, so `delete` on a `new[]` block is caught rather than left
    // undefined the way the language leaves it.  IR_Alloc says the same thing
    // by carrying an element count in `b`.
    bool isArray;
    std::string sym;            // callee or global name
    std::vector<IRReg> args;    // for IR_Call / IR_CallIndirect
    int line;

    IRInstr(IROp o)
        : op(o), dest(IR_NoReg), a(IR_NoReg), b(IR_NoReg), imm(0), fimm(0.0),
          isFloat(false), isArray(false), line(0) {}
};

// A class-typed local occupies its whole object size here, which is what makes
// `Point p;` a real object rather than a pointer to one.
struct IRLocal {
    std::string name;
    int slot;
    int size;
    bool isParam;
    // A 4-byte float slot has to be written as a float, not as four bytes of
    // an integer -- otherwise a float parameter arrives as noise.
    bool isFloat;
    // A by-value object parameter: the caller passes an ADDRESS and the VM
    // copies the bytes in, because that is what "by value" means and an
    // object does not fit in the register the argument travelled in.
    bool isObject;
    IRLocal(const std::string &n, int s, int sz, bool p, bool f = false, bool o = false)
        : name(n), slot(s), size(sz), isParam(p), isFloat(f), isObject(o) {}
};

struct IRFunction {
    std::string name;           // mangled:  Point__getX
    std::string sourceName;     // as written:  Point::getX
    int paramCount;             // includes `this` where there is one
    bool returnsValue;
    std::vector<IRLocal> locals;
    std::vector<IRInstr> code;

    IRFunction(const std::string &mangled, const std::string &source)
        : name(mangled), sourceName(source), paramCount(0),
          returnsValue(false), nextReg(0), nextLabel(0) {}

    IRReg newReg() { return nextReg++; }
    int newLabel() { return nextLabel++; }
    int addLocal(const std::string &n, int size, bool isParam, bool isFloat = false,
                 bool isObject = false);

    // Every emit returns its destination register, so expressions compose.
    IRReg emitConst(long value, int line);
    IRReg emitFConst(double value, int line);
    IRReg emitStringAddr(const std::string &sym, int line);
    IRReg emitConvert(IROp op, IRReg a, long imm, IRReg signFlag, int line);
    IRReg emitUnary(IROp op, IRReg a, int line);
    IRReg emitBinary(IROp op, IRReg a, IRReg b, int line);
    IRReg emitLocalAddr(int slot, int line);
    IRReg emitGlobalAddr(const std::string &sym, int line);
    IRReg emitFieldAddr(IRReg base, long offset, int line);
    IRReg emitFuncAddr(const std::string &sym, int line);
    IRReg emitLoad(IRReg addr, int size, bool isFloat, int line);
    void  emitStore(IRReg addr, IRReg value, int size, bool isFloat, int line);
    void  emitMemCopy(IRReg dst, IRReg src, int size, int line);
    IRReg emitCall(const std::string &sym, const std::vector<IRReg> &args,
                   bool wantsResult, int line);
    IRReg emitCallIndirect(IRReg target, const std::vector<IRReg> &args,
                           bool wantsResult, int line);
    IRReg emitVCallTarget(IRReg object, long slot, int line);
    IRReg emitAlloc(long bytes, int line);
    // The size in a register, and for new[] the element count beside it.  The
    // count is stored in the block: the SIZE cannot stand in for it, because
    // the allocator rounds a block up and five four-byte elements would come
    // back as six.
    IRReg emitAllocN(IRReg bytes, IRReg count, int line);
    IRReg emitArrayCount(IRReg ptr, int line);
    void  emitFree(IRReg ptr, int line, bool isArray = false);
    void  emitLabel(int label);
    void  emitJump(int label, int line);
    void  emitBranchZero(IRReg cond, int label, int line);
    void  emitBranchNZ(IRReg cond, int label, int line);
    void  emitReturn(IRReg value, int line);

    int registerCount() const { return nextReg; }
    // Anything after a return or jump cannot run, so lowering asks before
    // emitting a function's trailing code.
    bool endsWithTerminator() const;

private:
    int nextReg;
    int nextLabel;
    void push(const IRInstr &i) { code.push_back(i); }
};

struct IRGlobal {
    std::string name;
    int size;
    IRGlobal(const std::string &n, int s) : name(n), size(s) {}
};

// Read-only text, so a char* has something to point at.
struct IRString {
    std::string name;       // str0, str1, ...
    std::string value;
    IRString(const std::string &n, const std::string &v) : name(n), value(v) {}
};

// Data, not code.  Slot indices come from Layout, which is why they mean the
// same thing in a base and everything derived from it.
struct IRVTable {
    std::string className;
    std::vector<std::string> slots;     // mangled function names
};

struct IRModule {
    std::vector<IRFunction*> functions;
    std::vector<IRGlobal> globals;
    std::vector<IRString> strings;
    std::vector<IRVTable> vtables;

    // Interned: the same text used twice is one constant.
    std::string internString(const std::string &value);

    ~IRModule();
    void print() const;

private:
    static void printInstr(const IRInstr &i);
};

// One flat namespace of symbols, so the owning class folds into the name.
// Overloading means a name is no longer a symbol.  The signature is folded in
// so that add(int,int) and add(double,double) become different symbols, which
// is the whole reason real C++ mangles names at all.
std::string mangleSignature(const std::vector<cc::VarDecl*> &params);
std::string mangleFunction(const std::string &className, const std::string &name);
std::string mangleOverload(const std::string &className, const std::string &name,
                           const std::vector<cc::VarDecl*> &params,
                           bool isConstMethod = false);
std::string mangleConstructor(const std::string &className,
                              const std::vector<cc::VarDecl*> &params);
std::string mangleDestructor(const std::string &className);
std::string mangleVTable(const std::string &className);

#endif

// ---------- Lower.h ----------
// Lower.h -- PASS 5b, LAYER 1: lowering the C layer, namespace `cc`.
//
// Walks the parts of the tree C already had and turns them into IR.  Where a
// construct might be a C++ one it asks a virtual hook, which here answers "not
// mine"; cxx::Lowering (Lower1.h) answers them.
//
// The pass turns on one distinction: ADDRESS versus VALUE.  An lvalue has an
// address and its value is a load from it; a non-lvalue has only a value.
// Assignment lowers its left side as an address and its right as a value.
// This is also what makes references disappear -- a reference variable holds
// an address, so lowering its address is a load, not a slot lookup.
//
// C++98 only.

#ifndef LOWER_H
#define LOWER_H

#include <map>
#include <string>
#include <vector>


namespace cc {

class Lowering {
public:
    Lowering(IRModule &module, const Layout &layout, Diagnostics &diag);
    virtual ~Lowering();

    // Lowers a whole translation unit.
    void lowerUnit(const std::vector<Decl*> &units);

    // The synthetic function that runs every global's initialiser.  main calls
    // it first; without it `int g = 5;` left g at zero.
    static const char *GlobalInitName;
    // Its counterpart: global objects are destroyed after main returns.
    static const char *GlobalFiniName;

protected:
    IRModule &mod;
    const Layout &layout;
    Diagnostics &diag;

    IRFunction *fn;                     // the function being built, or 0
    std::map<std::string, int> slots;   // name -> frame slot, innermost wins
    // A name declared in a block may shadow one outside it, so unwinding the
    // scope has to RESTORE the outer binding rather than erase the name.
    // Erasing it made `int x; { int x; }` an internal error.
    struct Shadowed {
        std::string name;
        int   prevSlot;         // -1 when the name was not bound before
        Type *prevType;         // 0 likewise; not owned
        Shadowed() : prevSlot(-1), prevType(0) {}
    };
    std::vector<Shadowed> scopeNames;
    std::vector<int> scopeMarks;
    // Labels to jump to for `break` and `continue`, innermost last.
    std::vector<int> breakTargets;
    std::vector<int> continueTargets;
    // How many blocks were open when each loop was entered.  A break or a
    // continue leaves every block opened since, and leaving a block runs its
    // destructors -- exactly as a return does.
    std::vector<std::size_t> breakScopeDepth;
    std::vector<std::size_t> continueScopeDepth;
    // Case labels of the switch being lowered, filled on a first pass so the
    // comparison chain can be emitted before the body.
    std::map<const CaseStmt*, int> caseLabels;
    // Innermost last.  A `return` inside nested scopes runs the destructors of
    // every one of them, walking this list.
    std::vector<CompoundStmt*> openBlocks;

    // --- declarations -------------------------------------------------
    virtual void lowerDecl(Decl *d);
    std::string symbolFor(Function *f, const std::string &className);
    void lowerFunction(Function *f, const std::string &mangled,
                       const std::string &sourceName, bool hasThis);
    virtual void emitPrologue(Function *f);     // a ctor's base call and inits
    virtual void emitEpilogue(Function *f);     // a dtor's tail

    // --- statements ---------------------------------------------------
    virtual void lowerStmt(Stmt *s);
    void lowerBlock(CompoundStmt *block);
    void lowerIf(IfStmt *s);
    void lowerDoWhile(DoWhileStmt *s);
    void lowerSwitch(SwitchStmt *s);
    void lowerWhile(WhileStmt *s);
    void lowerFor(ForStmt *s);
    virtual void lowerVarDecl(VarDecl *vd);
    // Nothing in the C layer has a destructor, so these do nothing here.
    virtual void emitScopeExit(CompoundStmt *block);
    virtual void emitAllOpenScopeExits();
    // Scope exits for the blocks opened since `depth`, innermost first.
    virtual void emitScopeExitsDownTo(std::size_t depth);

    // --- expressions --------------------------------------------------
    IRReg lowerValue(Expr *e);
    IRReg lowerAddress(Expr *e);
    IRReg lowerBinary(BinaryExpr *e);
    // a[i]: the element's address, or the call when a class overloads it.
    IRReg lowerIndexAddress(IndexExpr *e);
    // What a[i] yields, taken from the BASE rather than from the analysis:
    // Semantic decays the inner array of g[1][2] to a pointer, and lowering
    // has to know it is still an array before it decides whether to load.
    Type *elementTypeOf(IndexExpr *e);
    virtual IRReg lowerIndexOperator(IndexExpr *e);      // 0 unless overloaded
    // Emits whatever machine operation the conversion needs, or nothing when
    // the two types already agree.  Every implicit conversion the semantic
    // pass allowed becomes a real instruction here.
    IRReg convert(IRReg value, Type *from, Type *to, int line);
    // The type an expression's operands meet in, so both sides are converted
    // before the operator runs.
    static bool arithKind(Type *t, BuiltinKind &out);
    static bool isFloatType(Type *t);
    // An array's VALUE is the address of its first element -- there is no
    // load, because an array is not something a register can hold.
    static bool isArrayType(Type *t);
    // bool lives in the C++ layer, so recognising and producing it are that
    // layer's job; the C layer only needs to ask.
    virtual bool isBoolType(Type *t) { (void)t; return false; }
    static BuiltinKind commonKind(BuiltinKind a, BuiltinKind b);
    Type *literalType(BuiltinKind k);
    Type *decayType(Type *t);
    Type *cloneTypeShallow(Type *t);
    // The C++ layer's types are unknown here, so copying one is its job.
    // A NEW node the caller owns, or 0.  The C layer has no type it cannot
    // already copy, so it never has one to offer.
    virtual Type *cloneForeignType(Type *) { return 0; }
    std::vector<Type*> ownedDecays;
    Type *commonType(BuiltinKind k);
    // Builtin types this pass forms, one per kind, owned here.
    std::map<int, Type*> builtinCache;
    IRReg lowerUnary(UnaryExpr *e);
    IRReg lowerAssign(BinaryExpr *e);
    // ++ / -- and += share one shape: take the address ONCE, load through it,
    // compute, store back.  Evaluating the target twice would be wrong the
    // moment it has a side effect.
    IRReg lowerIncDec(UnaryExpr *e);
    // The step for ++ on a pointer is the pointee's size, not one.
    IRReg stepFor(Type *t, int line);
    IRReg lowerShortCircuit(BinaryExpr *e);
    // Collapse any scalar to 0 or 1.  A logical operand is a truth value, not
    // the operand that happened to decide the answer.
    IRReg truth(IRReg value, Type *t, int line);
    virtual IRReg lowerCall(CallExpr *e, bool wantsResult);
    std::vector<IRReg> lowerArgs(CallExpr *e, Function *target, std::size_t skip);

    // --- hooks the C++ layer answers ----------------------------------
    // false when the node is not one this layer handles -- always, here.
    virtual bool lowerLayerValue(Expr *e, IRReg &out);
    virtual bool lowerLayerAddress(Expr *e, IRReg &out);

    // --- helpers ------------------------------------------------------
    int sizeOfType(Type *t) const;
    // Lowering needs sizes, not meanings, so this is a small local recompute
    // rather than a second type checker.
    virtual Type *typeOf(Expr *e);
    void pushScope();
    void popScope();
    int declareLocal(const std::string &name, int size, bool isParam, bool isFloat = false,
                     bool isObject = false);
    void emitGlobalInit(const std::vector<Decl*> &units);
    void emitGlobalFini(const std::vector<Decl*> &units);
    virtual void destroyGlobal(VarDecl *vd);
    // One global's initialiser, given its address.  Virtual because a global
    // object is CONSTRUCTED, and only the C++ layer knows that.
    virtual void initGlobal(VarDecl *vd, IRReg addr);
    // True when a value of this type is copied whole rather than loaded into a
    // register.  Only the C++ layer has such a type.
    virtual bool isObjectType(Type *t);
    // After a byte copy the destination must be made its own class again: the
    // copy carried the source's vptr, and the source may have been a derived
    // object sliced into a base.
    virtual void reassertVPtr(Type *t, IRReg addr, int line);
    // A function returning an object cannot hand it back in a register, and
    // its own frame is gone by the time the caller could copy it.  So the
    // CALLER supplies the space: a hidden pointer parameter, right after
    // `this`, that `return` copies into.  This is what makes  V c = a + b;
    // work at all.
    static const char *ReturnSlotName;
    bool returnsObject(Function *f);
    // The ADDRESS of an object-valued expression, whether it is a name or the
    // result of a call.
    IRReg lowerObjectValue(Expr *e);
    virtual bool yieldsObject(Expr *e) const;
    // An object passed BY VALUE: the callee gets a copy, and if the class
    // wrote a copy constructor that constructor is what makes it.
    virtual IRReg lowerByValueObject(Type *want, Expr *e, int line);
    // `return obj;` -- the same copy, into the slot the caller supplied.  Here
    // it is the bytes; the C++ layer overrides it to run the constructor.
    virtual void emitReturnObject(IRReg dest, Expr *e, int line);
    // A by-value argument that a copy constructor built lives in a temporary
    // of the caller's, and dies at the end of the expression that made it.
    // Nesting works because each call destroys only what it added.
    struct ArgTemp { int slot; Type *type; };
    std::vector<ArgTemp> argTemps;
    virtual void destroyArgTempsDownTo(std::size_t mark, int line);
    // Where an object-valued expression should be BUILT, when whoever asked
    // for it already has the space -- a variable being initialised from a
    // call, say.  Set it, lower the expression, and the outermost thing that
    // needed space takes it instead of declaring a temporary; anything nested
    // inside makes its own, as usual.  IR_NoReg means "make your own".
    // Taken at the TOP of whatever lowers a call, before its receiver and its
    // arguments -- those are nested expressions, and the destination belongs
    // to the outermost one.  Taking it later let  d = (a + b) * 2  give the
    // inner addition the slot meant for the multiplication.
    IRReg objectDest;
    IRReg takeObjectDest();
    // Space for a call's object result, and the address the callee fills in.
    // `given` is a destination already claimed by the caller, or IR_NoReg.
    IRReg allocReturnSlot(Function *target, int line, IRReg given = IR_NoReg);
    int findSlot(const std::string &name) const;
    virtual bool isReferenceExpr(Expr *e);  // its slot holds an address
    // A reference binds to an object, so it is passed and stored as that
    // object's ADDRESS.  C has no references, so this is false here.
    virtual bool isReferenceType(Type *t);
    // What a reference refers to.  A store through an int& is four bytes wide,
    // not eight -- the declared type decides, and T& is not the declared type
    // of the thing in memory.  Identity in the C layer, which has no T&.
    virtual Type *referentType(Type *t);
    std::map<std::string, Type*> localTypes;
    std::map<std::string, Type*> globalTypes;
    // Declared functions by name, bodiless ones included -- lowering needs
    // their parameter types to convert arguments at the call.
    std::map<std::string, Function*> functions;
    Type *currentReturnType;    // for converting a return expression

private:
    Lowering(const Lowering &);
    Lowering &operator=(const Lowering &);
};

} // namespace cc

#endif

// ---------- Lower1.h ----------
// Lower1.h -- PASS 5b, LAYER 2: lowering the C++ layer, namespace `cxx`.
//
// Derives from cc::Lowering.  Each line below is a C++ construct written out
// in terms C already had:
//
//     a method        ->  a function whose first parameter is `this`
//     T&              ->  a pointer, with one more load on every use
//     obj.field       ->  an address plus a constant offset
//     p->method()     ->  load vptr, index by a constant slot, call it
//     new T(args)     ->  alloc(sizeof T), then call the constructor
//     delete p        ->  call the destructor, then free
//     a constructor   ->  base ctor, store vptr, member inits, body
//     a destructor    ->  body, members reversed, base dtor
//     a local dying   ->  a destructor call at every exit from its block
//
// After this pass nothing about C++ is left for a code generator to know.
//
// C++98 only.

#ifndef LOWER1_H
#define LOWER1_H

#include <string>
#include <vector>


namespace cxx {

class Lowering : public cc::Lowering {
public:
    Lowering(IRModule &module, const Layout &layout, Diagnostics &diag,
             const std::map<std::string, ClassDecl*> &classes);

    // Emits every class's vtable as module data, then the code.
    void lowerClasses();
    ~Lowering();

private:
    const std::map<std::string, ClassDecl*> &classes;
    // The class whose method is being lowered, so `this` and an unqualified
    // member name can be resolved; empty outside a method.
    std::string currentClass;

    ClassDecl *findClass(const std::string &name) const;
    // Types this pass forms itself -- `this`, a `new` expression.  They belong
    // to no AST node, so this class owns and frees them.
    std::vector<cc::Type*> ownedTypes;
    cc::Type *makePointerToClass(const std::string &className);
    cc::Type *boolType();
    cc::Type *cachedBool;
    cc::Type *cloneType(cc::Type *t);
    ClassDecl *classOfType(cc::Type *t) const;  // through one pointer or ref
    const FieldLayout *findField(const std::string &className,
                                 const std::string &member) const;
    MethodDecl *findMethod(ClassDecl *cd, const std::string &member) const;
    int vtableSlotOf(const std::string &className, MethodDecl *m) const;

    // Resolves the arrow/dot difference: p->x loads p, o.x takes o's address.
    // The one place that distinction survives.
    IRReg lowerObjectAddress(MemberAccessExpr *ma);
    IRReg loadThis(int line);   // a parameter, so a load from its slot

    // --- the hooks the C layer asks ---
    virtual bool lowerLayerValue(cc::Expr *e, IRReg &out);
    virtual bool lowerLayerAddress(cc::Expr *e, IRReg &out);
    virtual IRReg lowerCall(cc::CallExpr *e, bool wantsResult);
    virtual bool isReferenceExpr(cc::Expr *e);
    virtual bool isReferenceType(cc::Type *t);
    virtual cc::Type *referentType(cc::Type *t);
    virtual void initGlobal(cc::VarDecl *vd, IRReg addr);
    virtual void destroyGlobal(cc::VarDecl *vd);
    virtual bool isObjectType(cc::Type *t);
    virtual void reassertVPtr(cc::Type *t, IRReg addr, int line);
    bool isAddressable(cc::Expr *e) const;
    virtual bool yieldsObject(cc::Expr *e) const;
    // An array of objects: its element class, and how many there are across
    // every dimension.  0 when the type is not one.
    ClassDecl *elementClassOf(cc::Type *t, long &count) const;
    // The class's own copy constructor, if it declared one.
    MethodDecl *copyConstructorOf(ClassDecl *cd) const;
    // An overloaded operator, lowered as the method call it is.
    IRReg emitOperatorCall(cc::Function *op, cc::Expr *lhsExpr, cc::Expr *rhsExpr, int line);
    IRReg lowerOperandFor(cc::Type *want, cc::Expr *e, int line);
    virtual IRReg lowerByValueObject(cc::Type *want, cc::Expr *e, int line);
    virtual void emitReturnObject(IRReg dest, cc::Expr *e, int line);
    virtual void destroyArgTempsDownTo(std::size_t mark, int line);
    virtual IRReg lowerIndexOperator(cc::IndexExpr *e);
    // Construct or destroy `count` objects laid end to end from `base`.
    void emitArrayConstruct(ClassDecl *cd, IRReg base, long count, int elemSize, int line);
    void emitArrayDestruct(ClassDecl *cd, IRReg base, long count, int elemSize, int line);
    // An array member being copied: element i from the source's element i.
    void emitArrayCopyConstruct(ClassDecl *cd, IRReg dst, IRReg src,
                                long count, int elemSize, int line);
    virtual bool isBoolType(cc::Type *t);
    virtual void lowerDecl(cc::Decl *d);
    virtual void emitPrologue(cc::Function *f);
    virtual void emitEpilogue(cc::Function *f);
    virtual void emitScopeExit(cc::CompoundStmt *block);
    virtual void emitAllOpenScopeExits();
    virtual void emitScopeExitsDownTo(std::size_t depth);
    virtual void lowerVarDecl(cc::VarDecl *vd);     // constructs class locals
    virtual cc::Type *typeOf(cc::Expr *e);          // the C++ forms
    virtual cc::Type *cloneForeignType(cc::Type *t);

    // --- object lifetime ---
    // `chosen` is the constructor the semantic pass picked.  Lowering does not
    // pick one of its own: two constructors may take the same number of
    // arguments, and only the analysis knows the operand types.
    void emitConstruct(ClassDecl *cd, IRReg objectAddr,
                       const std::vector<cc::Expr*> &args, int line,
                       cc::Function *chosen = 0);
    void emitDestruct(ClassDecl *cd, IRReg objectAddr, int line,
                      bool concreteType = false);
    void emitVPtrStore(ClassDecl *cd, IRReg objectAddr, int line);
    // A heap array's length is a VALUE, so these are real loops rather than
    // the unrolled runs used for an array whose bound the compiler knows.
    void emitHeapArrayConstruct(ClassDecl *cd, IRReg base, IRReg count,
                                int elemSize, int line);
    void emitHeapArrayDestruct(ClassDecl *cd, IRReg base, IRReg count,
                               int elemSize, int line);
    bool classHasDestructor(ClassDecl *cd) const;
    // A field that IS an object, as opposed to a pointer or reference to one.
    ClassDecl *classOfMemberType(cc::Type *t) const;
    ClassDecl *classOfMemberType(cc::Type *t, long &count) const;
};

} // namespace cxx

#endif

// ---------- CodeGen.h ----------
// CodeGen.h -- PASS 6b, IR to bytecode.
//
// The translation is nearly mechanical, because the IR has already done the
// hard part.  A three-address instruction becomes push-push-operate-pop:
//
//     %3 = add %1, %2      ->      ldr 1 / ldr 2 / add / str 3
//
// Every virtual register gets a frame slot rather than a real register.  A
// stack machine has no registers to allocate, and spilling everything keeps
// the bytecode readable next to the IR it came from -- which is the point of
// building a VM rather than emitting assembly.
//
// This pass also lays out the static data: globals, string literals, and the
// vtables, which become arrays of function indices.
//
// C++98 only.

#ifndef CODEGEN_H
#define CODEGEN_H

#include <map>
#include <string>
#include <vector>


class CodeGen {
public:
    CodeGen(Diagnostics &diag);

    // CONSUMES the module's function bodies.  Each function's instruction list
    // is released as soon as its bytecode exists, because otherwise the IR and
    // the image are both whole in memory at the moment this pass ends -- which
    // is the peak of a compile, and the IR half of it is already dead.  What
    // survives is every function's name, shape and locals, so an index or a
    // symbol looked up afterwards still answers; only the instructions go.
    //
    // A caller that wants to SEE the IR must print it before calling this.
    // main.cpp does, and there is nothing else in the pass order that reads a
    // function body after its bytecode has been made.
    void generate(IRModule &module, Image &out);

private:
    Diagnostics &diag;

    // Symbol -> index, resolved in a first pass so a call may precede its
    // definition.
    std::map<std::string, int> functionIndex;
    std::map<std::string, NativeId> natives;
    std::map<std::string, long> staticAddress;   // globals, strings, vtables

    void collectSymbols(const IRModule &module, Image &out);
    void layoutStaticData(const IRModule &module, Image &out);
    void generateFunction(const IRFunction &fn, FuncImage &out);

    // IR labels are ids; bytecode branches are instruction offsets.
    void resolveLabels(const IRFunction &fn, FuncImage &out,
                       const std::map<vmword, int> &labelAt);

    CodeGen(const CodeGen &);
    CodeGen &operator=(const CodeGen &);
};

#endif

// ---------- VM.h ----------
// VM.h -- PASS 6c, running the bytecode.
//
// The last step, and the one that makes the rest checkable: the compiler runs
// what it produced, on the machine that built it.
//
// One flat byte memory holds static data, the frame stack and the heap, so an
// address is an address wherever it points.  Values on the operand stack are 8
// bytes and hold a vmword or a double; memory keeps each type at its declared
// width, which is why loads and stores carry a size and a signedness.
//
// C++98 only.

#ifndef VM_H
#define VM_H

#include <string>
#include <vector>


class VM {
public:
    VM();

    // Runs main and returns its result.  `ok` reports whether execution
    // finished rather than trapping.
    vmword run(const Image &image, bool &ok);

    const std::string &errorMessage() const { return error; }
    vmword stepCount() const { return steps; }

    // Why the machine stopped, when it stopped. A program that ran out of
    // steps is not a program that did something wrong: it did something
    // lawful for longer than this machine was willing to watch, and a host
    // showing it to a person will want to say so differently from the way it
    // says a null was dereferenced. Asking the VM beats matching on the text
    // of errorMessage(), which is prose and is allowed to be reworded.
    bool outOfSteps() const { return stepsExhausted; }

    // The machine's size and patience.  Set before run(); the defaults are
    // what the command line uses and what every test case assumes.  A run that
    // cannot fit -- a call stack larger than the memory holding it -- is
    // refused the way any other bad image is, with a named error rather than
    // an assertion.
    void setLimits(const MachineLimits &l) { limits = l; }
    const MachineLimits &currentLimits() const { return limits; }

private:
    // A value is 8 bytes either way; which half is live depends on the
    // instruction that produced it, exactly as in a real register file.
    union Value {
        vmword i;
        double d;
    };

    MachineLimits limits;
    bool stepsExhausted;
    std::vector<unsigned char> mem;
    std::vector<Value> stack;       // the operand stack
    std::string error;
    vmword steps;

    vmword stackBase;               // frames grow from here
    vmword stackTop;
    vmword heapBase;
    vmword heapTop;
    vmword freeList;                // singly linked, through each block's header

    struct Frame {
        int func;
        int pc;
        vmword base;                // byte offset of this frame in mem
        int regBase;                // where registers start within the frame
        bool wantsResult;
    };
    std::vector<Frame> frames;
    const Image *img;               // what is running, for the frame tables

    void trap(const std::string &msg);
    bool failed() const { return !error.empty(); }

    void push(vmword v);
    void pushD(double v);
    Value pop();

    vmword readInt(vmword addr, int size, bool isSigned);
    void writeInt(vmword addr, int size, vmword value);
    double readFloat(vmword addr, int size);
    void writeFloat(vmword addr, int size, double value);

    // `arrayCount` is -1 for plain new and the element count for new[].  It is
    // recorded in the block, so delete[] knows how many destructors to run and
    // the matching form can be required rather than assumed.
    vmword allocate(vmword bytes, vmword arrayCount);
    void release(vmword addr, bool isArray);
    // Is this the START of a block, as opposed to somewhere inside one?
    bool isBlockStart(vmword block);
    // Is it on the free list already?  Both forms of delete need to say so.
    bool isOnFreeList(vmword block);
    // How many bytes the block at `addr` can hold, without trapping when the
    // address is not a heap block at all.  Input uses it to refuse a read into
    // a buffer whose size nothing knows.
    bool heapCapacity(vmword addr, vmword &cap);
    // The same question of a local: the machine knows the layout of every
    // frame it has pushed, so an address inside one has a known amount of room
    // after it even though the array that owns it has decayed to a pointer.
    bool frameCapacity(vmword addr, vmword &cap);
    // Copy a string into the machine's memory, NUL-terminated, never writing
    // more than `cap` bytes.
    void writeCString(vmword addr, const std::string &s, vmword cap);
    // False once a read has failed: what cin.good() reports.
    bool inputGood;
    // How many elements the new[] block at `addr` holds, or 0 with a trap set.
    vmword arrayCount(vmword addr);
    // The longest the free list could legitimately be; a walk past it is
    // going round a cycle, so both walks stop instead of spinning.
    vmword freeListLimit() const;

    void callNative(NativeId id, int argc);

    VM(const VM &);
    VM &operator=(const VM &);
};

#endif

// ======================================================================
// IMPLEMENTATION
// ======================================================================

// ---------- Lexer.cpp ----------
// Lexer.cpp
//
// C++98 only.



#include <cctype>
#include <cstdlib>
#include <map>
#include <string>

// --- token spelling, for "expected X" messages -------------------------
const char *tokenName(TokenKind k) {
    switch (k) {
    case TOK_EOF:        return "end of file";
    case TOK_IDENTIFIER: return "identifier";
    case TOK_NUMBER:     return "number";
    case TOK_FLOATLIT:   return "floating literal";
    case TOK_CHARLIT:    return "character literal";
    case TOK_STRINGLIT:  return "string literal";
    case TOK_INT:        return "int";
    case TOK_CHAR:       return "char";
    case TOK_VOID:       return "void";
    case TOK_BOOL:       return "bool";
    case TOK_SHORT:      return "short";
    case TOK_LONG:       return "long";
    case TOK_SIGNED:     return "signed";
    case TOK_UNSIGNED:   return "unsigned";
    case TOK_FLOAT:      return "float";
    case TOK_DOUBLE:     return "double";
    case TOK_CONST:      return "const";
    case TOK_RETURN:     return "return";
    case TOK_IF:         return "if";
    case TOK_ELSE:       return "else";
    case TOK_WHILE:      return "while";
    case TOK_FOR:        return "for";
    case TOK_BREAK:      return "break";
    case TOK_CONTINUE:   return "continue";
    case TOK_CLASS:      return "class";
    case TOK_STRUCT:     return "struct";
    case TOK_PUBLIC:     return "public";
    case TOK_PRIVATE:    return "private";
    case TOK_PROTECTED:  return "protected";
    case TOK_VIRTUAL:    return "virtual";
    case TOK_NEW:        return "new";
    case TOK_DELETE:     return "delete";
    case TOK_THIS:       return "this";
    case TOK_TRUE:       return "true";
    case TOK_FALSE:      return "false";
    case TOK_SEMI:       return "';'";
    case TOK_LPAREN:     return "'('";
    case TOK_RPAREN:     return "')'";
    case TOK_LBRACE:     return "'{'";
    case TOK_RBRACE:     return "'}'";
    case TOK_LBRACKET:   return "'['";
    case TOK_RBRACKET:   return "']'";
    case TOK_COMMA:      return "','";
    case TOK_COLON:      return "':'";
    case TOK_COLONCOLON: return "'::'";
    case TOK_DOT:        return "'.'";
    case TOK_ELLIPSIS:   return "'...'";
    case TOK_SHL:        return "'<<'";
    case TOK_SHR:        return "'>>'";
    case TOK_HASH:       return "'#'";
    case TOK_ARROW:      return "'->'";
    case TOK_TILDE:      return "'~'";
    case TOK_PLUS:       return "'+'";
    case TOK_MINUS:      return "'-'";
    case TOK_STAR:       return "'*'";
    case TOK_SLASH:      return "'/'";
    case TOK_PERCENT:    return "'%'";
    case TOK_ASSIGN:     return "'='";
    case TOK_PLUSPLUS:   return "'++'";
    case TOK_MINUSMINUS: return "'--'";
    case TOK_PLUSEQ:     return "'+='";
    case TOK_MINUSEQ:    return "'-='";
    case TOK_STAREQ:     return "'*='";
    case TOK_SLASHEQ:    return "'/='";
    case TOK_PERCENTEQ:  return "'%='";
    case TOK_DO:         return "do";
    case TOK_SWITCH:     return "switch";
    case TOK_CASE:       return "case";
    case TOK_DEFAULT:    return "default";
    case TOK_EQ:         return "'=='";
    case TOK_NE:         return "'!='";
    case TOK_LT:         return "'<'";
    case TOK_GT:         return "'>'";
    case TOK_LE:         return "'<='";
    case TOK_GE:         return "'>='";
    case TOK_ANDAND:     return "'&&'";
    case TOK_OROR:       return "'||'";
    case TOK_NOT:        return "'!'";
    case TOK_AMP:        return "'&'";
    case TOK_QUESTION:   return "'?'";
    case TOK_PIPE:       return "'|'";
    case TOK_CARET:      return "'^'";
    case TOK_RESERVED:   return "a reserved keyword";
    case TOK_UNKNOWN:    return "unknown token";
    }
    return "token";
}

// The features this subset deliberately leaves out, each with the message the
// parser gives when it meets one.  Naming the feature and the alternative is
// the difference between a compiler that teaches and one that merely refuses.
const char *reservedWordHelp(const std::string &w) {
    if (w == "template" || w == "typename" || w == "export") {
        return "templates are not supported";
    }
    if (w == "throw" || w == "try" || w == "catch") {
        return "exceptions are not supported";
    }
    if (w == "namespace" || w == "using") {
        return "namespaces are not supported";
    }
    if (w == "operator") {
        return "operator overloading is not supported";
    }
    if (w == "static") return "'static' is not supported";
    if (w == "friend")   return "'friend' is not supported";
    if (w == "mutable")  return "'mutable' is not supported";
    if (w == "explicit") return "'explicit' is not supported";
    if (w == "inline")   return "'inline' is not supported";
    if (w == "goto")     return "'goto' is not supported";
    if (w == "sizeof")   return "'sizeof' is not supported";
    if (w == "enum")     return "'enum' is not supported";
    if (w == "union")    return "'union' is not supported";
    if (w == "typedef")  return "'typedef' is not supported";
    if (w == "static_cast" || w == "const_cast" ||
        w == "dynamic_cast" || w == "reinterpret_cast") {
        return "named casts are not supported; use (T)value";
    }
    if (w == "volatile" || w == "register" || w == "extern" || w == "auto") {
        return "storage-class keywords are not supported";
    }
    if (w == "wchar_t")  return "'wchar_t' is not supported";
    if (w == "asm")      return "assembly is not supported";
    return 0;
}

// One place for the escapes both literal forms share.
static char decodeEscape(char c) {
    switch (c) {
    case 'n':  return '\n';
    case 't':  return '\t';
    case 'r':  return '\r';
    case '0':  return '\0';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"':  return '"';
    default:   return c;
    }
}

// --- position bookkeeping ---------------------------------------------

char Lexer::get() {
    if (pos >= src.size()) return '\0';
    char c = src[pos++];
    if (c == '\n') { ++line; col = 1; }
    else           { ++col; }
    return c;
}

Lexer::Position Lexer::tell() const {
    Position p;
    p.offset = pos;
    p.line = line;
    p.col = col;
    return p;
}

void Lexer::seek(const Position &p) {
    pos = p.offset;
    line = p.line;
    col = p.col;
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        while (std::isspace(static_cast<unsigned char>(peek()))) get();
        if (peek() == '/' && peekAt(1) == '/') {
            while (peek() != '\0' && peek() != '\n') get();
            continue;
        }
        if (peek() == '/' && peekAt(1) == '*') {
            get(); get();                       // consume the opening /*
            while (peek() != '\0') {
                if (peek() == '*' && peekAt(1) == '/') {
                    get(); get();               // consume the closing */
                    break;
                }
                get();
            }
            continue;
        }
        return;
    }
}

Token Lexer::makeToken(TokenKind k, int startLine, int startCol) {
    Token t;
    t.kind = k;
    t.line = startLine;
    t.col = startCol;
    return t;
}

// --- the scanner ------------------------------------------------------

Token Lexer::nextToken() {
    skipWhitespaceAndComments();

    const int startLine = line;
    const int startCol = col;

    char c = peek();
    if (c == '\0') return makeToken(TOK_EOF, startLine, startCol);

    // identifier or keyword
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        std::string id;
        while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') id += get();
        Token tok = makeToken(TOK_IDENTIFIER, startLine, startCol);
        if      (id == "int")       tok.kind = TOK_INT;
        else if (id == "char")      tok.kind = TOK_CHAR;
        else if (id == "void")      tok.kind = TOK_VOID;
        else if (id == "bool")      tok.kind = TOK_BOOL;
        else if (id == "short")     tok.kind = TOK_SHORT;
        else if (id == "long")      tok.kind = TOK_LONG;
        else if (id == "signed")    tok.kind = TOK_SIGNED;
        else if (id == "unsigned")  tok.kind = TOK_UNSIGNED;
        else if (id == "float")     tok.kind = TOK_FLOAT;
        else if (id == "double")    tok.kind = TOK_DOUBLE;
        else if (id == "const")     tok.kind = TOK_CONST;
        else if (id == "return")    tok.kind = TOK_RETURN;
        else if (id == "if")        tok.kind = TOK_IF;
        else if (id == "else")      tok.kind = TOK_ELSE;
        else if (id == "while")     tok.kind = TOK_WHILE;
        else if (id == "for")       tok.kind = TOK_FOR;
        else if (id == "break")     tok.kind = TOK_BREAK;
        else if (id == "continue")  tok.kind = TOK_CONTINUE;
        else if (id == "do")        tok.kind = TOK_DO;
        else if (id == "switch")    tok.kind = TOK_SWITCH;
        else if (id == "case")      tok.kind = TOK_CASE;
        else if (id == "default")   tok.kind = TOK_DEFAULT;
        else if (id == "class")     tok.kind = TOK_CLASS;
        else if (id == "struct")    tok.kind = TOK_STRUCT;
        else if (id == "public")    tok.kind = TOK_PUBLIC;
        else if (id == "private")   tok.kind = TOK_PRIVATE;
        else if (id == "protected") tok.kind = TOK_PROTECTED;
        else if (id == "virtual")   tok.kind = TOK_VIRTUAL;
        else if (id == "new")       tok.kind = TOK_NEW;
        else if (id == "delete")    tok.kind = TOK_DELETE;
        else if (id == "this")      tok.kind = TOK_THIS;
        else if (id == "true")      tok.kind = TOK_TRUE;
        else if (id == "false")     tok.kind = TOK_FALSE;
        else if (reservedWordHelp(id)) { tok.kind = TOK_RESERVED; tok.text = id; }
        else                        tok.text = id;
        return tok;
    }

    // A number is integer until a '.' or an exponent proves otherwise.  The
    // '.' is only part of the number when a digit follows, so that  p.x  still
    // lexes as three tokens.
    if (std::isdigit(static_cast<unsigned char>(c))) {
        std::string num;

        // 0x... and 0... are integers in a different base.  Without this they
        // lexed as `0` followed by an identifier, and the errors that followed
        // never mentioned the number.
        if (peek() == '0' && (peekAt(1) == 'x' || peekAt(1) == 'X')) {
            num += get();                                   // '0'
            num += get();                                   // 'x'
            while (std::isxdigit(static_cast<unsigned char>(peek()))) num += get();
            Token hex = makeToken(TOK_NUMBER, startLine, startCol);
            hex.text = num;
            hex.numberValue = std::strtol(num.c_str(), 0, 16);
            return hex;
        }
        if (peek() == '0' && peekAt(1) >= '0' && peekAt(1) <= '7') {
            while (peek() >= '0' && peek() <= '7') num += get();
            Token oct = makeToken(TOK_NUMBER, startLine, startCol);
            oct.text = num;
            oct.numberValue = std::strtol(num.c_str(), 0, 8);
            return oct;
        }

        while (std::isdigit(static_cast<unsigned char>(peek()))) num += get();

        bool isFloat = false;
        if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekAt(1)))) {
            isFloat = true;
            num += get();                                   // the '.'
            while (std::isdigit(static_cast<unsigned char>(peek()))) num += get();
        }
        if (peek() == 'e' || peek() == 'E') {
            const char sign = peekAt(1);
            const bool signedExp = (sign == '+' || sign == '-');
            if (std::isdigit(static_cast<unsigned char>(signedExp ? peekAt(2) : sign))) {
                isFloat = true;
                num += get();                               // 'e'
                if (signedExp) num += get();
                while (std::isdigit(static_cast<unsigned char>(peek()))) num += get();
            }
        }

        if (isFloat) {
            Token tok = makeToken(TOK_FLOATLIT, startLine, startCol);
            tok.text = num;
            tok.floatValue = std::atof(num.c_str());
            if (peek() == 'f' || peek() == 'F') { get(); tok.isFloatSuffixed = true; }
            return tok;
        }
        Token tok = makeToken(TOK_NUMBER, startLine, startCol);
        tok.text = num;
        tok.numberValue = std::atol(num.c_str());
        return tok;
    }

    // 'A' and "text".  Escapes are shared, so they are decoded in one place.
    if (c == '\'' || c == '"') {
        const char quote = get();
        std::string body;
        bool closed = false;
        while (peek() != '\0') {
            if (peek() == quote) { get(); closed = true; break; }
            if (peek() == '\n') break;                       // unterminated
            char ch = get();
            if (ch == '\\') ch = decodeEscape(get());
            body += ch;
        }
        Token tok = makeToken(quote == '\'' ? TOK_CHARLIT : TOK_STRINGLIT,
                              startLine, startCol);
        if (!closed) {
            tok.kind = TOK_UNKNOWN;
            tok.text = "unterminated literal";
            return tok;
        }
        if (quote == '"') { tok.text = body; return tok; }
        tok.numberValue = body.empty() ? 0 : static_cast<unsigned char>(body[0]);
        tok.text = body;
        return tok;
    }

    // punctuation and operators.  Two-character forms are tested before the
    // one-character form they start with, or  ==  would lex as  = =.
    char punct = get();
    TokenKind kind = TOK_UNKNOWN;
    switch (punct) {
    case ';': kind = TOK_SEMI; break;
    case '(': kind = TOK_LPAREN; break;
    case ')': kind = TOK_RPAREN; break;
    case '{': kind = TOK_LBRACE; break;
    case '}': kind = TOK_RBRACE; break;
    case '[': kind = TOK_LBRACKET; break;
    case ']': kind = TOK_RBRACKET; break;
    case ',': kind = TOK_COMMA; break;
    case '~': kind = TOK_TILDE; break;
    case '+':
        if (peek() == '+')      { get(); kind = TOK_PLUSPLUS; }
        else if (peek() == '=') { get(); kind = TOK_PLUSEQ; }
        else kind = TOK_PLUS;
        break;
    case '*':
        if (peek() == '=') { get(); kind = TOK_STAREQ; }
        else kind = TOK_STAR;
        break;
    case '/':
        if (peek() == '=') { get(); kind = TOK_SLASHEQ; }
        else kind = TOK_SLASH;
        break;
    case '%':
        if (peek() == '=') { get(); kind = TOK_PERCENTEQ; }
        else kind = TOK_PERCENT;
        break;
    case '.':
        // '...' is one token, so a variadic parameter list can be named.
        if (peek() == '.' && peekAt(1) == '.') { get(); get(); kind = TOK_ELLIPSIS; }
        else kind = TOK_DOT;
        break;
    case '#': kind = TOK_HASH; break;
    case '-':
        if (peek() == '>')      { get(); kind = TOK_ARROW; }
        else if (peek() == '-') { get(); kind = TOK_MINUSMINUS; }
        else if (peek() == '=') { get(); kind = TOK_MINUSEQ; }
        else kind = TOK_MINUS;
        break;
    case '=':
        if (peek() == '=') { get(); kind = TOK_EQ; }
        else kind = TOK_ASSIGN;
        break;
    case '!':
        if (peek() == '=') { get(); kind = TOK_NE; }
        else kind = TOK_NOT;
        break;
    case '<':
        if (peek() == '=')      { get(); kind = TOK_LE; }
        else if (peek() == '<') { get(); kind = TOK_SHL; }
        else kind = TOK_LT;
        break;
    case '>':
        if (peek() == '=')      { get(); kind = TOK_GE; }
        else if (peek() == '>') { get(); kind = TOK_SHR; }
        else kind = TOK_GT;
        break;
    case '&':
        if (peek() == '&') { get(); kind = TOK_ANDAND; }
        else kind = TOK_AMP;
        break;
    case '|':
        if (peek() == '|') { get(); kind = TOK_OROR; }
        else kind = TOK_PIPE;
        break;
    case '?': kind = TOK_QUESTION; break;
    case '^': kind = TOK_CARET; break;
    case ':':
        if (peek() == ':') { get(); kind = TOK_COLONCOLON; }
        else kind = TOK_COLON;
        break;
    default:
        kind = TOK_UNKNOWN;
        break;
    }

    Token tok = makeToken(kind, startLine, startCol);
    if (kind == TOK_UNKNOWN) tok.text = std::string(1, punct);
    return tok;
}

// A factory, so the parsers never need the lexer's definition in their headers.
Lexer *createLexer(const std::string &s) {
    return new Lexer(s);
}

// --- object-like macros ----------------------------------------------------
//
// A #define is a textual substitution, so it happens before the first token
// exists.  Everything else about the language is unaffected -- which is the
// point: a constant should not need a rule in the grammar.

namespace {

bool identStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool identPart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Copies a string or character literal through untouched: PI inside "PI" is
// not a macro use.
void copyLiteral(const std::string &src, std::size_t &i, std::string &out) {
    const char quote = src[i];
    out += src[i++];
    while (i < src.size()) {
        if (src[i] == '\\' && i + 1 < src.size()) { out += src[i++]; out += src[i++]; continue; }
        out += src[i];
        if (src[i] == quote) { ++i; return; }
        if (src[i] == '\n') { ++i; return; }             // unterminated
        ++i;
    }
}

} // namespace

std::string expandDefines(const std::string &src, Diagnostics &diag) {
    std::map<std::string, std::string> macros;
    std::string out;
    out.reserve(src.size());

    std::size_t i = 0;
    int line = 1;
    bool blank = true;                  // nothing but spaces on this line yet

    while (i < src.size()) {
        const char c = src[i];

        if (c == '\n') { out += c; ++i; ++line; blank = true; continue; }

        // Comments and literals pass through: a macro name inside one is text.
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') out += src[i++];
            continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            out += src[i++]; out += src[i++];
            while (i < src.size() && !(src[i] == '*' && i + 1 < src.size() && src[i + 1] == '/')) {
                if (src[i] == '\n') ++line;
                out += src[i++];
            }
            if (i < src.size()) { out += src[i++]; out += src[i++]; }
            blank = false;
            continue;
        }
        if (c == '"' || c == '\'') { copyLiteral(src, i, out); blank = false; continue; }

        // A directive, but only when it opens the line.
        if (c == '#' && blank) {
            std::size_t j = i + 1;
            while (j < src.size() && (src[j] == ' ' || src[j] == '\t')) ++j;
            std::string word;
            while (j < src.size() && identPart(src[j])) word += src[j++];

            if (word != "define") {
                // #include and the rest still reach the parser, which names
                // whichever one it is.
                while (i < src.size() && src[i] != '\n') out += src[i++];
                continue;
            }

            while (j < src.size() && (src[j] == ' ' || src[j] == '\t')) ++j;
            std::string name;
            while (j < src.size() && identPart(src[j])) name += src[j++];

            if (name.empty()) {
                diag.error(line, 1, "'#define' needs a name");
            } else if (j < src.size() && src[j] == '(') {
                diag.error(line, 1, "function-like macros are not supported");
            } else {
                std::string body;
                while (j < src.size() && src[j] != '\n') body += src[j++];
                // Trim, then expand through macros already defined, so one
                // constant may be written in terms of another.
                std::size_t b = 0, e = body.size();
                while (b < e && (body[b] == ' ' || body[b] == '\t')) ++b;
                while (e > b && (body[e - 1] == ' ' || body[e - 1] == '\t' || body[e - 1] == '\r')) --e;
                body = body.substr(b, e - b);

                std::string expanded;
                std::size_t k = 0;
                while (k < body.size()) {
                    // A literal in the body is text, exactly as it is anywhere
                    // else: #define G "PI" does not mention the macro PI.
                    if (body[k] == '"' || body[k] == '\'') {
                        copyLiteral(body, k, expanded);
                        continue;
                    }
                    if (identStart(body[k])) {
                        std::string w;
                        while (k < body.size() && identPart(body[k])) w += body[k++];
                        std::map<std::string, std::string>::const_iterator m = macros.find(w);
                        expanded += (m == macros.end()) ? w : m->second;
                        continue;
                    }
                    expanded += body[k++];
                }
                macros[name] = expanded;
            }

            // The line is blanked, not deleted, so every later line keeps its
            // number and a diagnostic still points where the user is looking.
            while (i < src.size() && src[i] != '\n') ++i;
            continue;
        }

        if (identStart(c)) {
            std::string word;
            const std::size_t start = i;
            while (i < src.size() && identPart(src[i])) word += src[i++];
            std::map<std::string, std::string>::const_iterator m = macros.find(word);
            out += (m == macros.end()) ? word : m->second;
            (void)start;
            blank = false;
            continue;
        }

        if (c != ' ' && c != '\t' && c != '\r') blank = false;
        out += c;
        ++i;
    }
    return out;
}


// --- <iostream>, in the language itself --------------------------------------
//
// ostream holds nothing: every one of its operators forwards to a native and
// returns *this, which is what makes  cout << a << b  chain.  endl is an
// object of its own class, so that `cout << endl` picks an overload rather
// than needing a special rule.

namespace {

const char *IOStreamPrelude =
    "class __endl_t { public: int _; __endl_t() { _ = 0; } };"
    " __endl_t endl;"
    " class ostream {"
    " public: int _;"
    " ostream() { _ = 0; }"
    " ostream operator<<(int n)      { print_int(n); return *this; }"
    " ostream operator<<(long n)     { print_int(n); return *this; }"
    " ostream operator<<(short n)    { print_int(n); return *this; }"
    " ostream operator<<(double d)   { print_double(d); return *this; }"
    " ostream operator<<(float f)    { print_double(f); return *this; }"
    " ostream operator<<(char c)     { print_char(c); return *this; }"
    " ostream operator<<(char* s)    { print_string(s); return *this; }"
    // Any other pointer prints as an address.  Without this one the only
    // overload a pointer could reach was the bool, and `cout << p` printed 1.
    " ostream operator<<(void* p)    { print_pointer(p); return *this; }"
    " ostream operator<<(bool b)     { print_int(b); return *this; }"
    " ostream operator<<(__endl_t e) { print_line(); return *this; }"
    " };"
    " ostream cout;"
    // istream is the same shape: no state of its own, every operator forwards
    // to a native and returns *this so that  cin >> a >> b  chains.  Whether
    // the last read worked lives in the machine rather than in the object,
    // which is why a copy returned by value costs nothing -- and why
    // cin.good() is right after a chain, not just after its first read.
    " class istream {"
    " public: int _;"
    " istream() { _ = 0; }"
    // A read that fails leaves its destination alone, which is what C++98
    // says -- so every one of these asks whether the read worked before it
    // assigns.  There are no exceptions here to throw instead.
    " istream operator>>(int &n)    { long v = read_int(); if (input_good() != 0) n = (int) v; return *this; }"
    " istream operator>>(long &n)   { long v = read_int(); if (input_good() != 0) n = v; return *this; }"
    " istream operator>>(short &n)  { long v = read_int(); if (input_good() != 0) n = (short) v; return *this; }"
    " istream operator>>(double &d) { double v = read_double(); if (input_good() != 0) d = v; return *this; }"
    " istream operator>>(float &f)  { double v = read_double(); if (input_good() != 0) f = (float) v; return *this; }"
    " istream operator>>(char &c)   { int v = read_char(); if (input_good() != 0) c = (char) v; return *this; }"
    " istream operator>>(bool &b)   { long v = read_int(); if (input_good() != 0) b = v != 0; return *this; }"
    // No width, so the buffer has to say how long it is: one from new[] does,
    // and the machine asks it.  Anything else gets told to use getline rather
    // than being written past the end of.
    " istream operator>>(char* s)   { read_string(s, 0); return *this; }"
    " bool good() { return input_good() != 0; }"
    " bool eof() { return input_good() == 0; }"
    " void getline(char* s, int max) { read_line(s, max); }"
    " };"
    " istream cin;"
    " class errstream {"
    " public: int _;"
    " errstream() { _ = 0; }"
    " errstream operator<<(int n)      { err_int(n); return *this; }"
    " errstream operator<<(long n)     { err_int(n); return *this; }"
    " errstream operator<<(short n)    { err_int(n); return *this; }"
    " errstream operator<<(double d)   { err_double(d); return *this; }"
    " errstream operator<<(float f)    { err_double(f); return *this; }"
    " errstream operator<<(char c)     { err_char(c); return *this; }"
    " errstream operator<<(char* s)    { err_string(s); return *this; }"
    " errstream operator<<(void* p)    { err_pointer(p); return *this; }"
    " errstream operator<<(bool b)     { err_int(b); return *this; }"
    " errstream operator<<(__endl_t e) { err_line(); return *this; }"
    " };"
    " errstream cerr;";

// The natives the prelude leans on.  Declaring them here means a program that
// includes <iostream> need not declare them itself.
const char *IOStreamNatives =
    "void print_int(long n);"
    " void print_char(int c);"
    " void print_double(double d);"
    " void print_string(char* s);"
    " void print_line();"
    " void err_int(long n);"
    " void err_char(int c);"
    " void err_double(double d);"
    " void err_string(char* s);"
    " void err_line();"
    " void print_pointer(void* p);"
    " void err_pointer(void* p);"
    " long read_int();"
    " double read_double();"
    " int read_char();"
    " void read_string(char* s, int max);"
    " void read_line(char* s, int max);"
    " int input_good();";

// Does the source include the named header?  Only the spelling matters --
// there is no file to look for.
bool includesHeader(const std::string &src, const std::string &name) {
    // Comments and literals are skipped: `// #include <iostream>` mentions the
    // header, it does not include it, and injecting the prelude on the strength
    // of a comment puts names into the program nobody asked for.
    std::size_t i = 0;
    bool blank = true;                  // nothing but spaces on this line yet
    while (i < src.size()) {
        const char c = src[i];
        if (c == '\n') { ++i; blank = true; continue; }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) ++i;
            i = (i + 1 < src.size()) ? i + 2 : src.size();
            blank = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            const char quote = c;
            ++i;
            while (i < src.size() && src[i] != quote && src[i] != '\n') {
                if (src[i] == '\\' && i + 1 < src.size()) ++i;
                ++i;
            }
            if (i < src.size()) ++i;
            blank = false;
            continue;
        }
        if (c == '#' && blank) {
            const std::size_t eol = src.find('\n', i);
            const std::string line = src.substr(i, (eol == std::string::npos ? src.size() : eol) - i);
            if (line.find("include") != std::string::npos &&
                line.find(name) != std::string::npos) {
                return true;
            }
            i = (eol == std::string::npos) ? src.size() : eol;
            continue;
        }
        if (c != ' ' && c != '\t' && c != '\r') blank = false;
        ++i;
    }
    return false;
}

} // namespace

std::string preludeFor(const std::string &src, int &lines) {
    lines = 0;
    if (!includesHeader(src, "iostream")) return std::string();
    // One line, so every diagnostic below it shifts by exactly one.
    lines = 1;
    return std::string(IOStreamNatives) + " " + IOStreamPrelude + "\n";
}

// ---------- Diagnostics.cpp ----------
// Diagnostics.cpp
//
// C++98 only.


#include <iostream>

Diagnostics::Diagnostics(const std::string &sourceName)
    : name(sourceName), errors(0), warnings(0), capped(false), warningsCapped(false),
      lineOffset(0) {}

void Diagnostics::report(const char *level, int line, int col, const std::string &msg) {
    std::cout.flush();          // keep diagnostics in step with any AST dump
    std::cerr << name;
    const int shown = line - lineOffset;
    if (shown > 0) std::cerr << ":" << shown << ":" << col;
    std::cerr << ": " << level << ": " << msg << std::endl;
}

void Diagnostics::error(int line, int col, const std::string &msg) {
    ++errors;
    if (errors > MaxReported) {
        if (!capped) {
            capped = true;
            std::cerr << name << ": error: too many errors; stopping here" << std::endl;
        }
        return;
    }
    report("error", line, col, msg);
}

// A warning has its own budget, and the error cap does not spend it.
//
// This used to read `if (capped || warnings > MaxReported)`, so the twenty-first
// ERROR silenced every warning for the rest of the compilation -- including the
// ones already found, and including the ones about the very code the errors were
// in. Two channels, one of which could switch the other off: a program with a
// syntax error early on had its narrowing warnings disappear, and nothing said
// they had.
//
// They are counted apart and capped apart, and the notice says which ran out.
void Diagnostics::warning(int line, int col, const std::string &msg) {
    ++warnings;
    if (warnings > MaxReported) {
        if (!warningsCapped) {
            warningsCapped = true;
            std::cerr << name << ": warning: too many warnings; stopping here" << std::endl;
        }
        return;
    }
    report("warning", line, col, msg);
}

void Diagnostics::printSummary() const {
    std::cout.flush();
    if (errors == 0 && warnings == 0) {
        std::cout << "No errors." << std::endl;
        return;
    }
    std::cerr << errors << " error(s), " << warnings << " warning(s)." << std::endl;
}

// ---------- AST.cpp ----------
// AST.cpp
//
// C++98 only.


namespace cc {

// --- operator spelling ---

const char *binaryOpText(BinaryOp op) {
    switch (op) {
    case BIN_Add:    return "+";
    case BIN_Sub:    return "-";
    case BIN_Mul:    return "*";
    case BIN_Div:    return "/";
    case BIN_Mod:    return "%";
    case BIN_Assign: return "=";
    case BIN_AddAssign: return "+=";
    case BIN_SubAssign: return "-=";
    case BIN_MulAssign: return "*=";
    case BIN_DivAssign: return "/=";
    case BIN_ModAssign: return "%=";
    case BIN_EQ:     return "==";
    case BIN_NE:     return "!=";
    case BIN_LT:     return "<";
    case BIN_GT:     return ">";
    case BIN_LE:     return "<=";
    case BIN_GE:     return ">=";
    case BIN_LAnd:   return "&&";
    case BIN_LOr:    return "||";
    case BIN_Shl:    return "<<";
    case BIN_Shr:    return ">>";
    }
    return "?";
}

bool binaryOpIsComparison(BinaryOp op) {
    return op == BIN_EQ || op == BIN_NE || op == BIN_LT
        || op == BIN_GT || op == BIN_LE || op == BIN_GE;
}

bool binaryOpIsLogical(BinaryOp op) {
    return op == BIN_LAnd || op == BIN_LOr;
}

bool binaryOpIsAssignment(BinaryOp op) {
    return op == BIN_Assign || op == BIN_AddAssign || op == BIN_SubAssign
        || op == BIN_MulAssign || op == BIN_DivAssign || op == BIN_ModAssign;
}

BinaryOp binaryOpUnderlying(BinaryOp op) {
    switch (op) {
    case BIN_AddAssign: return BIN_Add;
    case BIN_SubAssign: return BIN_Sub;
    case BIN_MulAssign: return BIN_Mul;
    case BIN_DivAssign: return BIN_Div;
    case BIN_ModAssign: return BIN_Mod;
    default:            return op;
    }
}

const char *unaryOpText(UnaryOp op) {
    switch (op) {
    case UN_Neg:    return "-";
    case UN_Not:    return "!";
    case UN_Deref:  return "*";
    case UN_AddrOf: return "&";
    case UN_PreInc:  return "++ (prefix)";
    case UN_PreDec:  return "-- (prefix)";
    case UN_PostInc: return "++ (postfix)";
    case UN_PostDec: return "-- (postfix)";
    }
    return "?";
}

bool unaryOpIsIncDec(UnaryOp op) {
    return op == UN_PreInc || op == UN_PreDec
        || op == UN_PostInc || op == UN_PostDec;
}

// --- builtin types ---

const char *builtinName(BuiltinKind k) {
    switch (k) {
    case BK_Void:   return "void";
    case BK_Char:   return "char";
    case BK_SChar:  return "signed char";
    case BK_UChar:  return "unsigned char";
    case BK_Short:  return "short";
    case BK_UShort: return "unsigned short";
    case BK_Int:    return "int";
    case BK_UInt:   return "unsigned int";
    case BK_Long:   return "long";
    case BK_ULong:  return "unsigned long";
    case BK_Float:  return "float";
    case BK_Double: return "double";
    }
    return "?";
}

int builtinSize(BuiltinKind k) {
    switch (k) {
    case BK_Void:   return 0;
    case BK_Char:
    case BK_SChar:
    case BK_UChar:  return 1;
    case BK_Short:
    case BK_UShort: return 2;
    case BK_Int:
    case BK_UInt:
    case BK_Float:  return 4;
    case BK_Long:
    case BK_ULong:
    case BK_Double: return 8;
    }
    return 0;
}

int builtinRank(BuiltinKind k) {
    switch (k) {
    case BK_Void:   return -1;
    case BK_Char:
    case BK_SChar:
    case BK_UChar:  return 0;
    case BK_Short:
    case BK_UShort: return 1;
    case BK_Int:
    case BK_UInt:   return 2;
    case BK_Long:
    case BK_ULong:  return 3;
    case BK_Float:  return 4;
    case BK_Double: return 5;
    }
    return -1;
}

bool builtinIsFloating(BuiltinKind k) {
    return k == BK_Float || k == BK_Double;
}

bool builtinIsInteger(BuiltinKind k) {
    return k != BK_Void && !builtinIsFloating(k);
}

bool builtinIsArithmetic(BuiltinKind k) {
    return k != BK_Void;
}

// Plain `char` is signed in this implementation, as it is on x86 and arm64.
bool builtinIsSigned(BuiltinKind k) {
    switch (k) {
    case BK_UChar:
    case BK_UShort:
    case BK_UInt:
    case BK_ULong:  return false;
    default:        return true;
    }
}

// --- Types ---

void BuiltinType::print(int indent) {
    printIndent(indent);
    std::cout << name() << std::endl;
}

void ArrayType::print(int indent) {
    printIndent(indent);
    std::cout << "array[" << count << "] of ";
    element->print(0);
}

void PointerType::print(int indent) {
    printIndent(indent);
    std::cout << "pointer to ";
    base->print(0);
}

// --- Expressions ---

void NumberExpr::print(int indent) {
    printIndent(indent);
    std::cout << value;
    if (kind != BK_Int) std::cout << " : " << builtinName(kind);
    std::cout << std::endl;
}

void FloatExpr::print(int indent) {
    printIndent(indent);
    std::cout << value << " : " << builtinName(kind) << std::endl;
}

void StringExpr::print(int indent) {
    printIndent(indent);
    std::cout << "\"" << value << "\"" << std::endl;
}

void IdentExpr::print(int indent) {
    printIndent(indent);
    std::cout << name << std::endl;
}

void UnaryExpr::print(int indent) {
    printIndent(indent);
    std::cout << "Unary " << unaryOpText(op) << std::endl;
    if (operand) operand->print(indent + 1);
}

IndexExpr::~IndexExpr() {
    delete base;
    delete index;
}

void IndexExpr::print(int indent) {
    printIndent(indent);
    std::cout << "Index" << std::endl;
    if (base) base->print(indent + 1);
    if (index) index->print(indent + 1);
}

BinaryExpr::~BinaryExpr() {
    delete lhs;
    delete rhs;
}

void BinaryExpr::print(int indent) {
    printIndent(indent);
    std::cout << "Binary " << binaryOpText(op) << std::endl;
    if (lhs) lhs->print(indent + 1);
    if (rhs) rhs->print(indent + 1);
}

CallExpr::~CallExpr() {
    delete callee;
    for (std::size_t i = 0; i < args.size(); ++i) delete args[i];
}

void CallExpr::print(int indent) {
    printIndent(indent);
    std::cout << "Call" << std::endl;
    if (callee) callee->print(indent + 1);
    for (std::size_t i = 0; i < args.size(); ++i) {
        printIndent(indent + 1);
        std::cout << "arg " << i << ":" << std::endl;
        args[i]->print(indent + 2);
    }
}

// --- Declarations ---

VarDecl::~VarDecl() {
    delete type;
    delete init;
    for (std::size_t i = 0; i < ctorArgs.size(); ++i) delete ctorArgs[i];
}

void VarDecl::print(int indent) {
    printIndent(indent);
    std::cout << "Var " << name << " : ";
    if (type) type->print(0);
    else std::cout << "<none>" << std::endl;
    if (init) {
        printIndent(indent + 1);
        std::cout << "init:" << std::endl;
        init->print(indent + 2);
    }
    if (hasCtorArgs) {
        printIndent(indent + 1);
        std::cout << "construct with " << ctorArgs.size() << " argument(s):" << std::endl;
        for (std::size_t i = 0; i < ctorArgs.size(); ++i) ctorArgs[i]->print(indent + 2);
    }
}

Function::~Function() {
    delete retType;
    for (std::size_t i = 0; i < params.size(); ++i) delete params[i];
    delete body;
}

// Split out so cxx::MethodDecl can change the first line and reuse the rest.
void Function::printSignature(int indent) {
    printIndent(indent);
    std::cout << "Function " << name << " returns ";
    if (retType) retType->print(0);
    else std::cout << "<none>" << std::endl;
}

void Function::printBodyPrefix(int) {
}

void Function::print(int indent) {
    printSignature(indent);
    for (std::size_t i = 0; i < params.size(); ++i) {
        printIndent(indent + 1);
        std::cout << "param:" << std::endl;
        params[i]->print(indent + 2);
    }
    printBodyPrefix(indent + 1);        // virtual: a constructor's init list
    if (body) body->print(indent + 1);
}

// --- Statements ---

CompoundStmt::~CompoundStmt() {
    for (std::size_t i = 0; i < body.size(); ++i) delete body[i];
}

void CompoundStmt::print(int indent) {
    printIndent(indent);
    std::cout << "Block" << std::endl;
    for (std::size_t i = 0; i < body.size(); ++i) body[i]->print(indent + 1);
    for (std::size_t i = 0; i < destroyAtExit.size(); ++i) {
        printIndent(indent + 1);
        std::cout << "[on exit: destroy " << destroyAtExit[i]->name << "]" << std::endl;
    }
}

void DeclStmt::print(int indent) {
    if (var) var->print(indent);
}

void ExprStmt::print(int indent) {
    printIndent(indent);
    std::cout << "ExprStmt" << std::endl;
    if (expr) expr->print(indent + 1);
}

void ReturnStmt::print(int indent) {
    printIndent(indent);
    std::cout << "Return" << std::endl;
    if (expr) expr->print(indent + 1);
}

IfStmt::~IfStmt() {
    delete cond;
    delete thenBranch;
    delete elseBranch;
}

void IfStmt::print(int indent) {
    printIndent(indent);
    std::cout << "If" << std::endl;
    if (cond) cond->print(indent + 1);
    printIndent(indent);
    std::cout << "then:" << std::endl;
    if (thenBranch) thenBranch->print(indent + 1);
    if (elseBranch) {
        printIndent(indent);
        std::cout << "else:" << std::endl;
        elseBranch->print(indent + 1);
    }
}

CastExpr::~CastExpr() {
    delete type;
    delete expr;
}

void CastExpr::print(int indent) {
    printIndent(indent);
    std::cout << "Cast to ";
    if (type) type->print(0);
    else std::cout << "<none>" << std::endl;
    if (expr) expr->print(indent + 1);
}

DoWhileStmt::~DoWhileStmt() {
    delete body;
    delete cond;
}

void DoWhileStmt::print(int indent) {
    printIndent(indent);
    std::cout << "DoWhile" << std::endl;
    if (body) body->print(indent + 1);
    if (cond) cond->print(indent + 1);
}

void CaseStmt::print(int indent) {
    printIndent(indent);
    if (isDefault) std::cout << "default:" << std::endl;
    else           std::cout << "case " << value << ":" << std::endl;
}

SwitchStmt::~SwitchStmt() {
    delete cond;
    delete body;
}

void SwitchStmt::print(int indent) {
    printIndent(indent);
    std::cout << "Switch" << std::endl;
    if (cond) cond->print(indent + 1);
    if (body) body->print(indent + 1);
}

WhileStmt::~WhileStmt() {
    delete cond;
    delete body;
}

void WhileStmt::print(int indent) {
    printIndent(indent);
    std::cout << "While" << std::endl;
    if (cond) cond->print(indent + 1);
    if (body) body->print(indent + 1);
}

ForStmt::~ForStmt() {
    delete init;
    delete cond;
    delete step;
    delete body;
}

void ForStmt::print(int indent) {
    printIndent(indent);
    std::cout << "For" << std::endl;
    if (init) init->print(indent + 1);
    if (cond) cond->print(indent + 1);
    if (step) step->print(indent + 1);
    if (body) body->print(indent + 1);
}

void BreakStmt::print(int indent) {
    printIndent(indent);
    std::cout << "Break" << std::endl;
}

void ContinueStmt::print(int indent) {
    printIndent(indent);
    std::cout << "Continue" << std::endl;
}

} // namespace cc

// ---------- AST1.cpp ----------
// AST1.cpp
//
// C++98 only.  See AST1.h for the inheritance layout.


namespace cxx {

const char *accessText(Access a) {
    switch (a) {
    case ACC_Public:    return "public";
    case ACC_Private:   return "private";
    case ACC_Protected: return "protected";
    }
    return "?";
}

// --- Types added by C++ ---

void ReferenceType::print(int indent) {
    printIndent(indent);
    std::cout << "reference to ";
    base->print(0);
}

void BoolType::print(int indent) {
    printIndent(indent);
    std::cout << "bool" << std::endl;
}

void ClassType::print(int indent) {
    printIndent(indent);
    std::cout << "class " << className << std::endl;
}

// --- Declarations ---

void FieldDecl::print(int indent) {
    printIndent(indent);
    std::cout << "Field " << accessText(access) << " " << name << " : ";
    if (type) type->print(0);
    else std::cout << "<none>" << std::endl;
}

MethodDecl::~MethodDecl() {
    for (std::size_t i = 0; i < memberInits.size(); ++i) {
        for (std::size_t j = 0; j < memberInits[i].args.size(); ++j) {
            delete memberInits[i].args[j];
        }
    }
}

// Everything but this first line comes from cc::Function::print().
void MethodDecl::printSignature(int indent) {
    printIndent(indent);
    const char *what = isConstructor ? "Constructor" : (isDestructor ? "Destructor" : "Method");
    std::cout << what << " " << accessText(access) << " ";
    if (isVirtual) std::cout << "virtual ";
    if (overrides) std::cout << "overriding ";
    if (!ownerClass.empty()) std::cout << ownerClass << "::";
    std::cout << name;
    if (isConstructor || isDestructor) {
        std::cout << std::endl;
    } else {
        std::cout << " returns ";
        if (retType) retType->print(0);
        else std::cout << "<none>" << std::endl;
    }
}

void MethodDecl::printBodyPrefix(int indent) {
    for (std::size_t i = 0; i < memberInits.size(); ++i) {
        printIndent(indent);
        std::cout << (memberInits[i].isBase ? "init base " : "init member ")
                  << memberInits[i].name << std::endl;
        for (std::size_t j = 0; j < memberInits[i].args.size(); ++j) {
            memberInits[i].args[j]->print(indent + 1);
        }
    }
}

ClassDecl::~ClassDecl() {
    for (std::size_t i = 0; i < members.size(); ++i) delete members[i];
    // Only the prototypes with no definition elsewhere belong to this class.
    for (std::size_t i = 0; i < friendProtos.size(); ++i) delete friendProtos[i];
}

void ClassDecl::print(int indent) {
    printIndent(indent);
    std::cout << "Class " << name;
    if (!baseName.empty()) std::cout << " : " << accessText(baseAccess) << " " << baseName;
    std::cout << std::endl;
    for (std::size_t i = 0; i < members.size(); ++i) members[i]->print(indent + 1);
}

// --- Qualified name ---

void QualifiedName::print(int indent) {
    printIndent(indent);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) std::cout << "::";
        std::cout << parts[i];
    }
    std::cout << std::endl;
}

// --- Expressions added by C++ ---

void MemberAccessExpr::print(int indent) {
    printIndent(indent);
    std::cout << "Member " << (isArrow ? "->" : ".") << member << std::endl;
    if (base) base->print(indent + 1);
}

void ThisExpr::print(int indent) {
    printIndent(indent);
    std::cout << "this" << std::endl;
}

void BoolExpr::print(int indent) {
    printIndent(indent);
    std::cout << (value ? "true" : "false") << std::endl;
}

TempExpr::~TempExpr() {
    delete type;
    for (std::size_t i = 0; i < args.size(); ++i) delete args[i];
}

void TempExpr::print(int indent) {
    printIndent(indent);
    std::cout << "Temporary ";
    if (type) type->print(0);
    else std::cout << "<none>" << std::endl;
    for (std::size_t i = 0; i < args.size(); ++i) args[i]->print(indent + 1);
}

NewExpr::~NewExpr() {
    delete allocType;
    delete count;
    for (std::size_t i = 0; i < args.size(); ++i) delete args[i];
}

void NewExpr::print(int indent) {
    printIndent(indent);
    std::cout << (count ? "New[] " : "New ");
    if (allocType) allocType->print(0);
    else std::cout << "<none>" << std::endl;
    if (count) count->print(indent + 1);
    for (std::size_t i = 0; i < args.size(); ++i) args[i]->print(indent + 1);
}

void DeleteExpr::print(int indent) {
    printIndent(indent);
    std::cout << (isArray ? "Delete[]" : "Delete") << std::endl;
    if (operand) operand->print(indent + 1);
}

} // namespace cxx

// ---------- SymbolTable.cpp ----------
//
//  SymbolTable.cpp
//  Compiler++
//
//  Created by G. R. Akhtar on 29/08/2026.
//
//  C++98 only.


Scope::~Scope() {
    for (std::map<std::string, Symbol*>::iterator it = table.begin();
         it != table.end(); ++it) {
        delete it->second;
    }
}

bool Scope::insert(const std::string &name, Symbol *sym) {
    if (table.find(name) != table.end()) return false;
    table[name] = sym;
    return true;
}

Symbol *Scope::lookup(const std::string &name) const {
    std::map<std::string, Symbol*>::const_iterator it = table.find(name);
    if (it == table.end()) return 0;
    return it->second;
}

// --- SymbolTable ---

SymbolTable::SymbolTable() {
    stack.push_back(new Scope());       // the global scope
}

SymbolTable::~SymbolTable() {
    while (!stack.empty()) {
        delete stack.back();
        stack.pop_back();
    }
}

void SymbolTable::pushScope() {
    stack.push_back(new Scope());
}

void SymbolTable::popScope() {
    if (stack.size() <= 1) return;      // never pop the global scope
    delete stack.back();
    stack.pop_back();
}

bool SymbolTable::insert(const std::string &name, Symbol *sym) {
    if (stack.empty()) return false;
    return stack.back()->insert(name, sym);
}

// Innermost scope first, then outward -- lexical scoping, literally.
Symbol *SymbolTable::lookup(const std::string &name) const {
    for (std::size_t i = stack.size(); i > 0; --i) {
        Symbol *s = stack[i - 1]->lookup(name);
        if (s) return s;
    }
    return 0;
}

Symbol *SymbolTable::lookupLocal(const std::string &name) const {
    if (stack.empty()) return 0;
    return stack.back()->lookup(name);
}

// ---------- Parser.cpp ----------
// Parser.cpp
//
// C++98 only.


#include <cstdlib>
#include <string>

namespace cc {

Parser::Parser(const std::string &s, Diagnostics &d)
    : lexer(createLexer(s)), diag(d), suppressSync(false), nesting(0), nestingReported(false),
      exprNesting(0), chainLinks(0), chainReported(false) {
    advance();
}

Parser::~Parser() {
    delete lexer;
}

void Parser::advance() {
    cur = lexer->nextToken();
}

// --- error reporting and recovery -------------------------------------

void Parser::errorAtCurrent(const std::string &msg) {
    diag.error(cur.line, cur.col, msg);
}

bool Parser::expect(TokenKind k, const char *context) {
    if (cur.kind == k) { advance(); return true; }
    errorAtCurrent(std::string("expected ") + tokenName(k) + " " + context
                   + ", found " + tokenName(cur.kind));
    return false;
}

bool Parser::match(TokenKind k) {
    if (cur.kind != k) return false;
    advance();
    return true;
}

// One message, then step over the whole construct.  A feature the subset does
// not have should cost the reader one line, not twenty.
bool Parser::skipReservedConstruct() {
    if (cur.kind != TOK_RESERVED) return false;

    // `using namespace std;` is stepped over WITHOUT a message, for the same
    // reason `std::` is accepted in an expression: this language's <iostream>
    // already puts cout, cin and endl at global scope, so the directive asks
    // for what is true and the program means the same with it or without it.
    // Every other `using` is still refused -- this is the one whose only
    // effect is the effect the language already has.
    if (cur.text == "using") {
        const State probe = save();
        advance();
        if (cur.kind == TOK_RESERVED && cur.text == "namespace") {
            advance();
            if (cur.kind == TOK_IDENTIFIER && cur.text == "std") {
                advance();
                if (cur.kind == TOK_SEMI) {
                    advance();
                    // Stepped over cleanly, so the caller must NOT resynchronise.
                    // Without this the directive returned a null declaration that
                    // read as a failed parse, and parseTranslationUnit's
                    // `if (!d) synchronize()` then skipped the token after it: a
                    // `template` on the next line was swallowed, and what was left
                    // -- `<class T>` -- was parsed as a class, so the one thing the
                    // reader needed to be told, that templates are excluded, was the
                    // one thing four cascading errors never said.
                    suppressSync = true;
                    return true;
                }
            }
        }
        restore(probe);
    }

    const char *help = reservedWordHelp(cur.text);
    errorAtCurrent(help ? help : "this keyword is not supported");
    skipConstruct();
    suppressSync = true;
    return true;
}

// `vector<int> v;` -- a template instantiation, which this version does not
// have and which nothing recognised.  The '<' was read as a comparison, so the
// type never existed and what came out was five errors about expressions, not
// one about templates, in the shape a beginner is most likely to type.
//
// It has to be a probe, because `a < b;` is an ordinary comparison and looks
// identical up to the '<'.  What separates them is what comes AFTER the
// matching '>': a name being declared.  That is the same test the class-name
// path already makes for `Widget w;`.
bool Parser::skipTemplateDeclaration() {
    if (cur.kind != TOK_IDENTIFIER) return false;
    const State probe = save();

    // Past any qualifier first: the name a user writes is `std::vector<int>`
    // far more often than `vector<int>`, and the qualifier is handled in the
    // expression parser, which this never reaches.
    while (cur.kind == TOK_IDENTIFIER) {
        advance();
        if (cur.kind != TOK_COLONCOLON) break;
        advance();
    }
    if (cur.kind != TOK_LT) { restore(probe); return false; }

    int depth = 0;
    while (cur.kind != TOK_EOF && cur.kind != TOK_SEMI &&
           cur.kind != TOK_LBRACE && cur.kind != TOK_RBRACE) {
        if (cur.kind == TOK_LT) ++depth;
        else if (cur.kind == TOK_GT) {
            --depth;
            advance();
            if (depth == 0) break;
            continue;
        }
        advance();
    }
    // A declaration names something; a comparison does not.
    const bool declaring = (depth == 0 && cur.kind == TOK_IDENTIFIER);
    restore(probe);
    if (!declaring) return false;

    errorAtCurrent("templates are not supported");
    skipConstruct();
    suppressSync = true;
    return true;
}

// One token past a '(' -- enough to tell a function pointer's declarator from
// an ordinary parenthesised expression.
bool Parser::peekIsStar() {
    const State st = save();
    advance();                          // consume '('
    const bool star = (cur.kind == TOK_STAR);
    restore(st);
    return star;
}

void Parser::skipParenGroup() {
    if (cur.kind != TOK_LPAREN) return;
    int depth = 0;
    while (cur.kind != TOK_EOF) {
        if (cur.kind == TOK_LPAREN) ++depth;
        else if (cur.kind == TOK_RPAREN) {
            --depth;
            advance();
            if (depth == 0) return;
            continue;
        }
        advance();
    }
}

void Parser::skipConstruct() {
    // try { ... } catch ( ... ) { ... }  is ONE construct, however many
    // keywords it spells, so it earns one message.
    const bool isTry = (cur.kind == TOK_RESERVED && cur.text == "try");
    int depth = 0;
    while (cur.kind != TOK_EOF) {
        if (cur.kind == TOK_LBRACE) { ++depth; advance(); continue; }
        if (cur.kind == TOK_RBRACE) {
            if (depth == 0) return;         // belongs to an enclosing block
            --depth;
            advance();
            if (depth == 0) {
                // try { ... } catch ( ... ) { ... } is one construct: keep
                // going rather than leaving `catch` to earn a second message.
                if (isTry && cur.kind == TOK_RESERVED && cur.text == "catch") {
                    advance();
                    skipParenGroup();
                    continue;
                }
                match(TOK_SEMI);            // a class or enum body may end with ;
                return;
            }
            continue;
        }
        if (cur.kind == TOK_SEMI && depth == 0) { advance(); return; }
        advance();
    }
}

// Skip to a token that plausibly begins something new.
void Parser::synchronize() {
    while (cur.kind != TOK_EOF) {
        if (cur.kind == TOK_SEMI) { advance(); return; }
        switch (cur.kind) {
        case TOK_RBRACE:
        case TOK_CLASS:
        case TOK_STRUCT:
        case TOK_IF:
        case TOK_WHILE:
        case TOK_DO:
        case TOK_SWITCH:
        case TOK_FOR:
        case TOK_RETURN:
        case TOK_INT:
        case TOK_CHAR:
        case TOK_VOID:
        case TOK_SHORT:
        case TOK_LONG:
        case TOK_SIGNED:
        case TOK_UNSIGNED:
        case TOK_FLOAT:
        case TOK_DOUBLE:
            return;
        default:
            advance();
        }
    }
}

// --- speculation ------------------------------------------------------

Parser::State Parser::save() const {
    State st;
    st.cur = cur;
    st.lexPos = lexer->tell();
    return st;
}

void Parser::restore(const State &st) {
    cur = st.cur;
    lexer->seek(st.lexPos);
}

// --- translation unit -------------------------------------------------

std::vector<Decl*> Parser::parseTranslationUnit() {
    std::vector<Decl*> units;
    while (cur.kind != TOK_EOF) {
        const std::size_t posBefore = lexer->tell().offset;
        suppressSync = false;
        Decl *d = parseDeclaration();       // virtual
        if (d) units.push_back(d);
        // Anything hoisted out of that declaration follows it.
        for (std::size_t k = 0; k < pending.size(); ++k) units.push_back(pending[k]);
        pending.clear();
        if (suppressSync) { suppressSync = false; continue; }
        // Only a FAILED parse resynchronises: one that produced a node left the
        // parser somewhere sensible even if it also reported.  The progress
        // check is the backstop that makes this loop terminate regardless.
        if (!d) synchronize();
        if (lexer->tell().offset == posBefore && cur.kind != TOK_EOF) advance();
    }
    return units;
}

Function *Parser::parseSingleFunction() {
    Type *t = parseType();
    if (!t) { errorAtCurrent("expected a return type"); return 0; }
    if (cur.kind != TOK_IDENTIFIER) {
        errorAtCurrent("expected a function name");
        delete t;
        return 0;
    }
    const std::string name = cur.text;
    advance();
    return parseFunctionRest(t, name);
}

// --- declarations -----------------------------------------------------

Decl *Parser::parseDeclaration() {
    if (skipTemplateDeclaration()) return 0;    // `vector<int> v;` at file scope
    if (skipReservedConstruct()) return 0;
    // #include is accepted and ignored.  There is no header to read -- a
    // native binds by being declared without a body -- but every C++ program a
    // student has ever seen opens with one, and stopping there helps nobody.
    // Any OTHER directive is still refused, by name.
    if (cur.kind == TOK_HASH) {
        const int line = cur.line;
        advance();
        const bool isInclude = (cur.kind == TOK_IDENTIFIER && cur.text == "include");
        if (!isInclude) {
            const std::string what = (cur.kind == TOK_IDENTIFIER)
                                   ? ("'#" + cur.text + "'")
                                   : std::string("this directive");
            errorAtCurrent(what + " is not supported");
        }
        while (cur.kind != TOK_EOF && cur.line == line) advance();
        suppressSync = true;
        return 0;
    }
    Type *t = parseType();              // virtual
    if (!t) {
        errorAtCurrent(std::string("expected a declaration, found ") + tokenName(cur.kind));
        advance();                      // guarantee progress
        return 0;
    }
    // int (*p)(int) -- a type, then a parenthesised '*'.  Nothing else in this
    // grammar looks like that, so it can be named instead of misread.
    if (cur.kind == TOK_LPAREN && peekIsStar()) {
        errorAtCurrent("function pointers are not supported");
        delete t;
        skipConstruct();
        return 0;
    }
    if (cur.kind != TOK_IDENTIFIER) {
        errorAtCurrent("expected a name in declaration");
        delete t;
        return 0;
    }
    const int line = cur.line, col = cur.col;
    const std::string name = cur.text;
    advance();

    Decl *d = 0;
    if (cur.kind == TOK_LPAREN) d = parseFunctionRest(t, name);
    else                        d = parseVarDeclTail(t, name, line, col);
    if (d) { d->line = line; d->col = col; }
    return d;
}

// '(' [ type IDENT { ',' type IDENT } ] ')' then ';' or a body.
Function *Parser::parseFunctionRest(Type *retType, const std::string &name) {
    Function *fn = new Function(retType, name);
    parseFunctionParamsAndBody(fn);
    return fn;
}

void Parser::parseFunctionParamsAndBody(Function *fn) {
    if (!expect(TOK_LPAREN, "in function declaration")) return;

    while (cur.kind != TOK_RPAREN && cur.kind != TOK_EOF) {
        if (cur.kind == TOK_ELLIPSIS) {
            errorAtCurrent("variadic functions are not supported");
            while (cur.kind != TOK_RPAREN && cur.kind != TOK_EOF) advance();
            break;
        }
        Type *pt = parseType();                     // virtual
        if (!pt) { errorAtCurrent("expected a parameter type"); break; }
        std::string pname;
        int pline = cur.line, pcol = cur.col;
        if (cur.kind == TOK_IDENTIFIER) { pname = cur.text; advance(); }
        // Each is rejected by name: a bare "expected ')'" says nothing about
        // which feature the program was reaching for.
        if (cur.kind == TOK_ASSIGN) {
            errorAtCurrent("default arguments are not supported");
            while (cur.kind != TOK_COMMA && cur.kind != TOK_RPAREN &&
                   cur.kind != TOK_EOF) advance();
        }
        if (cur.kind == TOK_LBRACKET) {
            errorAtCurrent("array parameters are not supported; "
                           "pass a pointer");
            while (cur.kind != TOK_COMMA && cur.kind != TOK_RPAREN &&
                   cur.kind != TOK_EOF) advance();
        }
        VarDecl *param = new VarDecl(pt, pname, 0);
        param->line = pline;
        param->col = pcol;
        fn->params.push_back(param);
        if (!match(TOK_COMMA)) break;
    }
    expect(TOK_RPAREN, "after parameter list");

    // A trailing const is a C++ member-function thing, so the hook takes it.
    parseFunctionTail(fn);                          // virtual: C++ adds  : x(1)

    if (match(TOK_SEMI)) return;                    // a declaration only
    if (cur.kind == TOK_LBRACE) {
        fn->body = parseBlock();
        return;
    }
    // `= 0` is the one thing that plausibly follows a signature and is not a
    // body, so it is worth its own sentence rather than a punctuation complaint.
    if (cur.kind == TOK_ASSIGN) {
        errorAtCurrent("pure virtual functions are not supported; "
                       "give the method a body");
        while (cur.kind != TOK_SEMI && cur.kind != TOK_EOF) advance();
        match(TOK_SEMI);
        return;
    }
    errorAtCurrent("expected ';' or '{' after function " + fn->name);
}

// IDENT already consumed:  [ '=' expr ] ';'
VarDecl *Parser::parseVarDeclTail(Type *type, const std::string &name,
                                  int nameLine, int nameCol) {
    type = parseArraySuffixes(type);            // int a[10]
    VarDecl *vd = new VarDecl(type, name, 0);
    vd->line = nameLine;
    vd->col = nameCol;
    parseVarInitializer(vd);            // virtual: C++ adds  (args)
    // `int a = 1, b = 2;` -- one type, several names.  The comma is where it
    // shows, so the comma is where it is named; without this the reader was
    // told a semicolon was expected, which is true and unhelpful.
    if (cur.kind == TOK_COMMA) {
        errorAtCurrent("declaring more than one variable in a statement is not "
                       "supported; write a declaration each");
        while (cur.kind != TOK_SEMI && cur.kind != TOK_EOF) advance();
    }
    expect(TOK_SEMI, ("after declaration of " + name).c_str());
    return vd;
}

Type *Parser::parseArraySuffixes(Type *element) {
    std::vector<long> dims;
    // Where the first '[' was.  A type has a position for the same reason an
    // expression does -- something later has to be able to point at it, and
    // Layout does when the array turns out not to fit in the machine.
    const int line = cur.line;
    const int col = cur.col;
    while (cur.kind == TOK_LBRACKET) {
        advance();
        long n = 0;
        if (cur.kind == TOK_NUMBER) {
            n = cur.numberValue;
            advance();
        } else {
            errorAtCurrent("an array bound must be a constant integer");
        }
        if (n <= 0) errorAtCurrent("an array must have at least one element");
        dims.push_back(n > 0 ? n : 1);
        expect(TOK_RBRACKET, "after an array bound");
    }
    // Built inside out, so the LAST bound is the innermost element count.
    for (std::size_t i = dims.size(); i > 0; --i) {
        ArrayType *at = new ArrayType(element, dims[i - 1]);
        at->line = line;
        at->col = col;
        element = at;
    }
    return element;
}

void Parser::parseVarInitializer(VarDecl *vd) {
    if (!match(TOK_ASSIGN)) return;
    // int a[3] = {1, 2, 3};  -- a brace list, which this version does not take.
    // Name it, because "expected an expression, found '{'" explains nothing.
    if (cur.kind == TOK_LBRACE) {
        errorAtCurrent("brace initialisers are not supported");
        int depth = 0;
        while (cur.kind != TOK_EOF) {
            if (cur.kind == TOK_LBRACE) ++depth;
            else if (cur.kind == TOK_RBRACE) { --depth; advance(); if (!depth) break; continue; }
            advance();
        }
        return;
    }
    vd->init = parseExpression();
}

// --- statements -------------------------------------------------------

CompoundStmt *Parser::parseBlock() {
    CompoundStmt *block = new CompoundStmt();
    block->line = cur.line;
    block->col = cur.col;
    if (tooDeep()) return block;
    if (!expect(TOK_LBRACE, "to open a block")) return block;
    ++nesting;
    while (cur.kind != TOK_RBRACE && cur.kind != TOK_EOF) {
        const std::size_t posBefore = lexer->tell().offset;
        suppressSync = false;
        Stmt *s = parseStatement();         // virtual
        if (s) block->body.push_back(s);
        if (suppressSync) { suppressSync = false; continue; }
        if (!s) synchronize();
        if (lexer->tell().offset == posBefore && cur.kind != TOK_EOF) advance();
    }
    expect(TOK_RBRACE, "to close a block");
    --nesting;
    return block;
}

// Declaration and expression share a first token -- Point p; vs p.x = 1; -- so
// the declaration rule is tried first and rewound if it fails.  parseType() is
// VIRTUAL, so this one C rule also declares the C++ layer's types.
Stmt *Parser::parseStatement() {
    // Statements nest too, and not only through blocks -- `else if` chains an
    // if-statement onto an if-statement with no block between them, so the
    // depth grows without parseCompound ever seeing it.  Twenty thousand of
    // them parsed, and then the semantic pass walked the same shape and did
    // not.  This is the same counter parseCompound uses, and every nested
    // statement is a level of it however it was reached.
    if (tooDeep()) return 0;
    ++nesting;
    Stmt *s = parseStatementImpl();
    --nesting;
    return s;
}

Stmt *Parser::parseStatementImpl() {
    if (skipReservedConstruct()) return 0;

    // A label is only useful with goto, which this subset does not have, so it
    // is reported here rather than left to fail as a stray expression.
    if (cur.kind == TOK_IDENTIFIER) {
        const State st = save();
        const std::string name = cur.text;
        advance();
        if (cur.kind == TOK_COLON) {
            errorAtCurrent("labels are not supported");
            advance();
            suppressSync = true;
            return 0;
        }
        restore(st);
        (void)name;
    }

    const int line = cur.line, col = cur.col;
    Stmt *s = 0;

    switch (cur.kind) {
    case TOK_LBRACE:   s = parseBlock(); break;
    case TOK_IF:       s = parseIf(); break;
    case TOK_WHILE:    s = parseWhile(); break;
    case TOK_DO:       s = parseDoWhile(); break;
    case TOK_SWITCH:   s = parseSwitch(); break;
    case TOK_CASE: {
        advance();
        Expr *v = parseExpression();
        // A label is a constant, and -1 is as constant as 1.
        long value = 0;
        bool constant = false;
        if (NumberExpr *n = dynamic_cast<NumberExpr*>(v)) {
            value = n->value;
            constant = true;
        } else if (UnaryExpr *u = dynamic_cast<UnaryExpr*>(v)) {
            NumberExpr *inner = dynamic_cast<NumberExpr*>(u->operand);
            if (inner && u->op == UN_Neg) {
                value = -inner->value;
                constant = true;
            }
        }
        if (!constant) errorAtCurrent("a case label needs a constant integer");
        s = new CaseStmt(value, false);
        delete v;
        expect(TOK_COLON, "after a case label");
        break;
    }
    case TOK_DEFAULT:
        advance();
        expect(TOK_COLON, "after 'default'");
        s = new CaseStmt(0, true);
        break;
    case TOK_FOR:      s = parseFor(); break;
    case TOK_RETURN:   s = parseReturn(); break;
    case TOK_BREAK:    advance(); expect(TOK_SEMI, "after break"); s = new BreakStmt(); break;
    case TOK_CONTINUE: advance(); expect(TOK_SEMI, "after continue"); s = new ContinueStmt(); break;
    case TOK_SEMI:     advance(); return 0;         // an empty statement
    default: {
        if (skipTemplateDeclaration()) return 0;
        State st = save();
        Type *t = parseType();                      // virtual
        if (t) {
            if (cur.kind == TOK_IDENTIFIER) {
                const std::string name = cur.text;
                const int nameLine = cur.line, nameCol = cur.col;
                advance();
                s = new DeclStmt(parseVarDeclTail(t, name, nameLine, nameCol));
                break;
            }
            // int (*p)(int); -- a type, then a parenthesised '*'.
            if (cur.kind == TOK_LPAREN && peekIsStar()) {
                errorAtCurrent("function pointers are not supported");
                delete t;
                skipConstruct();
                suppressSync = true;
                return 0;
            }
            delete t;                               // a type, but not a declaration
        }
        restore(st);
        s = parseExprStatement();
        break;
    }
    }

    if (s) { s->line = line; s->col = col; }
    return s;
}

Stmt *Parser::parseIf() {
    advance();                                      // consume 'if'
    expect(TOK_LPAREN, "after 'if'");
    Expr *cond = parseExpression();
    expect(TOK_RPAREN, "after if condition");
    Stmt *thenBranch = parseStatement();
    Stmt *elseBranch = 0;
    if (match(TOK_ELSE)) elseBranch = parseStatement();
    return new IfStmt(cond, thenBranch, elseBranch);
}

Stmt *Parser::parseWhile() {
    advance();                                      // consume 'while'
    expect(TOK_LPAREN, "after 'while'");
    Expr *cond = parseExpression();
    expect(TOK_RPAREN, "after while condition");
    return new WhileStmt(cond, parseStatement());
}

Stmt *Parser::parseFor() {
    advance();                                      // consume 'for'
    expect(TOK_LPAREN, "after 'for'");
    Stmt *init = 0;
    if (cur.kind != TOK_SEMI) init = parseStatement();   // consumes its own ';'
    else advance();
    Expr *cond = 0;
    if (cur.kind != TOK_SEMI) cond = parseExpression();
    expect(TOK_SEMI, "after for condition");
    Expr *step = 0;
    if (cur.kind != TOK_RPAREN) step = parseExpression();
    expect(TOK_RPAREN, "after for clauses");
    Stmt *body = parseStatement();

    // `for (T t; ...)` is `{ T t; for (; ...) ... }`.  Rewriting it here gives
    // the init declaration an ordinary block to live in, so it is destroyed on
    // every path out -- the loop ending, a break, or a return -- without any
    // rule in lowering having to know about for-init.
    if (dynamic_cast<DeclStmt*>(init)) {
        CompoundStmt *wrap = new CompoundStmt();
        wrap->line = init->line;
        wrap->col = init->col;
        wrap->body.push_back(init);
        wrap->body.push_back(new ForStmt(0, cond, step, body));
        return wrap;
    }
    return new ForStmt(init, cond, step, body);
}

Stmt *Parser::parseDoWhile() {
    advance();                                      // consume 'do'
    Stmt *body = parseStatement();
    expect(TOK_WHILE, "after a do body");
    expect(TOK_LPAREN, "after 'while'");
    Expr *cond = parseExpression();
    expect(TOK_RPAREN, "after a do-while condition");
    expect(TOK_SEMI, "after do-while");
    return new DoWhileStmt(body, cond);
}

Stmt *Parser::parseSwitch() {
    advance();                                      // consume 'switch'
    expect(TOK_LPAREN, "after 'switch'");
    Expr *cond = parseExpression();
    expect(TOK_RPAREN, "after a switch subject");
    if (cur.kind != TOK_LBRACE) {
        errorAtCurrent("expected '{' after switch");
        return new SwitchStmt(cond, new CompoundStmt());
    }
    return new SwitchStmt(cond, parseBlock());
}

Stmt *Parser::parseReturn() {
    advance();                                      // consume 'return'
    Expr *e = 0;
    if (cur.kind != TOK_SEMI) e = parseExpression();
    expect(TOK_SEMI, "after return");
    return new ReturnStmt(e);
}

Stmt *Parser::parseExprStatement() {
    Expr *e = parseExpression();
    if (!e) { advance(); return 0; }                // guarantee progress
    expect(TOK_SEMI, "after expression statement");
    return new ExprStmt(e);
}

// --- the type grammar -------------------------------------------------

Type *Parser::parsePointerSuffixes(Type *base) {
    while (cur.kind == TOK_STAR) {
        advance();
        base = new PointerType(base);
        // `char* const p` -- the const after the star belongs to the POINTER,
        // which may then not be moved.  `const char* p` put it on the value
        // instead, and the two are different promises.
        if (match(TOK_CONST)) base->isConst = true;
    }
    return base;
}

// The builtin types are C's, every one of them, so the whole specifier soup is
// resolved here and the C++ layer inherits it by calling this first.
//
//     [const] { signed | unsigned | short | long | int | char
//               | void | float | double }...
//
// The specifiers may appear in any order and combine, so they are collected
// into three independent facts -- signedness, length, base -- and resolved once
// at the end.  Nothing is consumed unless it is a specifier, which is what lets
// parseStatement() call this speculatively.
Type *Parser::parseType() {
    bool sawConst = match(TOK_CONST);

    enum { SignNone, SignSigned, SignUnsigned } sign = SignNone;
    enum { LenNone, LenShort, LenLong } length = LenNone;
    enum { BaseNone, BaseInt, BaseChar, BaseVoid, BaseFloat, BaseDouble }
        base = BaseNone;

    const int line = cur.line, col = cur.col;
    bool sawAny = false;
    bool bad = false;
    bool alreadyReported = false;       // a specific message beats the generic one

    for (;;) {
        switch (cur.kind) {
        case TOK_SIGNED:   if (sign != SignNone) bad = true; sign = SignSigned; break;
        case TOK_UNSIGNED: if (sign != SignNone) bad = true; sign = SignUnsigned; break;
        case TOK_SHORT:    if (length != LenNone) bad = true; length = LenShort; break;
        case TOK_LONG:
            if (length == LenLong) {
                errorAtCurrent("'long long' is not supported");
                bad = true;
                alreadyReported = true;
            } else if (length != LenNone) bad = true;
            length = LenLong;
            break;
        case TOK_INT:      if (base != BaseNone) bad = true; base = BaseInt; break;
        case TOK_CHAR:     if (base != BaseNone) bad = true; base = BaseChar; break;
        case TOK_VOID:     if (base != BaseNone) bad = true; base = BaseVoid; break;
        case TOK_FLOAT:    if (base != BaseNone) bad = true; base = BaseFloat; break;
        case TOK_DOUBLE:   if (base != BaseNone) bad = true; base = BaseDouble; break;
        default:
            goto resolve;
        }
        sawAny = true;
        advance();
        if (match(TOK_CONST)) sawConst = true;      // const may trail too
    }

resolve:
    if (!sawAny) return 0;                          // not a type this layer knows

    const bool uns = (sign == SignUnsigned);
    BuiltinKind kind = BK_Int;

    if (base == BaseVoid || base == BaseFloat || base == BaseDouble) {
        // These take no length or signedness.
        if (sign != SignNone || length != LenNone) {
            errorAtCurrent(std::string("'") + (base == BaseVoid ? "void" :
                           base == BaseFloat ? "float" : "double")
                           + "' cannot be combined with signed, unsigned, short or long");
            bad = true;
            alreadyReported = true;
        }
        kind = (base == BaseVoid)  ? BK_Void
             : (base == BaseFloat) ? BK_Float
                                   : BK_Double;
    } else if (base == BaseChar) {
        if (length != LenNone) {
            errorAtCurrent("'char' cannot be combined with short or long");
            bad = true;
            alreadyReported = true;
        }
        // Plain char is a distinct type from signed char, as in C++.
        kind = (sign == SignNone) ? BK_Char : (uns ? BK_UChar : BK_SChar);
    } else {
        // int, or a length and signedness with int implied
        if (length == LenShort)     kind = uns ? BK_UShort : BK_Short;
        else if (length == LenLong) kind = uns ? BK_ULong : BK_Long;
        else                        kind = uns ? BK_UInt : BK_Int;
    }

    if (bad && !alreadyReported) errorAtCurrent("conflicting type specifiers");

    Type *t = new BuiltinType(kind);
    t->line = line;
    t->col = col;
    // The const belongs to the value, not to a pointer built on top of it:
    // `const char *s` is a pointer to const char, and s itself may be moved.
    t->isConst = sawConst;
    return parsePointerSuffixes(t);
}

// --- the precedence chain ---------------------------------------------
// Each level parses the one below and loops on its own operators, so reading
// top to bottom is reading the precedence table.

// One more link in the tree the current expression is building.
//
// Refusing to go deeper is not enough on its own: the loops above are still
// looking at the tokens, and `*` is both a dereference and a multiplication,
// so an abandoned `***...p` came straight back as a multiplication chain and
// went on growing the tree one BinaryExpr at a time.  The tokens have to stop
// being an expression, not merely stop being parsed -- so the rest of it is
// consumed here, at the point of refusal, which is the same thing the
// excluded operators do a few lines below and for the same reason.
bool Parser::chainTooDeep() {
    ++chainLinks;
    if (chainLinks <= MaxNesting) return false;
    if (!chainReported) {
        errorAtCurrent("expression nested too deeply");
        chainReported = true;
        while (cur.kind != TOK_SEMI && cur.kind != TOK_RPAREN &&
               cur.kind != TOK_RBRACE && cur.kind != TOK_COMMA &&
               cur.kind != TOK_EOF) advance();
    }
    return true;
}

bool Parser::tooDeep() {
    if (nesting < MaxNesting) return false;
    if (!nestingReported) {
        errorAtCurrent("nested too deeply");
        nestingReported = true;
    }
    return true;
}

Expr *Parser::parseExpression() {
    if (tooDeep()) return 0;
    // The count is one expression's, so it starts again at the outermost one.
    // A nested expression -- an argument, a parenthesised group -- goes on
    // counting into the same total, which is right: it is the same tree.
    const bool outermost = (exprNesting == 0);
    if (outermost) { chainLinks = 0; chainReported = false; }
    ++nesting;
    ++exprNesting;
    Expr *e = parseAssign();
    --exprNesting;
    --nesting;

    // Refusing the expression abandons it partway, and the rest of it is still
    // sitting there for whatever asked for the expression to complain about
    // next.  One mistake costs one line, so the remains go here -- the same
    // skip the excluded operators below make, for the same reason.
    if (outermost && chainReported) {
        while (cur.kind != TOK_SEMI && cur.kind != TOK_RPAREN &&
               cur.kind != TOK_RBRACE && cur.kind != TOK_EOF) advance();
        return e;
    }

    // An operator left out of the subset lands here, where a complete
    // expression has been parsed and something follows it that no rule above
    // accepts.  Naming it costs one message; leaving it produced a complaint
    // about whichever bracket happened to be expected next, which never
    // mentioned the operator the program was reaching for.  None of these can
    // be a separator -- an argument list consumes its own commas in
    // parseCallSuffix -- so seeing one here is always the operator itself.
    if (cur.kind == TOK_QUESTION) {
        errorAtCurrent("the conditional operator '?:' is not supported; "
                       "use an if statement");
        while (cur.kind != TOK_SEMI && cur.kind != TOK_RPAREN &&
               cur.kind != TOK_EOF) advance();
    } else if (cur.kind == TOK_AMP || cur.kind == TOK_PIPE || cur.kind == TOK_CARET) {
        errorAtCurrent("bitwise operators are not supported; "
                       "'<<' and '>>' are the only bit operations");
        while (cur.kind != TOK_SEMI && cur.kind != TOK_RPAREN &&
               cur.kind != TOK_EOF) advance();
    }
    return e;
}

// Loosest, and groups RIGHT: a = b = c is a = (b = c).  Whether the left side
// is assignable is the semantic pass's question, not the grammar's.
Expr *Parser::parseAssign() {
    Expr *left = parseLogicalOr();
    if (!left) return left;
    BinaryOp op;
    switch (cur.kind) {
    case TOK_ASSIGN:    op = BIN_Assign; break;
    case TOK_PLUSEQ:    op = BIN_AddAssign; break;
    case TOK_MINUSEQ:   op = BIN_SubAssign; break;
    case TOK_STAREQ:    op = BIN_MulAssign; break;
    case TOK_SLASHEQ:   op = BIN_DivAssign; break;
    case TOK_PERCENTEQ: op = BIN_ModAssign; break;
    default: return left;
    }
    const int line = cur.line, col = cur.col;
    advance();
    Expr *e = new BinaryExpr(op, left, parseAssign());
    e->line = line; e->col = col;
    return e;
}

Expr *Parser::parseLogicalOr() {
    Expr *left = parseLogicalAnd();
    while (left && cur.kind == TOK_OROR) {
        const int line = cur.line, col = cur.col;
        advance();
        Expr *e = new BinaryExpr(BIN_LOr, left, parseLogicalAnd());
        e->line = line; e->col = col;
        left = e;
    }
    return left;
}

Expr *Parser::parseLogicalAnd() {
    Expr *left = parseEquality();
    while (left && cur.kind == TOK_ANDAND) {
        const int line = cur.line, col = cur.col;
        advance();
        Expr *e = new BinaryExpr(BIN_LAnd, left, parseEquality());
        e->line = line; e->col = col;
        left = e;
    }
    return left;
}

Expr *Parser::parseEquality() {
    Expr *left = parseRelational();
    while (left && (cur.kind == TOK_EQ || cur.kind == TOK_NE)) {
        const BinaryOp op = (cur.kind == TOK_EQ) ? BIN_EQ : BIN_NE;
        const int line = cur.line, col = cur.col;
        advance();
        Expr *e = new BinaryExpr(op, left, parseRelational());
        e->line = line; e->col = col;
        left = e;
    }
    return left;
}

// Shift binds tighter than a comparison and looser than + and -, exactly as
// in C -- so  a << b + c  is  a << (b + c).
Expr *Parser::parseShift() {
    for (Expr *left = parseAddSub(); ; ) {
        BinaryOp op;
        switch (cur.kind) {
        case TOK_SHL: op = BIN_Shl; break;
        case TOK_SHR: op = BIN_Shr; break;
        default: return left;
        }
        if (!left) return left;
        const int line = cur.line, col = cur.col;
        advance();
        Expr *e = new BinaryExpr(op, left, parseAddSub());
        e->line = line; e->col = col;
        left = e;
    }
}

Expr *Parser::parseRelational() {
    for (Expr *left = parseShift(); ; ) {
        BinaryOp op;
        switch (cur.kind) {
        case TOK_LT: op = BIN_LT; break;
        case TOK_GT: op = BIN_GT; break;
        case TOK_LE: op = BIN_LE; break;
        case TOK_GE: op = BIN_GE; break;
        default: return left;
        }
        if (!left) return left;
        const int line = cur.line, col = cur.col;
        advance();
        Expr *e = new BinaryExpr(op, left, parseShift());
        e->line = line; e->col = col;
        left = e;
    }
}

Expr *Parser::parseAddSub() {
    Expr *left = parseMulDiv();
    while (left && (cur.kind == TOK_PLUS || cur.kind == TOK_MINUS)) {
        const BinaryOp op = (cur.kind == TOK_PLUS) ? BIN_Add : BIN_Sub;
        const int line = cur.line, col = cur.col;
        advance();
        Expr *e = new BinaryExpr(op, left, parseMulDiv());
        e->line = line; e->col = col;
        left = e;
    }
    return left;
}

Expr *Parser::parseMulDiv() {
    for (Expr *left = parseUnary(); ; ) {
        BinaryOp op;
        switch (cur.kind) {
        case TOK_STAR:    op = BIN_Mul; break;
        case TOK_SLASH:   op = BIN_Div; break;
        case TOK_PERCENT: op = BIN_Mod; break;
        default: return left;
        }
        if (!left) return left;
        const int line = cur.line, col = cur.col;
        advance();
        Expr *e = new BinaryExpr(op, left, parseUnary());
        e->line = line; e->col = col;
        left = e;
    }
}

Expr *Parser::parseUnary() {
    // Every link in every chain passes through here exactly once, whichever
    // loop above is building it: a binary level parses its right operand by
    // descending to this point, and a unary operator reaches it by recursion.
    // So this is the one place that can count the depth of the tree without
    // the seven precedence loops each having to remember to.
    if (chainTooDeep()) return 0;
    UnaryOp op;
    switch (cur.kind) {
    case TOK_MINUS:      op = UN_Neg; break;
    case TOK_NOT:        op = UN_Not; break;
    case TOK_STAR:       op = UN_Deref; break;
    case TOK_AMP:        op = UN_AddrOf; break;
    case TOK_PLUSPLUS:   op = UN_PreInc; break;
    case TOK_MINUSMINUS: op = UN_PreDec; break;
    default: return parsePostfix();
    }
    const int line = cur.line, col = cur.col;
    advance();
    Expr *e = new UnaryExpr(op, parseUnary());      // unary operators nest
    e->line = line; e->col = col;
    return e;
}

// Calls belong to C, member access does not -- so it comes through a virtual
// hook.  One loop handles both in any order, which is what parses p.getX().y
Expr *Parser::parsePostfix() {
    Expr *e = parsePrimary();                       // virtual
    while (e) {
        if (cur.kind == TOK_LPAREN) { e = parseCallSuffix(e); continue; }
        if (cur.kind == TOK_LBRACKET) { e = parseIndexSuffix(e); continue; }
        if (cur.kind == TOK_PLUSPLUS || cur.kind == TOK_MINUSMINUS) {
            const UnaryOp op = (cur.kind == TOK_PLUSPLUS) ? UN_PostInc : UN_PostDec;
            const int line = cur.line, col = cur.col;
            advance();
            Expr *p = new UnaryExpr(op, e);
            p->line = line; p->col = col;
            e = p;
            continue;
        }
        Expr *m = parseMemberSuffix(e);             // virtual; 0 in the C layer
        if (m) { e = m; continue; }
        break;
    }
    return e;
}

Expr *Parser::parseCallSuffix(Expr *callee) {
    const int line = cur.line, col = cur.col;
    advance();                                      // consume '('
    CallExpr *call = new CallExpr(callee);
    call->line = line;
    call->col = col;
    while (cur.kind != TOK_RPAREN && cur.kind != TOK_EOF) {
        Expr *a = parseExpression();
        if (!a) break;
        call->args.push_back(a);
        if (!match(TOK_COMMA)) break;
    }
    expect(TOK_RPAREN, "after call arguments");
    return call;
}

// a[i] means *(a + i).  Building exactly that keeps subscripting and pointer
// arithmetic one feature rather than two.
Expr *Parser::parseIndexSuffix(Expr *base) {
    const int line = cur.line, col = cur.col;
    advance();                                  // consume '['
    Expr *index = parseExpression();
    expect(TOK_RBRACKET, "after a subscript");
    if (!index) return base;
    Expr *e = new IndexExpr(base, index);
    e->line = line; e->col = col;
    return e;
}

Expr *Parser::parsePrimary() {
    const int line = cur.line, col = cur.col;

    // Every literal form is C's, so they all live in this layer.
    if (cur.kind == TOK_NUMBER || cur.kind == TOK_CHARLIT) {
        const BuiltinKind k = (cur.kind == TOK_CHARLIT) ? BK_Char : BK_Int;
        const long v = cur.numberValue;
        advance();
        Expr *e = new NumberExpr(v, k);
        e->line = line; e->col = col;
        return e;
    }
    if (cur.kind == TOK_FLOATLIT) {
        const double v = cur.floatValue;
        const BuiltinKind k = cur.isFloatSuffixed ? BK_Float : BK_Double;
        advance();
        Expr *e = new FloatExpr(v, k);
        e->line = line; e->col = col;
        return e;
    }
    if (cur.kind == TOK_STRINGLIT) {
        const std::string v = cur.text;
        advance();
        Expr *e = new StringExpr(v);
        e->line = line; e->col = col;
        return e;
    }
    if (cur.kind == TOK_IDENTIFIER) {
        const std::string n = cur.text;
        advance();
        Expr *e = new IdentExpr(n);
        e->line = line; e->col = col;
        return e;
    }
    if (cur.kind == TOK_LPAREN) return parseCastOrParen();

    if (cur.kind == TOK_RESERVED) {
        const char *help = reservedWordHelp(cur.text);
        errorAtCurrent(help ? help : "this keyword is not supported");
        advance();
        // sizeof(T) and the named casts carry an operand; stepping over it
        // keeps one message from becoming three.
        skipParenGroup();
        if (cur.kind == TOK_LT) {           // static_cast<T>(v)
            while (cur.kind != TOK_EOF && cur.kind != TOK_GT) advance();
            match(TOK_GT);
            skipParenGroup();
        }
        return 0;
    }
    errorAtCurrent(std::string("expected an expression, found ") + tokenName(cur.kind));
    return 0;
}

// '(' is ambiguous: it opens either a cast or a parenthesised expression, and
// only what follows tells them apart.  Try the type rule and rewind when it
// does not fit -- the same speculation parseStatement() uses.
Expr *Parser::parseCastOrParen() {
    const int line = cur.line, col = cur.col;
    State st = save();
    advance();                                      // consume '('
    Type *t = parseType();                          // virtual: knows C++ types
    if (t && cur.kind == TOK_RPAREN) {
        advance();
        Expr *inner = parseUnary();                 // a cast binds like a unary
        Expr *e = new CastExpr(t, inner);
        e->line = line; e->col = col;
        return e;
    }
    delete t;
    restore(st);

    advance();                                      // consume '(' again
    Expr *e = parseExpression();
    // A comma HERE is the comma operator: an argument list consumes its own
    // commas in parseCallSuffix, so this pair of brackets is grouping and
    // nothing else.
    if (cur.kind == TOK_COMMA) {
        errorAtCurrent("the comma operator is not supported; "
                       "write the two expressions as separate statements");
        while (cur.kind != TOK_RPAREN && cur.kind != TOK_SEMI &&
               cur.kind != TOK_EOF) advance();
    }
    expect(TOK_RPAREN, "after parenthesised expression");
    return e;
}

// This subset introduces member access with classes, in the layer above.
Expr *Parser::parseMemberSuffix(Expr *) {
    return 0;
}

void Parser::parseFunctionTail(Function *) {
}

} // namespace cc

// ---------- Parser1.cpp ----------
// Parser1.cpp
//
// C++98 only.  See Parser1.h for what this layer adds to cc::Parser.


#include <string>

namespace cxx {

// The base constructor creates the lexer and primes the first token.
Parser::Parser(const std::string &s, Diagnostics &d)
    : cc::Parser(s, d), classBeingParsed(0) {}

// Only the class form is new; the rest goes back to the base class.
cc::Decl *Parser::parseDeclaration() {
    if (cur.kind == TOK_CLASS || cur.kind == TOK_STRUCT) return parseClass();
    // Out-of-line definitions are tried FIRST: `Point::~Point()` must not be
    // handed to a speculative parseType(), whose qualified-name rule would
    // swallow the name and report on the '~'.
    if (Decl *d = parseOutOfLineDefinition()) return d;
    if (Decl *d = parseOperatorFunction()) return d;
    return cc::Parser::parseDeclaration();
}

// A type followed by the `operator` keyword, and nothing else in the grammar
// looks like that -- so the attempt is speculative only up to that point.
Decl *Parser::parseOperatorFunction() {
    const State st = save();
    cc::Type *ret = parseType();                // virtual: knows C++ types
    if (!ret) { restore(st); return 0; }
    if (!(cur.kind == TOK_RESERVED && cur.text == "operator")) {
        delete ret;
        restore(st);
        return 0;
    }

    const int line = cur.line, col = cur.col;
    advance();                                  // consume 'operator'
    const std::string name = operatorMemberName();
    if (name.empty()) { delete ret; skipConstruct(); suppressSync = true; return 0; }

    cc::Function *fn = new cc::Function(ret, name);
    fn->line = line;
    fn->col = col;
    parseFunctionParamsAndBody(fn);
    return fn;
}

// A qualified name at file scope means a member defined outside its class.
// Telling it from an ordinary declaration needs the type, the name and then a
// '::', so the whole attempt is speculative and rewinds if it does not fit.
Decl *Parser::parseOutOfLineDefinition() {
    State st = save();

    // The return type must be parsed WITHOUT the C++ layer's qualified-name
    // rule, or `Point::Point` would be swallowed whole as a class type.  A
    // constructor and a destructor have none at all, so its absence is not a
    // failure here.
    cc::Type *ret = 0;
    switch (cur.kind) {
    case TOK_INT: case TOK_CHAR: case TOK_VOID: case TOK_SHORT: case TOK_LONG:
    case TOK_SIGNED: case TOK_UNSIGNED: case TOK_FLOAT: case TOK_DOUBLE:
    case TOK_CONST:
        ret = cc::Parser::parseType();          // builtins and pointers only
        break;
    case TOK_IDENTIFIER: {
        // A class return type looks like  Point Point::make() : two names in a
        // row.  One name followed by '::' is the qualifier itself.
        const State probe = save();
        advance();
        const bool classReturn = (cur.kind == TOK_IDENTIFIER);
        restore(probe);
        if (classReturn) ret = parseType();
        break;
    }
    default:
        break;
    }

    if (cur.kind != TOK_IDENTIFIER) { delete ret; restore(st); return 0; }
    const std::string className = cur.text;
    const int line = cur.line, col = cur.col;
    advance();
    if (cur.kind != TOK_COLONCOLON) { delete ret; restore(st); return 0; }
    advance();

    bool isDtor = false;
    if (cur.kind == TOK_TILDE) { isDtor = true; advance(); }
    if (cur.kind != TOK_IDENTIFIER) {
        errorAtCurrent("expected a member name after '::'");
        delete ret;
        return 0;
    }
    const std::string member = cur.text;
    advance();
    if (cur.kind != TOK_LPAREN) { delete ret; restore(st); return 0; }

    // A constructor is spelled Point::Point, a destructor Point::~Point.
    const bool isCtor = (!isDtor && !ret && member == className);
    MethodDecl *md = new MethodDecl(ret, isDtor ? "~" + className : member,
                                    ACC_Public);
    md->ownerClass = className;
    md->isConstructor = isCtor;
    md->isDestructor = isDtor;
    md->line = line;
    md->col = col;
    parseFunctionParamsAndBody(md);
    if (!md->body) {
        errorAtCurrent("an out-of-line definition needs a body");
    }
    return md;
}

ClassDecl *Parser::parseClass() {
    // `struct` members default to public, `class` members to private -- the
    // only difference between the two keywords in this subset.
    const Access defaultAccess = (cur.kind == TOK_STRUCT) ? ACC_Public : ACC_Private;
    const int line = cur.line, col = cur.col;
    advance();                                  // consume 'class' or 'struct'

    if (cur.kind != TOK_IDENTIFIER) {
        errorAtCurrent("expected a class name");
        return 0;
    }
    const std::string cname = cur.text;
    advance();
    classNames.insert(cname);                   // before the body, so Node *next; works

    // `class A;` names a type without defining it.  Recording the name is the
    // whole of what a forward declaration does, and it is what lets two
    // classes point at each other.
    if (cur.kind == TOK_SEMI) {
        advance();
        suppressSync = true;
        return 0;
    }

    ClassDecl *cd = new ClassDecl(cname);
    cd->line = line;
    cd->col = col;

    // optional base clause:  : [public|private|protected] Base
    if (match(TOK_COLON)) {
        // The same default the keyword sets for members.
        cd->baseAccess = defaultAccess == ACC_Public ? ACC_Public : ACC_Private;
        if (cur.kind == TOK_PUBLIC || cur.kind == TOK_PRIVATE || cur.kind == TOK_PROTECTED) {
            cd->baseAccess = (cur.kind == TOK_PUBLIC)  ? ACC_Public
                           : (cur.kind == TOK_PRIVATE) ? ACC_Private
                                                       : ACC_Protected;
            advance();
        }
        if (cur.kind != TOK_IDENTIFIER) {
            errorAtCurrent("expected a base class name");
        } else {
            cd->baseName = cur.text;
            advance();
        }
        // A deliberate limit, so say so rather than emitting a parse error.
        if (cur.kind == TOK_COMMA) {
            errorAtCurrent("multiple inheritance is not supported");
            while (cur.kind != TOK_LBRACE && cur.kind != TOK_EOF) advance();
        }
    }

    if (!expect(TOK_LBRACE, "to open a class body")) return cd;

    ClassDecl *savedBeing = classBeingParsed;
    classBeingParsed = cd;

    Access access = defaultAccess;
    while (cur.kind != TOK_RBRACE && cur.kind != TOK_EOF) {
        // an access specifier:  public:  private:  protected:
        if (cur.kind == TOK_PUBLIC || cur.kind == TOK_PRIVATE || cur.kind == TOK_PROTECTED) {
            access = (cur.kind == TOK_PUBLIC)  ? ACC_Public
                   : (cur.kind == TOK_PRIVATE) ? ACC_Private
                                               : ACC_Protected;
            advance();
            expect(TOK_COLON, "after an access specifier");
            continue;
        }
        const std::size_t posBefore = lexer->tell().offset;
        suppressSync = false;
        Decl *m = parseMemberDecl(cname, access);
        if (suppressSync) { suppressSync = false; continue; }
        if (m) {
            cd->members.push_back(m);
            // Aliases for lookup; `members` owns them.
            MethodDecl *md = dynamic_cast<MethodDecl*>(m);
            if (md && md->isConstructor) cd->ctors.push_back(md);
            if (md && md->isDestructor) {
                if (cd->dtor) diag.error(md->line, md->col,
                                         "class '" + cname + "' already has a destructor");
                else cd->dtor = md;
            }
        }
        // Only a failed parse resynchronises; only a stalled one is forced.
        if (!m) synchronize();
        if (lexer->tell().offset == posBefore && cur.kind != TOK_EOF) advance();
    }

    classBeingParsed = savedBeing;

    expect(TOK_RBRACE, "to close a class body");
    expect(TOK_SEMI, "after a class definition");
    return cd;
}

// Both start with a type and a name, so the parameter list tells them apart.
// Two tokens of lookahead: IDENT '(' where IDENT is the class's own name.
bool Parser::looksLikeConstructor(const std::string &className) {
    if (cur.kind != TOK_IDENTIFIER || cur.text != className) return false;
    State st = save();
    advance();
    const bool yes = (cur.kind == TOK_LPAREN);
    restore(st);
    return yes;
}

Decl *Parser::parseMemberDecl(const std::string &className, Access access) {
    if (skipTemplateDeclaration()) return 0;    // `vector<int> v;` as a field
    // `friend` is a grant of access, not a member: it produces nothing here.
    if (cur.kind == TOK_RESERVED && cur.text == "friend") {
        parseFriend();
        suppressSync = true;                    // parseFriend consumed the whole thing
        return 0;
    }
    if (skipReservedConstruct()) return 0;
    // `virtual` precedes the return type and is meaningful only on a method.
    const bool sawVirtual = (cur.kind == TOK_VIRTUAL);
    if (sawVirtual) advance();

    // --- destructor:  ~ClassName ( ) ---
    if (cur.kind == TOK_TILDE) {
        const int line = cur.line, col = cur.col;
        advance();
        if (cur.kind != TOK_IDENTIFIER) {
            errorAtCurrent("expected a class name after '~'");
            return 0;
        }
        const std::string dname = cur.text;
        if (dname != className) {
            errorAtCurrent("destructor name '~" + dname + "' does not match class '"
                           + className + "'");
        }
        advance();
        // No return type: a destructor has none, so retType stays 0.
        MethodDecl *md = new MethodDecl(0, "~" + className, access);
        md->ownerClass = className;
        md->isDestructor = true;
        md->isVirtual = sawVirtual;
        md->line = line;
        md->col = col;
        parseFunctionParamsAndBody(md);
        return md;
    }

    // --- constructor:  ClassName ( ... ) ---
    if (looksLikeConstructor(className)) {
        const int line = cur.line, col = cur.col;
        advance();                              // consume the class name
        MethodDecl *md = new MethodDecl(0, className, access);
        md->ownerClass = className;
        md->isConstructor = true;
        // Recorded, not rejected: that is a rule about meaning, and reporting
        // it here would resynchronise over a member that parsed fine.
        md->isVirtual = sawVirtual;
        md->line = line;
        md->col = col;
        parseFunctionParamsAndBody(md);         // the tail hook picks up  : x(1)
        return md;
    }

    if (cur.kind == TOK_CLASS || cur.kind == TOK_STRUCT) {
        errorAtCurrent("nested classes are not supported");
        skipConstruct();
        suppressSync = true;            // the skip IS the recovery
        return 0;
    }

    cc::Type *t = parseType();                  // virtual: knows C++ types
    if (!t) {
        errorAtCurrent(std::string("expected a member declaration, found ")
                       + tokenName(cur.kind));
        advance();                              // guarantee progress
        return 0;
    }
    // `operator+` is a member whose name happens to be spelled with a keyword
    // and a symbol.  Everything after this point treats it as an ordinary
    // method, which is what makes overload resolution and dispatch work on it
    // without a rule of their own.
    std::string name;
    int line = cur.line, col = cur.col;
    if (cur.kind == TOK_RESERVED && cur.text == "operator") {
        advance();
        name = operatorMemberName();
        if (name.empty()) { delete t; skipConstruct(); suppressSync = true; return 0; }
    } else if (cur.kind != TOK_IDENTIFIER) {
        errorAtCurrent("expected a member name");
        delete t;
        skipConstruct();
        suppressSync = true;            // the skip IS the recovery
        return 0;
    } else {
        name = cur.text;
        advance();
    }

    if (cur.kind == TOK_COLON) {
        errorAtCurrent("bit-fields are not supported");
        while (cur.kind != TOK_SEMI && cur.kind != TOK_RBRACE && cur.kind != TOK_EOF) advance();
        match(TOK_SEMI);
        delete t;
        suppressSync = true;
        return 0;
    }

    if (cur.kind == TOK_LPAREN) {
        // MethodDecl IS a cc::Function, so the C layer's routine fills it.
        MethodDecl *md = new MethodDecl(t, name, access);
        md->ownerClass = className;
        md->isVirtual = sawVirtual;
        md->line = line;
        md->col = col;
        parseFunctionParamsAndBody(md);
        return md;
    }

    if (sawVirtual) {
        diag.error(line, col, "'virtual' can only be applied to a member function");
    }
    // In C the array part follows the NAME, and a field is no different.
    t = parseArraySuffixes(t);
    FieldDecl *fd = new FieldDecl(t, name, access);
    fd->ownerClass = className;
    fd->line = line;
    fd->col = col;
    // `int rows, cols;` is one rule broken once, and a field had no message
    // for it -- only "expected ';' ... found ','", which names the punctuation
    // and not the rule, and then two more lines as the rest of the list was
    // read as declarations of its own.
    // The local form has said the right thing for a while; a field says it
    // now too.  What follows is one more line -- "undeclared identifier
    // 'cols'" -- and that one stays: it is a true statement about the program
    // the compiler was given, not the parser losing its place.  Declaring the
    // skipped names to silence it would need a type clone the parser does not
    // have, and a fourth copy of that idiom is a worse trade than a second
    // line.
    if (cur.kind == TOK_COMMA) {
        errorAtCurrent("declaring more than one field in a statement is not "
                       "supported; write a declaration each");
        while (cur.kind != TOK_SEMI && cur.kind != TOK_RBRACE &&
               cur.kind != TOK_EOF) advance();
    }
    expect(TOK_SEMI, ("after field " + name).c_str());
    return fd;
}

// friend <type> <name> ( params ) ;      -- a grant, defined elsewhere
// friend <type> <name> ( params ) { ... } -- a grant AND the definition, which
//                                            is hoisted to file scope
void Parser::parseFriend() {
    const int line = cur.line, col = cur.col;
    advance();                                  // consume 'friend'

    if (cur.kind == TOK_CLASS || cur.kind == TOK_STRUCT) {
        errorAtCurrent("a friend class is not supported; name the function");
        skipConstruct();
        return;
    }

    cc::Type *ret = parseType();                // virtual: knows C++ types
    if (!ret) {
        errorAtCurrent("expected a return type after 'friend'");
        skipConstruct();
        return;
    }

    std::string name;
    if (cur.kind == TOK_RESERVED && cur.text == "operator") {
        advance();
        name = operatorMemberName();
        if (name.empty()) { delete ret; skipConstruct(); return; }
    } else if (cur.kind == TOK_IDENTIFIER) {
        name = cur.text;
        advance();
    } else {
        errorAtCurrent("expected a function name after 'friend'");
        delete ret;
        skipConstruct();
        return;
    }

    cc::Function *fn = new cc::Function(ret, name);
    fn->line = line;
    fn->col = col;
    parseFunctionParamsAndBody(fn);             // handles both ';' and '{'

    // The grant records the whole signature, so an overload of the same name
    // is a different function and is granted nothing.
    if (classBeingParsed) {
        classBeingParsed->friends.push_back(fn);
        // A body makes this the definition, and a definition belongs at file
        // scope -- which then owns it.  Without one, the prototype has nowhere
        // else to live, so the class keeps it.
        if (fn->body) pending.push_back(fn);
        else          classBeingParsed->friendProtos.push_back(fn);
    } else {
        if (fn->body) pending.push_back(fn);
        else delete fn;
    }
}

// The token(s) after `operator`, as the member's name: "operator+".  An
// operator this version does not overload is refused by name here, where the
// program is still readable, rather than misparsed further down.
std::string Parser::operatorMemberName() {
    struct Entry { TokenKind kind; const char *text; };
    static const Entry table[] = {
        { TOK_PLUS, "+" }, { TOK_MINUS, "-" }, { TOK_STAR, "*" },
        { TOK_SLASH, "/" }, { TOK_PERCENT, "%" },
        { TOK_EQ, "==" }, { TOK_NE, "!=" },
        { TOK_LT, "<" }, { TOK_GT, ">" }, { TOK_LE, "<=" }, { TOK_GE, ">=" },
        { TOK_ASSIGN, "=" },
        { TOK_PLUSEQ, "+=" }, { TOK_MINUSEQ, "-=" }, { TOK_STAREQ, "*=" },
        { TOK_SLASHEQ, "/=" }, { TOK_PERCENTEQ, "%=" },
        { TOK_SHL, "<<" }, { TOK_SHR, ">>" }
    };
    const int count = static_cast<int>(sizeof(table) / sizeof(table[0]));
    for (int i = 0; i < count; ++i) {
        if (cur.kind == table[i].kind) {
            advance();
            return std::string("operator") + table[i].text;
        }
    }
    if (cur.kind == TOK_LBRACKET) {
        advance();
        if (!expect(TOK_RBRACKET, "after 'operator['")) return std::string();
        return "operator[]";
    }
    // operator()  -- an empty pair, with the parameter list after it.
    if (cur.kind == TOK_LPAREN) {
        const State probe = save();
        advance();
        if (cur.kind == TOK_RPAREN) { advance(); return "operator()"; }
        restore(probe);
        errorAtCurrent("expected an operator after 'operator'");
        return std::string();
    }
    errorAtCurrent(std::string("this operator cannot be overloaded"));
    return std::string();
}

bool Parser::namesAClass(const std::string &n) const {
    return classNames.find(n) != classNames.end();
}

// C types plus class names and references; the builtin and pointer forms come
// from cc::Parser.
cc::Type *Parser::parseType() {
    // `const Point p;` -- the C layer's parseType would consume the const and
    // then find no specifier it knows, so it is taken here for a class type.
    const bool leadingConst = (cur.kind == TOK_CONST);

    // `const bool` -- the C layer's parseType would consume the const and then
    // find no specifier it knows, so bool claims both tokens itself.  The
    // lookahead is a probe because a lone `const` in front of anything else
    // still belongs to the branches below.
    if (leadingConst) {
        const State probe = save();
        advance();
        if (cur.kind != TOK_BOOL) restore(probe);
    }

    cc::Type *t = 0;

    // bool is C++'s, so the C layer's specifier soup knows nothing about it.
    // It joins the common path rather than returning from here: the reference
    // suffix is handled once, at the bottom, and a `return` taken early meant
    // `bool*` parsed while `bool&` did not.
    bool isBool = false;
    if (cur.kind == TOK_BOOL) {
        const int line = cur.line, col = cur.col;
        advance();
        cc::Type *b = new BoolType();
        b->line = line;
        b->col = col;
        b->isConst = leadingConst;
        t = parsePointerSuffixes(b);
        isBool = true;
    }

    if (!isBool) t = cc::Parser::parseType();   // int, char, void, and T*

    // a qualified / class name like A::B -- new in C++.  An identifier is a
    // type only when it names a class; otherwise it is somebody's variable,
    // and `(x)` is a parenthesised expression rather than a cast.
    bool isTypeName = false;
    if (!isBool && !t && cur.kind == TOK_IDENTIFIER) {
        isTypeName = namesAClass(cur.text);
        if (!isTypeName) {
            // Two names in a row is a declaration even when the first is not a
            // class we have seen: `Widget w;` deserves "unknown type", not a
            // cascade of expression errors.
            const State probe = save();
            delete parseQualifiedName();
            while (cur.kind == TOK_STAR) advance();
            if (cur.kind == TOK_AMP) advance();
            isTypeName = (cur.kind == TOK_IDENTIFIER);
            restore(probe);
        }
    }
    if (isTypeName) {
        const int line = cur.line, col = cur.col;
        QualifiedName *qn = parseQualifiedName();
        if (qn && !qn->parts.empty()) {
            const std::string last = qn->parts[qn->parts.size() - 1];
            delete qn;
            ClassType *ct = new ClassType(last);
            ct->line = line;
            ct->col = col;
            // The const belongs to the VALUE: `const Point *p` is a pointer to
            // a const Point, and p itself may still be moved.
            ct->isConst = leadingConst;
            t = parsePointerSuffixes(ct);       // the * suffixes are the C layer's
        } else {
            delete qn;
        }
    }

    if (!t) return 0;

    // reference suffix T& -- new in C++, and only one is legal
    if (cur.kind == TOK_AMP) {
        advance();
        t = new ReferenceType(t);
    }
    return t;
}

// Names are not resolved here: whether `x` is a field or the base's name is a
// question about the hierarchy, which is the semantic pass's business.
void Parser::parseFunctionTail(cc::Function *fn) {
    MethodDecl *md = dynamic_cast<MethodDecl*>(fn);

    // int get() const -- a promise about *this, so only a member may make it.
    if (cur.kind == TOK_CONST) {
        if (md && !md->isConstructor && !md->isDestructor) {
            md->isConstMethod = true;
            advance();
        } else {
            errorAtCurrent("only a member function may be const");
            advance();
        }
    }

    if (cur.kind != TOK_COLON) return;
    if (!md || !md->isConstructor) {
        errorAtCurrent("only a constructor may have an initialiser list");
        while (cur.kind != TOK_LBRACE && cur.kind != TOK_SEMI && cur.kind != TOK_EOF) advance();
        return;
    }
    advance();                                  // consume ':'

    for (;;) {
        if (cur.kind != TOK_IDENTIFIER) {
            errorAtCurrent("expected a member or base name in the initialiser list");
            break;
        }
        MemberInit init;
        init.name = cur.text;
        init.line = cur.line;
        init.col = cur.col;
        advance();
        if (!expect(TOK_LPAREN, "after an initialiser name")) break;
        while (cur.kind != TOK_RPAREN && cur.kind != TOK_EOF) {
            cc::Expr *a = parseExpression();
            if (!a) break;
            init.args.push_back(a);
            if (!match(TOK_COMMA)) break;
        }
        expect(TOK_RPAREN, "after initialiser arguments");
        md->memberInits.push_back(init);
        if (!match(TOK_COMMA)) break;
    }
}

// C++ adds direct initialisation, Point q(1, 2).
void Parser::parseVarInitializer(cc::VarDecl *vd) {
    if (cur.kind == TOK_LPAREN) {
        advance();
        vd->hasCtorArgs = true;
        while (cur.kind != TOK_RPAREN && cur.kind != TOK_EOF) {
            cc::Expr *a = parseExpression();
            if (!a) break;
            vd->ctorArgs.push_back(a);
            if (!match(TOK_COMMA)) break;
        }
        expect(TOK_RPAREN, "after constructor arguments");
        return;
    }
    cc::Parser::parseVarInitializer(vd);
}

QualifiedName *Parser::parseQualifiedName() {
    if (cur.kind != TOK_IDENTIFIER) return 0;
    QualifiedName *qn = new QualifiedName();
    qn->line = cur.line;
    qn->col = cur.col;
    qn->parts.push_back(cur.text);
    advance();
    while (cur.kind == TOK_COLONCOLON) {
        advance();
        if (cur.kind != TOK_IDENTIFIER) {
            errorAtCurrent("expected an identifier after '::'");
            return qn;
        }
        qn->parts.push_back(cur.text);
        advance();
    }
    return qn;
}

// Numbers, identifiers and parentheses are C's; these are not.
cc::Expr *Parser::parsePrimary() {
    // A namespace qualification, which this version does not have -- with one
    // exception it would be perverse not to make.
    //
    // `std::` is accepted and dropped.  This language's own <iostream> puts
    // cout, cin and endl at global scope, so `std::cout` and `cout` name the
    // same thing and the qualifier is the only difference between a program
    // someone pasted in and one that compiles.  Refusing it bought nothing:
    // the name resolves either way, and the error was a spelling complaint
    // about the most common spelling there is.
    //
    // Every other qualifier is still refused, and refused rather than dropped,
    // because `foo::bar` is not a program this compiler can be trusted to have
    // understood -- there is no foo, and quietly reading it as `bar` would be
    // answering a question nobody asked.
    while (cur.kind == TOK_IDENTIFIER && !namesAClass(cur.text)) {
        const State probe = save();
        const int line = cur.line, col = cur.col;
        const std::string qualifier = cur.text;
        advance();
        if (cur.kind != TOK_COLONCOLON) { restore(probe); break; }
        advance();                              // '::'
        if (cur.kind != TOK_IDENTIFIER) { restore(probe); break; }
        if (qualifier != "std") {
            diag.error(line, col,
                       "namespaces are not supported; write '"
                       + cur.text + "', not '" + qualifier + "::" + cur.text + "'");
        }
        // Loop, so a::b::c is named once per qualifier rather than cascading.
    }

    // ClassName ( args )  builds an unnamed object.  Only a name that IS a
    // class starts one, so an ordinary call is untouched.
    if (cur.kind == TOK_IDENTIFIER && namesAClass(cur.text)) {
        const State st = save();
        const int line = cur.line, col = cur.col;
        const std::string cname = cur.text;
        advance();
        if (cur.kind == TOK_LPAREN) {
            advance();
            TempExpr *t = new TempExpr(new ClassType(cname));
            t->line = line;
            t->col = col;
            t->type->line = line;
            t->type->col = col;
            while (cur.kind != TOK_RPAREN && cur.kind != TOK_EOF) {
                cc::Expr *a = parseExpression();
                if (!a) break;
                t->args.push_back(a);
                if (!match(TOK_COMMA)) break;
            }
            expect(TOK_RPAREN, "after the arguments of a temporary");
            return t;
        }
        restore(st);
    }

    const int line = cur.line, col = cur.col;

    if (cur.kind == TOK_TRUE || cur.kind == TOK_FALSE) {
        const bool v = (cur.kind == TOK_TRUE);
        advance();
        cc::Expr *e = new BoolExpr(v);
        e->line = line; e->col = col;
        return e;
    }

    if (cur.kind == TOK_THIS) {
        advance();
        cc::Expr *e = new ThisExpr();
        e->line = line; e->col = col;
        return e;
    }

    if (cur.kind == TOK_NEW) {
        advance();
        cc::Type *t = parseType();
        if (!t) { errorAtCurrent("expected a type after 'new'"); return 0; }
        NewExpr *ne = new NewExpr(t);
        ne->line = line; ne->col = col;
        // new T[n].  parseType() stops at the '[' -- an array bound belongs to
        // the declarator in this grammar, never to the type -- so the count is
        // parsed here and hangs off the expression.
        if (match(TOK_LBRACKET)) {
            ne->count = parseExpression();
            if (!ne->count) errorAtCurrent("expected an element count after '['");
            expect(TOK_RBRACKET, "after the element count");
        }
        if (match(TOK_LPAREN)) {                // constructor arguments
            while (cur.kind != TOK_RPAREN && cur.kind != TOK_EOF) {
                cc::Expr *a = parseExpression();
                if (!a) break;
                ne->args.push_back(a);
                if (!match(TOK_COMMA)) break;
            }
            expect(TOK_RPAREN, "after 'new' arguments");
        }
        return ne;
    }

    if (cur.kind == TOK_DELETE) {
        advance();
        bool isArray = false;
        if (match(TOK_LBRACKET)) {
            isArray = true;
            expect(TOK_RBRACKET, "after 'delete['");
        }
        DeleteExpr *de = new DeleteExpr(parseUnary());
        de->isArray = isArray;
        de->line = line; de->col = col;
        return de;
    }

    return cc::Parser::parsePrimary();
}

// The inherited parsePostfix() loop asks this every turn, which is what lets
// one loop parse p.getX().y -- alternating C and C++ suffixes.
cc::Expr *Parser::parseMemberSuffix(cc::Expr *base) {
    if (cur.kind != TOK_DOT && cur.kind != TOK_ARROW) return 0;
    const bool arrow = (cur.kind == TOK_ARROW);
    const int line = cur.line, col = cur.col;
    advance();
    if (cur.kind != TOK_IDENTIFIER) {
        errorAtCurrent("expected a member name after '.' or '->'");
        return 0;
    }
    const std::string member = cur.text;
    advance();
    cc::Expr *e = new MemberAccessExpr(base, member, arrow);
    e->line = line; e->col = col;
    return e;
}

} // namespace cxx

// ---------- Semantic.cpp ----------
//
//  Semantic.cpp
//  Compiler++
//
//  Created by G. R. Akhtar on 29/08/2026.
//
//  C++98 only.  See Semantic.h for how this pass spans both class layers.


// The native table: a bodyless declaration of one of its names is a binding
// to the machine, so the analyser has to be able to ask what it is binding to.

#include <cmath>
#include <cstddef>
#include <sstream>

SemanticAnalyzer::SemanticAnalyzer(Diagnostics &d)
    : diag(d), currentReturnType(0), currentFunction(0), currentIsCtorOrDtor(false),
      currentMethodIsConst(false), loopDepth(0),
      switchDepth(0) {}

SemanticAnalyzer::~SemanticAnalyzer() {
    for (std::size_t i = 0; i < ownedTypes.size(); ++i) delete ownedTypes[i];
}

void SemanticAnalyzer::error(cc::ASTNode *at, const std::string &msg) {
    if (at) diag.error(at->line, at->col, msg);
    else    diag.error(0, 0, msg);
}

cc::Type *SemanticAnalyzer::makeBuiltin(cc::BuiltinKind k) {
    cc::Type *t = new cc::BuiltinType(k);
    ownedTypes.push_back(t);
    return t;
}

// Can this value answer "is it true?"  Anything arithmetic, any pointer, and
// bool itself.
bool SemanticAnalyzer::isTestable(cc::Type *t) {
    if (!t) return true;                    // an earlier error already spoke
    cc::BuiltinKind k;
    if (arithmeticKind(t, k)) return true;
    return dynamic_cast<cc::PointerType*>(stripReference(decay(t))) != 0;
}

bool SemanticAnalyzer::isBoolType(cc::Type *t) {
    return dynamic_cast<cxx::BoolType*>(stripReference(t)) != 0;
}

cc::Type *SemanticAnalyzer::makeBool() {
    cc::Type *t = new cxx::BoolType();
    ownedTypes.push_back(t);
    return t;
}

bool SemanticAnalyzer::arithmeticKind(cc::Type *t, cc::BuiltinKind &out) {
    if (isBoolType(t)) { out = cc::BK_Int; return true; }
    cc::BuiltinKind k;
    if (!builtinKindOf(t, k) || !cc::builtinIsArithmetic(k)) return false;
    out = k;
    return true;
}

bool SemanticAnalyzer::builtinKindOf(cc::Type *t, cc::BuiltinKind &out) {
    cc::BuiltinType *bt = dynamic_cast<cc::BuiltinType*>(stripReference(t));
    if (!bt) return false;
    out = bt->kind;
    return true;
}

// Anything narrower than int is computed as an int.  This is why `char + char`
// has type int in C++, and why a compiler needs a rank rather than a size.
cc::BuiltinKind SemanticAnalyzer::promote(cc::BuiltinKind k) {
    if (cc::builtinIsFloating(k)) return k;
    return cc::builtinRank(k) < cc::builtinRank(cc::BK_Int) ? cc::BK_Int : k;
}

// The common type two operands meet in: floating wins over integer, then the
// higher rank wins, and at equal rank unsigned wins over signed.
cc::BuiltinKind SemanticAnalyzer::usualArithmetic(cc::BuiltinKind a, cc::BuiltinKind b) {
    if (a == cc::BK_Double || b == cc::BK_Double) return cc::BK_Double;
    if (a == cc::BK_Float  || b == cc::BK_Float)  return cc::BK_Float;
    a = promote(a);
    b = promote(b);
    if (a == b) return a;
    const int ra = cc::builtinRank(a), rb = cc::builtinRank(b);
    if (ra != rb) return (ra > rb) ? a : b;
    return cc::builtinIsSigned(a) ? b : a;      // equal rank: unsigned wins
}

// The range of an integer kind, as the smallest and largest value it holds.
static void integerRange(cc::BuiltinKind k, double &lo, double &hi) {
    const int bits = cc::builtinSize(k) * 8;
    if (cc::builtinIsSigned(k)) {
        const double half = std::pow(2.0, bits - 1);
        lo = -half;
        hi = half - 1;
    } else {
        lo = 0;
        hi = std::pow(2.0, bits) - 1;
    }
}

bool SemanticAnalyzer::literalFitsIn(cc::Expr *e, cc::BuiltinKind to) {
    // -5 is a literal too: the minus is a unary operator in the grammar, which
    // is no reason to warn that char cannot hold it.
    if (cc::UnaryExpr *u = dynamic_cast<cc::UnaryExpr*>(e)) {
        if (u->op == cc::UN_Neg) {
            if (cc::NumberExpr *nn = dynamic_cast<cc::NumberExpr*>(u->operand)) {
                if (cc::builtinIsFloating(to)) return true;
                double lo, hi;
                integerRange(to, lo, hi);
                const double v = -static_cast<double>(nn->value);
                return v >= lo && v <= hi;
            }
            if (dynamic_cast<cc::FloatExpr*>(u->operand)) {
                return literalFitsIn(u->operand, to);
            }
        }
    }
    if (cc::NumberExpr *n = dynamic_cast<cc::NumberExpr*>(e)) {
        if (cc::builtinIsFloating(to)) return true;
        double lo, hi;
        integerRange(to, lo, hi);
        const double v = static_cast<double>(n->value);
        return v >= lo && v <= hi;
    }
    if (cc::FloatExpr *f = dynamic_cast<cc::FloatExpr*>(e)) {
        if (to == cc::BK_Double) return true;
        // Exactly representable as a float?  Then the conversion loses nothing.
        if (to == cc::BK_Float) {
            return static_cast<double>(static_cast<float>(f->value)) == f->value;
        }
        return false;                       // a float literal into an integer
    }
    return false;
}

void SemanticAnalyzer::warnIfNarrowing(cc::Expr *e, cc::Type *from, cc::Type *to,
                                       cc::ASTNode *at, const std::string &what) {
    // Converting to bool is a normalisation, not a loss, and bool converts out
    // as 0 or 1, which fits everywhere.  Neither direction is narrowing.
    if (isBoolType(from) || isBoolType(to)) return;
    cc::BuiltinKind kf, kt;
    if (!builtinKindOf(from, kf) || !builtinKindOf(to, kt)) return;
    if (!cc::builtinIsArithmetic(kf) || !cc::builtinIsArithmetic(kt)) return;
    if (!isNarrowing(kf, kt)) return;
    if (e && literalFitsIn(e, kt)) return;
    diag.warning(at->line, at->col,
                 std::string("narrowing ") + describe(from) + " to " + describe(to)
                 + " in " + what);
}

// Legal, but worth saying out loud: the value may not survive the trip.
bool SemanticAnalyzer::isNarrowing(cc::BuiltinKind from, cc::BuiltinKind to) {
    if (from == to) return false;
    if (cc::builtinIsFloating(from) && !cc::builtinIsFloating(to)) return true;
    if (cc::builtinIsFloating(from) && cc::builtinIsFloating(to)) {
        return cc::builtinRank(to) < cc::builtinRank(from);
    }
    if (cc::builtinIsFloating(to)) return false;        // int -> float widens
    if (cc::builtinSize(to) < cc::builtinSize(from)) return true;
    // Same width but a different signedness reinterprets the top bit.
    if (cc::builtinSize(to) == cc::builtinSize(from) &&
        cc::builtinIsSigned(to) != cc::builtinIsSigned(from)) {
        return true;
    }
    return false;
}

// const travels with the copy: a clone that dropped it silently turned every
// const type back into a plain one.
cc::Type *SemanticAnalyzer::cloneType(cc::Type *t) {
    cc::Type *c = cloneTypeShape(t);
    if (c && t) c->isConst = t->isConst;
    return c;
}

cc::Type *SemanticAnalyzer::cloneTypeShape(cc::Type *t) {
    if (!t) return 0;
    cc::BuiltinType *bt = dynamic_cast<cc::BuiltinType*>(t);
    if (bt) return new cc::BuiltinType(bt->kind);
    if (dynamic_cast<cxx::BoolType*>(t)) return new cxx::BoolType();
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    if (ct) return new cxx::ClassType(ct->className);
    cc::PointerType *pt = dynamic_cast<cc::PointerType*>(t);
    if (pt) return new cc::PointerType(cloneType(pt->base));
    cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t);
    if (at) return new cc::ArrayType(cloneType(at->element), at->count);
    cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(t);
    if (rt) return new cxx::ReferenceType(cloneType(rt->base));
    return 0;
}

cc::Type *SemanticAnalyzer::makePointerTo(cc::Type *t) {
    cc::Type *p = new cc::PointerType(cloneType(t));
    ownedTypes.push_back(p);
    return p;
}

// --- Type helpers ---

// The one place arrays turn into pointers.  Everything that asks "what type
// does this expression have" goes through here; a declaration does not.
cc::Type *SemanticAnalyzer::decay(cc::Type *t) {
    cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(stripReference(t));
    if (!at) return t;
    cc::Type *p = new cc::PointerType(cloneType(at->element));
    ownedTypes.push_back(p);
    return p;
}

cc::Type *SemanticAnalyzer::stripReference(cc::Type *t) {
    cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(t);
    return rt ? rt->base : t;
}

std::string SemanticAnalyzer::describe(cc::Type *t) {
    if (!t) return "<none>";
    // const reads before a value and after a star, exactly as it is written:
    // `const int*` is a pointer to const, `int* const` a const pointer.
    const std::string lead = t->isConst ? "const " : "";
    if (dynamic_cast<cxx::BoolType*>(t)) return lead + "bool";
    cc::BuiltinType *bt = dynamic_cast<cc::BuiltinType*>(t);
    if (bt) return lead + bt->name();
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    if (ct) return lead + ct->className;
    cc::PointerType *pt = dynamic_cast<cc::PointerType*>(t);
    if (pt) return describe(pt->base) + "*" + (t->isConst ? " const" : "");
    cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t);
    if (at) {
        std::ostringstream ss;
        ss << describe(at->element) << "[" << at->count << "]";
        return ss.str();
    }
    cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(t);
    if (rt) return describe(rt->base) + "&";
    return "<type>";
}

std::string SemanticAnalyzer::countText(std::size_t n) {
    std::ostringstream ss;
    ss << n;
    return ss.str();
}

// How a diagnostic names a parameter.  A named one is named, which is what
// three messages here always assumed -- they interpolated the name straight in
// and read "the argument to ''" when there was none.  A parameter need not
// have one: `void print_int(int);` is a complete declaration, and it heads a
// good part of this project's own test corpus.  So an unnamed parameter is
// given the two things that DO identify it, its position and its function.
std::string SemanticAnalyzer::parameterText(cc::Function *fn, std::size_t i) {
    if (fn && i < fn->params.size() && !fn->params[i]->name.empty())
        return "parameter '" + fn->params[i]->name + "'";
    std::ostringstream ss;
    ss << "parameter " << (i + 1) << " of '" << (fn ? fn->name : std::string("?")) << "'";
    return ss.str();
}

bool SemanticAnalyzer::isVoid(cc::Type *t) {
    cc::BuiltinKind k;
    return builtinKindOf(t, k) && k == cc::BK_Void;
}

bool SemanticAnalyzer::sameDeclaredType(cc::Type *a, cc::Type *b) {
    if (!a || !b) return a == b;
    if (a->isConst != b->isConst) return false;

    cxx::ReferenceType *ra = dynamic_cast<cxx::ReferenceType*>(a);
    cxx::ReferenceType *rb = dynamic_cast<cxx::ReferenceType*>(b);
    if (ra || rb) return ra && rb && sameDeclaredType(ra->base, rb->base);

    cc::PointerType *pa = dynamic_cast<cc::PointerType*>(a);
    cc::PointerType *pb = dynamic_cast<cc::PointerType*>(b);
    if (pa || pb) return pa && pb && sameDeclaredType(pa->base, pb->base);

    cc::ArrayType *aa = dynamic_cast<cc::ArrayType*>(a);
    cc::ArrayType *ab = dynamic_cast<cc::ArrayType*>(b);
    if (aa || ab) return aa && ab && sameDeclaredType(aa->element, ab->element);

    return sameType(a, b);
}

bool SemanticAnalyzer::sameType(cc::Type *a, cc::Type *b) {
    a = stripReference(a);
    b = stripReference(b);
    if (!a || !b) return true;          // an earlier error already spoke
    if (dynamic_cast<cxx::BoolType*>(a) || dynamic_cast<cxx::BoolType*>(b)) {
        return dynamic_cast<cxx::BoolType*>(a) && dynamic_cast<cxx::BoolType*>(b);
    }
    cc::BuiltinType *ba = dynamic_cast<cc::BuiltinType*>(a);
    cc::BuiltinType *bb = dynamic_cast<cc::BuiltinType*>(b);
    if (ba && bb) return ba->kind == bb->kind;
    cxx::ClassType *ca = dynamic_cast<cxx::ClassType*>(a);
    cxx::ClassType *cb = dynamic_cast<cxx::ClassType*>(b);
    if (ca && cb) return ca->className == cb->className;
    cc::PointerType *pa = dynamic_cast<cc::PointerType*>(a);
    cc::PointerType *pb = dynamic_cast<cc::PointerType*>(b);
    if (pa && pb) return sameType(pa->base, pb->base);
    return false;                       // different shapes entirely
}

cxx::ClassDecl *SemanticAnalyzer::classOf(cc::Type *t) {
    t = stripReference(t);
    cc::PointerType *pt = dynamic_cast<cc::PointerType*>(t);
    if (pt) t = pt->base;
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    return ct ? findClass(ct->className) : 0;
}

// `T m` and `T m[2]` are both members made of T, and both are constructed --
// the second one element at a time.  classOf answers a different question and
// looks through a pointer to do it, which is exactly wrong here: a `T*` member
// is a value to copy, not an object to construct.
cxx::ClassDecl *SemanticAnalyzer::memberClassOf(cc::Type *t) {
    while (cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t)) t = at->element;
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    return ct ? findClass(ct->className) : 0;
}

// A Derived object begins with its Base subobject at offset 0, so the upcast
// costs nothing.  The reverse is not allowed: a Base* need not be a Derived.
bool SemanticAnalyzer::canConvert(cc::Type *from, cc::Type *to) {
    if (sameType(from, to)) return true;
    if (!from || !to) return true;              // an earlier error already spoke

    cc::Type *f = stripReference(from);
    cc::Type *t = stripReference(to);

    // Any arithmetic type converts to any other, and bool is one of them: it
    // converts from anything as a test against zero, and to anything as 0 or 1.
    cc::BuiltinKind kf, kt;
    if (arithmeticKind(f, kf) && arithmeticKind(t, kt)) return true;
    // A pointer tests as a bool too:  if (p)
    if (isBoolType(t) && dynamic_cast<cc::PointerType*>(f)) return true;

    // Derived* -> Base*, and any object pointer -> void*
    cc::PointerType *pf = dynamic_cast<cc::PointerType*>(f);
    cc::PointerType *ptt = dynamic_cast<cc::PointerType*>(t);
    if (pf && ptt) {
        // void* is the generic address, as in C++.  Not the reverse: coming
        // back out needs a cast, because only the program knows what it is.
        if (isVoid(ptt->base)) return true;
        cxx::ClassDecl *df = classOf(pf->base);
        cxx::ClassDecl *dt = classOf(ptt->base);
        if (df && dt) return isDerivedFrom(df, dt);
        return false;
    }

    // Derived -> Base& , and Derived -> Base
    cxx::ClassDecl *cf = classOf(f);
    cxx::ClassDecl *ctd = classOf(t);
    if (cf && ctd) return isDerivedFrom(cf, ctd);

    return false;
}

// Its TYPE is int, so no type-only rule can accept it where a pointer is
// wanted -- the expression itself has to be looked at.
bool SemanticAnalyzer::isNullPointerConstant(cc::Expr *e) {
    cc::NumberExpr *n = dynamic_cast<cc::NumberExpr*>(e);
    return n && n->value == 0;
}

// Const may be ADDED on the way in, never taken away: a const int* may not
// become an int*, or every check above it could be sidestepped by one
// assignment.  The other direction is always safe.
bool SemanticAnalyzer::constQualificationOk(cc::Type *from, cc::Type *to) {
    cc::Type *f = stripReference(from);
    cc::Type *t = stripReference(to);
    for (;;) {
        cc::PointerType *pf = dynamic_cast<cc::PointerType*>(f);
        cc::PointerType *pt = dynamic_cast<cc::PointerType*>(t);
        if (!pf || !pt) return true;            // not both pointers: nothing to compare
        f = pf->base;
        t = pt->base;
        if (!f || !t) return true;
        if (f->isConst && !t->isConst) return false;
    }
}

bool SemanticAnalyzer::convertible(cc::Expr *fromExpr, cc::Type *from, cc::Type *to) {
    if (dynamic_cast<cc::PointerType*>(stripReference(to)) && isNullPointerConstant(fromExpr)) {
        return true;                            // 0 is any pointer type
    }
    if (!constQualificationOk(from, to)) return false;
    // Binding a T& to a const object would lose the const just as surely.
    if (cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(to)) {
        if (rt->base && !rt->base->isConst && from && stripReference(from)->isConst) {
            return false;
        }
    }
    return canConvert(from, to);
}

// int, int*, int** need no resolution; a class name does.
bool SemanticAnalyzer::checkTypeIsKnown(cc::Type *t, cc::ASTNode *at, const std::string &where) {
    if (!t) return true;
    // A pointer or reference to a class needs only its NAME: `class A;` then
    // `A *p;` is the whole point of a forward declaration.  Anything by value
    // needs the definition, and says so below.
    cc::PointerType *pt = dynamic_cast<cc::PointerType*>(t);
    if (pt) {
        if (dynamic_cast<cxx::ClassType*>(pt->base)) return true;
        return checkTypeIsKnown(pt->base, at, where);
    }
    cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(t);
    if (rt) {
        if (dynamic_cast<cxx::ClassType*>(rt->base)) return true;
        return checkTypeIsKnown(rt->base, at, where);
    }
    cc::ArrayType *arr = dynamic_cast<cc::ArrayType*>(t);
    if (arr) return checkTypeIsKnown(arr->element, at, where);
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    if (ct && !findClass(ct->className)) {
        error(at, "unknown type '" + ct->className + "' in " + where);
        return false;
    }
    return true;
}

// --- Classes and their members ---

cxx::ClassDecl *SemanticAnalyzer::findClass(const std::string &name) {
    std::map<std::string, cxx::ClassDecl*>::iterator it = classes.find(name);
    return (it == classes.end()) ? 0 : it->second;
}

void SemanticAnalyzer::resolveBases() {
    std::map<std::string, cxx::ClassDecl*>::iterator it;
    for (it = classes.begin(); it != classes.end(); ++it) {
        cxx::ClassDecl *cd = it->second;
        if (cd->baseName.empty()) continue;
        cxx::ClassDecl *base = findClass(cd->baseName);
        if (!base) {
            error(cd, "unknown base class '" + cd->baseName + "' for class '" + cd->name + "'");
            continue;
        }
        if (base == cd) {
            error(cd, "class '" + cd->name + "' cannot inherit from itself");
            continue;
        }
        cd->base = base;
    }

    // A chain longer than the number of classes must have revisited one.
    for (it = classes.begin(); it != classes.end(); ++it) {
        cxx::ClassDecl *cd = it->second;
        std::size_t steps = 0;
        for (cxx::ClassDecl *p = cd->base; p; p = p->base) {
            if (p == cd || ++steps > classes.size()) {
                error(cd, "inheritance cycle involving class '" + cd->name + "'");
                cd->base = 0;               // break it, so later walks terminate
                break;
            }
        }
    }
}

// Most-derived first: the first match wins, which IS name hiding.
// The member an operator expression calls, if the left operand is an object
// that declares one.  A class without the operator is not an error here: the
// builtin rules below will say what is actually wrong with it.
// The object a method call is made on: `a.f()` uses a itself, `p->f()` uses
// what p points at.
bool SemanticAnalyzer::objectIsConst(cxx::MemberAccessExpr *ma) {
    if (!ma || !ma->base) return false;
    if (!ma->isArrow) return isConstExpr(ma->base);
    cc::Type *bt = stripReference(ma->base->resolvedType);
    cc::PointerType *pt = dynamic_cast<cc::PointerType*>(bt);
    return pt && pt->base && pt->base->isConst;
}

bool SemanticAnalyzer::isNonConstReferenceTo(cc::Type *t) {
    cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(t);
    return rt && rt->base && !rt->base->isConst;
}

bool SemanticAnalyzer::isConstExpr(cc::Expr *e) {
    if (!e) return false;

    // Inside a const member function, *this is const -- so every field of it
    // is, whether written `x` or `this->x`.
    if (currentMethodIsConst) {
        if (cxx::MemberAccessExpr *tm = dynamic_cast<cxx::MemberAccessExpr*>(e)) {
            if (dynamic_cast<cxx::ThisExpr*>(tm->base)) return true;
        }
        if (cc::IdentExpr *id = dynamic_cast<cc::IdentExpr*>(e)) {
            Symbol *sym = symbols.lookup(id->name);
            if (sym && sym->kind == SYM_Field) return true;
        }
    }

    // A member is const when the object is, whatever the field says.
    if (cxx::MemberAccessExpr *ma = dynamic_cast<cxx::MemberAccessExpr*>(e)) {
        if (!ma->isArrow && isConstExpr(ma->base)) return true;
        // p->x through a const P*: the OBJECT is *p, so its constness is the
        // pointee's, not the pointer's.
        if (ma->isArrow && ma->base) {
            cc::Type *bt = stripReference(ma->base->resolvedType);
            cc::PointerType *pt = dynamic_cast<cc::PointerType*>(bt);
            if (pt && pt->base && pt->base->isConst) return true;
        }
    }
    // An element of a const ARRAY is const.  Through a POINTER it is not:
    // `char* const p` freezes p, not what p points at, so p[0] stays writable
    // and the pointee's own const decides.
    if (cc::IndexExpr *ix = dynamic_cast<cc::IndexExpr*>(e)) {
        // The one case that must NOT propagate is a const POINTER: `char* const
        // p` freezes p, not what p points at.  Everything else that is const --
        // a const array, a field of a const object, a field inside a const
        // member function -- passes its constness to the element.
        cc::Type *bt = ix->base ? stripReference(ix->base->resolvedType) : 0;
        const bool pointerItselfConst =
            bt && dynamic_cast<cc::PointerType*>(bt) != 0 && bt->isConst;
        if (!pointerItselfConst && isConstExpr(ix->base)) return true;
    }

    cc::Type *t = stripReference(e->resolvedType);
    return t && t->isConst;
}

bool SemanticAnalyzer::isClassType(cc::Type *t) {
    return dynamic_cast<cxx::ClassType*>(stripReference(t)) != 0;
}

// An operator expression looks for a function only when an operand is an
// object: overloading for two builtins is not a thing C++ allows either.
cc::Function *SemanticAnalyzer::findOperator(cc::Expr *lhs, cc::Type *lt,
                                             cc::Expr *rhs, cc::Type *rt,
                                             cc::BinaryOp op, cc::ASTNode *at) {
    if (!isClassType(lt) && !isClassType(rt)) return 0;

    // A member is preferred, and only exists when the object is on the left.
    if (cxx::MethodDecl *m = findMemberOperator(lt, op, rhs, rt, at)) return m;

    // operator= must be a member; C++ says so, and a non-member one could not
    // be generated for a class that never mentioned it.
    if (op == cc::BIN_Assign) return 0;

    return findFreeOperator(lhs, lt, rhs, rt, std::string("operator") + cc::binaryOpText(op));
}

// The unary minus of an object.  It is the one unary operator this compiler
// overloads, and it is the one worth overloading: a vector, a matrix or a
// complex number all have a negation, and none of them have a `!`.
//
// A MEMBER takes nothing at all -- `V operator-()` -- which is what makes it
// unary; the one-parameter `operator-` beside it is the binary subtraction,
// and the two are told apart here by nothing more than that count.  A
// NON-MEMBER takes the operand itself, which is how a class whose definition
// cannot be changed still gets a negation.
cc::Function *SemanticAnalyzer::findUnaryMinusOperator(cc::Expr *operand,
                                                       cc::Type *t,
                                                       cc::ASTNode *at) {
    if (!isClassType(t)) return 0;

    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(stripReference(t));
    cxx::ClassDecl *cd = ct ? findClass(ct->className) : 0;
    if (cd) {
        const std::vector<cc::Function*> cands = findMethods(cd, "operator-");
        for (std::size_t i = 0; i < cands.size(); ++i) {
            cxx::MethodDecl *m = dynamic_cast<cxx::MethodDecl*>(cands[i]);
            if (!m || !m->params.empty()) continue;
            cxx::ClassDecl *owner = findClass(m->ownerClass);
            if (!memberIsAccessible(m, owner)) {
                error(at, std::string("'operator-' is ") + cxx::accessText(memberAccess(m))
                          + " in class '" + (owner ? owner->name : ct->className) + "'");
            }
            checkConstUse(m, stripReference(t)->isConst, at);
            return m;
        }
    }

    std::map<std::string, std::vector<cc::Function*> >::const_iterator it =
        overloads.find("operator-");
    if (it == overloads.end()) return 0;
    const std::vector<cc::Function*> &free = it->second;
    cc::Function *exact = 0;
    cc::Function *viable = 0;
    for (std::size_t i = 0; i < free.size(); ++i) {
        cc::Function *f = free[i];
        if (f->params.size() != 1) continue;          // two is the binary one
        cc::Type *want = f->params[0]->type;
        if (exactForOverload(t, want))          { if (!exact)  exact  = f; continue; }
        if (convertible(operand, t, want))      { if (!viable) viable = f; }
    }
    return exact ? exact : viable;
}

// The file-scope operator whose two parameters accept these two operands.
cc::Function *SemanticAnalyzer::findFreeOperator(cc::Expr *lhs, cc::Type *lt,
                                                 cc::Expr *rhs, cc::Type *rt,
                                                 const std::string &name) {
    std::map<std::string, std::vector<cc::Function*> >::const_iterator it =
        overloads.find(name);
    if (it == overloads.end()) return 0;

    const std::vector<cc::Function*> &cands = it->second;
    cc::Function *exact = 0;
    cc::Function *viable = 0;
    for (std::size_t i = 0; i < cands.size(); ++i) {
        cc::Function *f = cands[i];
        if (f->params.size() != 2) continue;
        cc::Type *p0 = f->params[0]->type;
        cc::Type *p1 = f->params[1]->type;
        if (exactForOverload(lt, p0) && exactForOverload(rt, p1)) {
            if (!exact) exact = f;
            continue;
        }
        if (convertible(lhs, lt, p0) && convertible(rhs, rt, p1)) {
            if (!viable) viable = f;
        }
    }
    return exact ? exact : viable;
}

// An operator may be overloaded like any other member -- operator+(V) beside
// operator+(int) -- so the RIGHT operand chooses which.  Taking the first by
// name would reject `v + 5` for having the wrong argument type.
// obj(args) selects among the class's operator() overloads by the arguments,
// exactly as an ordinary call does.
cxx::MethodDecl *SemanticAnalyzer::findCallOperator(cc::Type *ot, cc::CallExpr *call) {
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(stripReference(ot));
    if (!ct) return 0;
    cxx::ClassDecl *cd = findClass(ct->className);
    if (!cd) return 0;
    const std::vector<cc::Function*> cands = findMethods(cd, "operator()");
    if (cands.empty()) return 0;
    cc::Function *chosen = resolveOverload(cands, call, "operator()");
    cxx::MethodDecl *m = dynamic_cast<cxx::MethodDecl*>(chosen);
    if (!m) return 0;
    cxx::ClassDecl *owner = findClass(m->ownerClass);
    if (!memberIsAccessible(m, owner)) {
        error(call, std::string("'operator()' is ") + cxx::accessText(memberAccess(m))
                    + " in class '" + (owner ? owner->name : cd->name) + "'");
    }
    checkConstUse(m, ot && stripReference(ot)->isConst, call);
    return m;
}

// operator[] takes one argument, so the index picks the overload.
cxx::MethodDecl *SemanticAnalyzer::findIndexOperator(cxx::ClassDecl *cd, cc::Expr *index,
                                                     cc::Type *it, bool objectConst,
                                                     cc::ASTNode *at) {
    if (!cd) return 0;
    const std::vector<cc::Function*> cands = findMethods(cd, "operator[]");
    // A container usually declares the pair -- `T& operator[](int)` beside
    // `T operator[](int) const` -- so constness picks between them before the
    // argument does.
    cxx::MethodDecl *exact = 0, *viable = 0;
    for (int pass = 0; pass < 2 && !exact && !viable; ++pass) {
        const bool wantConst = (pass == 0) ? objectConst : !objectConst;
        for (std::size_t i = 0; i < cands.size(); ++i) {
            cxx::MethodDecl *m = dynamic_cast<cxx::MethodDecl*>(cands[i]);
            if (!m || m->params.size() != 1) continue;
            if (m->isConstMethod != wantConst) continue;
            cc::Type *want = m->params[0]->type;
            if (it && exactForOverload(it, want)) { if (!exact) exact = m; continue; }
            if (convertible(index, it, want))             { if (!viable) viable = m; }
        }
    }
    cxx::MethodDecl *chosen = exact ? exact : viable;
    if (!chosen) return 0;
    checkConstUse(chosen, objectConst, at);
    cxx::ClassDecl *owner = findClass(chosen->ownerClass);
    if (!memberIsAccessible(chosen, owner)) {
        error(at, std::string("'operator[]' is ") + cxx::accessText(memberAccess(chosen))
                  + " in class '" + (owner ? owner->name : cd->name) + "'");
    }
    return chosen;
}

// Every member reached through an object obeys the same const rule, whether it
// is spelled f(), a.f(), a + b or a[i].  Keeping the check in one place is what
// stops each new call form growing a hole of its own.
void SemanticAnalyzer::checkConstUse(cxx::MethodDecl *m, bool objectConst, cc::ASTNode *at) {
    if (!m || !objectConst || m->isConstMethod) return;
    error(at, "'" + m->name + "' is not const, and the object here is");
}

cxx::MethodDecl *SemanticAnalyzer::findMemberOperator(cc::Type *lt, cc::BinaryOp op,
                                                      cc::Expr *rhs, cc::Type *rt,
                                                      cc::ASTNode *at) {
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(stripReference(lt));
    if (!ct) return 0;
    cxx::ClassDecl *cd = findClass(ct->className);
    if (!cd) return 0;

    const std::string name = std::string("operator") + cc::binaryOpText(op);
    const std::vector<cc::Function*> cands = findMethods(cd, name);
    if (cands.empty()) return 0;

    cxx::MethodDecl *exact = 0;
    cxx::MethodDecl *viable = 0;
    for (std::size_t i = 0; i < cands.size(); ++i) {
        cxx::MethodDecl *m = dynamic_cast<cxx::MethodDecl*>(cands[i]);
        if (!m || m->params.size() != 1) continue;
        cc::Type *want = m->params[0]->type;
        if (rt && exactForOverload(rt, want)) { if (!exact) exact = m; continue; }
        if (convertible(rhs, rt, want))               { if (!viable) viable = m; }
    }

    // No member takes this operand -- decline QUIETLY, because a non-member
    // may well take it.  Reporting here made `cout << myObject` impossible:
    // ostream has member <<'s, so the free one was never reached.
    cxx::MethodDecl *chosen = exact ? exact : viable;
    if (!chosen) return 0;

    cxx::ClassDecl *owner = findClass(chosen->ownerClass);
    if (!memberIsAccessible(chosen, owner)) {
        error(at, "'" + chosen->name + "' is " + cxx::accessText(memberAccess(chosen))
                  + " in class '" + (owner ? owner->name : ct->className) + "'");
    }
    checkConstUse(chosen, lt && stripReference(lt)->isConst, at);
    return chosen;
}

cc::Decl *SemanticAnalyzer::findMember(cxx::ClassDecl *cd, const std::string &member,
                                       cxx::ClassDecl **foundIn) {
    for (cxx::ClassDecl *c = cd; c; c = c->base) {
        for (std::size_t i = 0; i < c->members.size(); ++i) {
            cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(c->members[i]);
            if (fd && fd->name == member) { if (foundIn) *foundIn = c; return fd; }
            cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(c->members[i]);
            // Neither takes part in ordinary name lookup.
            if (md && !md->isConstructor && !md->isDestructor && md->name == member) {
                if (foundIn) *foundIn = c;
                return md;
            }
        }
    }
    return 0;
}

bool SemanticAnalyzer::isDerivedFrom(cxx::ClassDecl *derived, cxx::ClassDecl *base) {
    for (cxx::ClassDecl *c = derived; c; c = c->base) {
        if (c == base) return true;
    }
    return false;
}

// A friend is granted access by name, so the question is simply whether the
// function we are inside is one the class named.
bool SemanticAnalyzer::isFriendOf(cxx::ClassDecl *owner) const {
    if (!owner || !currentFunction) return false;
    for (std::size_t i = 0; i < owner->friends.size(); ++i) {
        cc::Function *f = owner->friends[i];
        if (f->name == currentFunction->name && sameParams(f, currentFunction)) return true;
    }
    return false;
}

bool SemanticAnalyzer::memberIsAccessible(cc::Decl *m, cxx::ClassDecl *owner) const {
    const cxx::Access a = memberAccess(m);
    if (a == cxx::ACC_Public) return true;
    // A friend sees everything, from wherever it is written.
    if (isFriendOf(owner)) return true;
    if (currentClass.empty()) return false;
    // const, so findClass() is off limits.
    std::map<std::string, cxx::ClassDecl*>::const_iterator it = classes.find(currentClass);
    cxx::ClassDecl *from = (it == classes.end()) ? 0 : it->second;
    if (!from) return false;
    if (from == owner) return true;                 // inside the class itself
    if (a == cxx::ACC_Protected) return isDerivedFrom(from, owner);
    return false;                                   // private, from outside
}

cxx::ClassDecl *SemanticAnalyzer::ownerClassOf(cc::Decl *m) {
    cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(m);
    if (fd) return findClass(fd->ownerClass);
    cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(m);
    if (md) return findClass(md->ownerClass);
    return 0;
}

bool SemanticAnalyzer::sameSignature(cc::Function *a, cc::Function *b) {
    if (a->params.size() != b->params.size()) return false;
    for (std::size_t i = 0; i < a->params.size(); ++i) {
        if (!sameType(a->params[i]->type, b->params[i]->type)) return false;
    }
    return true;
}

cxx::Access SemanticAnalyzer::memberAccess(cc::Decl *m) {
    cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(m);
    if (fd) return fd->access;
    cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(m);
    if (md) return md->access;
    return cxx::ACC_Public;
}

cc::Type *SemanticAnalyzer::memberType(cc::Decl *m) {
    cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(m);
    if (fd) return fd->type;
    cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(m);
    if (md) return md->retType;
    return 0;
}

// Same name and matching signature over a base virtual is an override.  Same
// name, different signature is legal but almost always a mistake, so it warns.
void SemanticAnalyzer::resolveOverrides(cxx::ClassDecl *cd) {
    if (!cd->base) return;

    // A destructor overrides by POSITION: ~Derived and ~Base are spelled
    // differently but occupy the same vtable slot.
    if (cd->dtor) {
        for (cxx::ClassDecl *b = cd->base; b; b = b->base) {
            if (!b->dtor) continue;
            if (b->dtor->isVirtual) {
                cd->dtor->overrides = b->dtor;
                cd->dtor->isVirtual = true;
            }
            break;
        }
    }

    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(cd->members[i]);
        if (!md || md->isConstructor || md->isDestructor) continue;
        // A base may declare several virtuals of one name.  The one being
        // overridden is the one with the SAME SIGNATURE; taking the first by
        // name gave f(int,int) a vtable slot of its own and sent every call
        // through f(int).
        cxx::MethodDecl *bm = 0;
        cxx::MethodDecl *nameOnly = 0;
        for (cxx::ClassDecl *b = cd->base; b && !bm; b = b->base) {
            for (std::size_t k = 0; k < b->members.size(); ++k) {
                cxx::MethodDecl *cand = dynamic_cast<cxx::MethodDecl*>(b->members[k]);
                if (!cand || cand->isConstructor || cand->isDestructor) continue;
                if (cand->name != md->name) continue;
                if (sameSignature(md, cand)) { bm = cand; break; }
                if (!nameOnly) nameOnly = cand;
            }
        }

        if (!bm) {
            // Same name, no signature matches: hiding, which is legal and
            // almost always a mistake.
            if (nameOnly) {
                diag.warning(md->line, md->col,
                             "'" + md->name + "' hides '" + nameOnly->ownerClass
                             + "::" + nameOnly->name + "' rather than overriding it");
            }
            continue;
        }
        if (!bm->isVirtual) continue;       // hiding a non-virtual is not an override
        if (!sameType(md->retType, bm->retType)) {
            error(md, "'" + md->name + "' overrides '" + bm->ownerClass + "::" + bm->name
                      + "' but returns " + describe(md->retType)
                      + " instead of " + describe(bm->retType));
            continue;
        }
        // Virtualness propagates down the chain whether or not `virtual` is
        // repeated.
        md->overrides = bm;
        md->isVirtual = true;
    }
}

// Most-derived first, so insert() -- which refuses a duplicate -- keeps the
// derived member.  That is name hiding, for free.
void SemanticAnalyzer::pushClassScope(cxx::ClassDecl *cd) {
    symbols.pushScope();
    for (cxx::ClassDecl *c = cd; c; c = c->base) {
        for (std::size_t i = 0; i < c->members.size(); ++i) {
            cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(c->members[i]);
            if (fd) {
                Symbol *s = new Symbol(SYM_Field, fd->name, fd, fd->type);
                if (!symbols.insert(fd->name, s)) delete s;
                continue;
            }
            cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(c->members[i]);
            if (md && !md->isConstructor && !md->isDestructor) {
                Symbol *s = new Symbol(SYM_Method, md->name, md, md->retType);
                if (!symbols.insert(md->name, s)) delete s;
            }
        }
    }
}

// Only a class object whose class or some base declares one.  A pointer never
// does -- `delete` is explicit for a reason.
// Does anything this class owns need destroying?  Its base, or a member that
// is an object (an array of them counts).
bool SemanticAnalyzer::needsDestructor(cxx::ClassDecl *cd) {
    if (!cd) return false;
    if (cd->dtor) return true;
    for (cxx::ClassDecl *b = cd->base; b; b = b->base) if (b->dtor) return true;
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
        if (fd && hasDestructor(fd->type)) return true;
    }
    return false;
}

// Exact ENOUGH to win outright, for overload resolution only.
//
// sameType, plus one rule: void* is exact for any pointer.  Conversions here
// come in two tiers -- exact, then merely possible -- with no ranking inside
// the second, and a pointer can become either a void* or a bool, because
// `if (p)` is the reason bool takes a pointer at all.  So an overload set
// holding both is ambiguous for every pointer, and ostream's holds both.
// Calling void* the exact answer for a pointer settles it the way C++ settles
// it, without a conversion-ranking pass this compiler does not have.
//
// The deviation, and it is a real one: f(Base*) beside f(void*), called with a
// Derived*, picks void* here where C++ picks Base*.
bool SemanticAnalyzer::exactForOverload(cc::Type *arg, cc::Type *param) {
    cc::Type *p = stripReference(param);
    if (sameType(arg, p)) return true;
    cc::PointerType *pa = dynamic_cast<cc::PointerType*>(stripReference(arg));
    cc::PointerType *pp = dynamic_cast<cc::PointerType*>(p);
    return pa != 0 && pp != 0 && isVoid(pp->base);
}

cxx::MethodDecl *SemanticAnalyzer::copyConstructorOf(cxx::ClassDecl *cd) {
    if (!cd) return 0;
    for (std::size_t i = 0; i < cd->ctors.size(); ++i) {
        cxx::MethodDecl *c = cd->ctors[i];
        if (!c || c->params.size() != 1) continue;
        // By reference, necessarily: taking the argument by value would need
        // the very copy being defined.
        cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(c->params[0]->type);
        if (!rt) continue;
        cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(rt->base);
        if (ct && ct->className == cd->name) return c;
    }
    return 0;
}

// Can a copy constructor for this class be GENERATED?  Only if every part its
// initialiser list will name can be constructed from one argument.  A part
// that declares a copy constructor answers yes on the spot; otherwise it must
// be one this same rule can generate, which makes the question recursive.
//
// An array member used to stop the recursion with a no, because no syntax puts
// an array in an initialiser list.  That is true of the syntax and false of the
// consequence: generating nothing left the WHOLE class on the byte copy, so a
// sibling member that DID have a copy constructor never saw one either -- and
// where that sibling owned memory, the two objects ended up holding one
// pointer and the second destructor freed it twice.  An array member is now
// named like any other and lowered as a whole-array move, which is what the
// byte copy always did for it, so the array is copied exactly as before and
// everything beside it is copied properly.
bool SemanticAnalyzer::canSynthesiseCopy(cxx::ClassDecl *cd, int depth) {
    if (!cd) return true;                       // no part is no obstacle
    if (copyConstructorOf(cd)) return true;     // declared: nothing to generate
    if (depth > 64) return false;               // a hierarchy this deep is broken

    if (!canSynthesiseCopy(cd->base, depth + 1)) return false;
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
        if (!fd) continue;
        // Only a member made OF a class is constructed -- an array of them one
        // element at a time.  A pointer or a scalar is copied as the value it is.
        cxx::ClassDecl *fcd = memberClassOf(fd->type);
        if (!fcd) continue;
        if (!canSynthesiseCopy(fcd, depth + 1)) return false;
    }
    return true;
}

bool SemanticAnalyzer::needsCopyConstructor(cxx::ClassDecl *cd) {
    if (!cd || copyConstructorOf(cd)) return false;
    if (!canSynthesiseCopy(cd)) return false;

    for (cxx::ClassDecl *b = cd->base; b; b = b->base) {
        if (copyConstructorOf(b)) return true;
    }
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
        if (!fd) continue;
        if (copyConstructorOf(memberClassOf(fd->type))) return true;
    }
    return false;
}

// The copy constructor the language says exists whether or not it is written.
// Without it a derived object was copied with memcpy, so a base that counted
// its copies never saw one -- the same silent wrong answer a declared copy
// constructor was added to prevent, one level up.
//
// It is built as the initialiser list a reader would have written:
//
//     Derived(const Derived &__o) : Base(__o), extra(__o.extra) { }
//
// so nothing downstream needs to know it was generated.  selectConstructor
// picks the base's and each member's copy constructor exactly as it would for
// a written one, and Lowering::copyConstructorOf finds it in cd->ctors.
//
// Generates the constructor for cd, and FIRST for every base and class-typed
// member its initialiser list will name -- otherwise the list calls a
// constructor that does not exist, and a class whose parts were merely
// default-constructible stopped compiling the moment one of its other parts
// gained a copy constructor.  canSynthesiseCopy has already established that
// the recursion succeeds all the way down.
bool SemanticAnalyzer::synthesiseCopyFor(cxx::ClassDecl *cd) {
    if (!cd || copyConstructorOf(cd) || !canSynthesiseCopy(cd)) return false;

    if (cd->base) synthesiseCopyFor(cd->base);
    for (std::size_t m = 0; m < cd->members.size(); ++m) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[m]);
        if (!fd) continue;
        // An array member's ELEMENT class needs one too: lowering calls it per
        // element, so it must exist by the time lowering looks.
        if (cxx::ClassDecl *fcd = memberClassOf(fd->type)) synthesiseCopyFor(fcd);
    }

    cxx::MethodDecl *c = new cxx::MethodDecl(0, cd->name, cxx::ACC_Public);
    c->ownerClass = cd->name;
    c->isConstructor = true;
    c->isImplicit = true;
    c->line = cd->line;
    c->col = cd->col;

    // const T &__o -- the const rides on the referenced type, which is
    // where every check looks for it.
    cxx::ClassType *arg = new cxx::ClassType(cd->name);
    arg->isConst = true;
    arg->line = cd->line;
    arg->col = cd->col;
    cxx::ReferenceType *ref = new cxx::ReferenceType(arg);
    ref->line = cd->line;
    ref->col = cd->col;
    cc::VarDecl *param = new cc::VarDecl(ref, "__o", 0);
    param->line = cd->line;
    param->col = cd->col;
    c->params.push_back(param);

    // : Base(__o)
    if (cd->base) {
        cxx::MemberInit bi;
        bi.name = cd->base->name;
        bi.line = cd->line;
        bi.col = cd->col;
        cc::IdentExpr *src = new cc::IdentExpr("__o");
        src->line = cd->line;
        src->col = cd->col;
        bi.args.push_back(src);
        c->memberInits.push_back(bi);
    }

    // , each member as  m(__o.m)
    for (std::size_t m = 0; m < cd->members.size(); ++m) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[m]);
        if (!fd) continue;
        cxx::MemberInit mi;
        mi.name = fd->name;
        mi.line = cd->line;
        mi.col = cd->col;
        cc::IdentExpr *base = new cc::IdentExpr("__o");
        base->line = cd->line;
        base->col = cd->col;
        cxx::MemberAccessExpr *acc =
            new cxx::MemberAccessExpr(base, fd->name, false);
        acc->line = cd->line;
        acc->col = cd->col;
        mi.args.push_back(acc);
        c->memberInits.push_back(mi);
    }

    c->body = new cc::CompoundStmt();    // the list is the whole of it
    c->body->line = cd->line;
    c->body->col = cd->col;

    cd->members.push_back(c);
    cd->ctors.push_back(c);
    return true;
}

void SemanticAnalyzer::synthesiseCopyConstructors() {
    // Repeat until nothing changes: giving one class a copy constructor can
    // make a class that holds one need its own.
    bool changed = true;
    while (changed) {
        changed = false;
        std::map<std::string, cxx::ClassDecl*>::iterator it;
        for (it = classes.begin(); it != classes.end(); ++it) {
            if (!it->second || !needsCopyConstructor(it->second)) continue;
            if (synthesiseCopyFor(it->second)) changed = true;
        }
    }
}

void SemanticAnalyzer::synthesiseDestructors() {
    // Repeat until nothing changes: giving one class a destructor can make a
    // class that holds one need its own.
    bool changed = true;
    while (changed) {
        changed = false;
        std::map<std::string, cxx::ClassDecl*>::iterator it;
        for (it = classes.begin(); it != classes.end(); ++it) {
            cxx::ClassDecl *cd = it->second;
            if (!cd || cd->dtor || !needsDestructor(cd)) continue;
            cxx::MethodDecl *d = new cxx::MethodDecl(0, "~" + cd->name, cxx::ACC_Public);
            d->ownerClass = cd->name;
            d->isDestructor = true;
            d->isImplicit = true;
            d->line = cd->line;
            d->col = cd->col;
            d->body = new cc::CompoundStmt();       // nothing of its own to do
            d->body->line = cd->line;
            d->body->col = cd->col;
            cd->members.push_back(d);
            cd->dtor = d;
            changed = true;
        }
    }
}

bool SemanticAnalyzer::hasDestructor(cc::Type *t) {
    if (!t) return false;
    if (dynamic_cast<cc::PointerType*>(t)) return false;
    if (dynamic_cast<cxx::ReferenceType*>(t)) return false;   // a reference owns nothing
    // An array of objects owns every one of them.
    while (cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t)) t = at->element;
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    if (!ct) return false;
    for (cxx::ClassDecl *c = findClass(ct->className); c; c = c->base) {
        if (c->dtor) return true;
    }
    return false;
}

// Rules about a class as a whole.
void SemanticAnalyzer::checkClassInvariants(cxx::ClassDecl *cd) {
    // A constructor runs BEFORE the object has a vtable pointer to dispatch
    // through -- it is what puts one there -- so there is nothing for
    // `virtual` to mean on it.  Cleared FIRST, so the polymorphism test below
    // is not fooled by a keyword that should never have been there.
    for (std::size_t i = 0; i < cd->ctors.size(); ++i) {
        if (cd->ctors[i]->isVirtual) {
            error(cd->ctors[i], "a constructor cannot be virtual");
            cd->ctors[i]->isVirtual = false;
        }
    }

    // Two members of one name and one signature: the call site could not
    // choose between them.
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::MethodDecl *a = dynamic_cast<cxx::MethodDecl*>(cd->members[i]);
        if (!a || a->isDestructor) continue;
        for (std::size_t j = i + 1; j < cd->members.size(); ++j) {
            cxx::MethodDecl *b = dynamic_cast<cxx::MethodDecl*>(cd->members[j]);
            if (!b || b->name != a->name || b->isDestructor) continue;
            if (!sameParams(a, b)) continue;
            error(b, "'" + cd->name + "::" + a->name + "' is declared twice with the same parameters");
        }
        for (std::size_t j = 0; j < cd->members.size(); ++j) {
            cxx::FieldDecl *f = dynamic_cast<cxx::FieldDecl*>(cd->members[j]);
            if (f && f->name == a->name) {
                error(f, "'" + f->name + "' is both a field and a member function of '"
                         + cd->name + "'");
            }
        }
    }

    if (cd->dtor && !cd->dtor->params.empty()) {
        error(cd->dtor, "a destructor cannot take parameters");
    }

    // A polymorphic class is meant to be used through a base pointer, and
    // deleting through one with a non-virtual destructor runs the wrong one.
    bool polymorphic = false;
    for (cxx::ClassDecl *c = cd; c && !polymorphic; c = c->base) {
        for (std::size_t i = 0; i < c->members.size(); ++i) {
            cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(c->members[i]);
            if (md && md->isVirtual && !md->isDestructor && !md->isConstructor) {
                polymorphic = true;
                break;
            }
        }
    }
    // Only advise about a destructor the user actually wrote; one the compiler
    // generated is not theirs to make virtual.
    if (polymorphic && cd->dtor && !cd->dtor->isVirtual && !cd->dtor->isImplicit) {
        diag.warning(cd->dtor->line, cd->dtor->col,
                     "class '" + cd->name + "' has virtual functions but a non-virtual destructor");
    }
}

// Each name is a field OF THIS CLASS or the base's own name.  An inherited
// field is the base constructor's job, which is why the base gets an entry.
void SemanticAnalyzer::analyzeMemberInits(cxx::MethodDecl *ctor, cxx::ClassDecl *cd) {
    std::vector<std::string> seen;
    int lastFieldIndex = -1;
    bool outOfOrder = false;

    for (std::size_t i = 0; i < ctor->memberInits.size(); ++i) {
        cxx::MemberInit &mi = ctor->memberInits[i];

        for (std::size_t j = 0; j < seen.size(); ++j) {
            if (seen[j] == mi.name) {
                diag.error(mi.line, mi.col, "'" + mi.name + "' is initialised more than once");
            }
        }
        seen.push_back(mi.name);

        // the base class, by name
        if (cd->base && mi.name == cd->base->name) {
            mi.isBase = true;
            if (i != 0) {
                diag.warning(mi.line, mi.col,
                             "the base class initialiser is written after a member, but the base "
                             "is always constructed first");
            }
            for (std::size_t a = 0; a < mi.args.size(); ++a) {
                bool lv = false;
                analyzeExpr(mi.args[a], lv);
            }
            mi.resolvedCtor = selectConstructor(cd->base, mi.args, ctor,
                              "base class '" + cd->base->name + "'");
            continue;
        }

        // otherwise a field declared in THIS class
        int fieldIndex = -1;
        cxx::FieldDecl *field = 0;
        int counter = 0;
        for (std::size_t m = 0; m < cd->members.size(); ++m) {
            cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[m]);
            if (!fd) continue;
            if (fd->name == mi.name) { field = fd; fieldIndex = counter; break; }
            ++counter;
        }
        if (!field) {
            cxx::ClassDecl *owner = 0;
            if (cd->base && findMember(cd->base, mi.name, &owner)) {
                diag.error(mi.line, mi.col,
                           "'" + mi.name + "' is inherited from '" + owner->name
                           + "'; initialise the base instead");
            } else {
                diag.error(mi.line, mi.col,
                           "'" + mi.name + "' is not a member of class '" + cd->name + "'");
            }
            continue;
        }

        if (fieldIndex < lastFieldIndex) outOfOrder = true;
        lastFieldIndex = fieldIndex;

        // An array member, which only a GENERATED constructor names: there is
        // no syntax for it, so a user still reaches the checks below and the
        // error they already gave.  What the entry means is a whole-array
        // move, decided in lowering; here it needs only its source analysed,
        // because lowering asks for that expression's type.
        if (ctor->isImplicit && dynamic_cast<cc::ArrayType*>(field->type)) {
            for (std::size_t a = 0; a < mi.args.size(); ++a) {
                bool lv = false;
                analyzeExpr(mi.args[a], lv);
            }
            continue;
        }

        // A member that is itself an object is CONSTRUCTED here, with whatever
        // arguments are written -- `Outer() : in(7)` calls Inner(int).  Only a
        // scalar member is initialised from a single value.
        if (cxx::ClassType *fct = dynamic_cast<cxx::ClassType*>(field->type)) {
            cxx::ClassDecl *fcd = findClass(fct->className);
            mi.resolvedCtor = selectConstructor(fcd, mi.args, ctor,
                                                "the initialiser for '" + mi.name + "'");
            continue;
        }

        if (mi.args.size() != 1) {
            if (mi.args.empty() && hasDestructor(field->type)) continue;   // default-construct
            if (mi.args.size() > 1) {
                diag.error(mi.line, mi.col,
                           "'" + mi.name + "' takes one initialiser value");
                continue;
            }
        }
        for (std::size_t a = 0; a < mi.args.size(); ++a) {
            bool lv = false;
            cc::Type *at = analyzeExpr(mi.args[a], lv);
            if (at && !convertible(mi.args[a], at, field->type)) {
                diag.error(mi.line, mi.col,
                           "cannot initialise '" + describe(field->type) + " " + mi.name
                           + "' from an expression of type " + describe(at));
            } else if (at) {
                warnIfNarrowing(mi.args[a], at, field->type, mi.args[a],
                                "the initialisation of '" + mi.name + "'");
            }
        }
    }

    // Members are constructed in DECLARATION order, never in the order the
    // list is written.  The bug only shows when one initialiser reads another.
    if (outOfOrder) {
        diag.warning(ctor->line, ctor->col,
                     "initialiser list is out of declaration order; members construct in declaration order");
    }
}

cxx::MethodDecl *SemanticAnalyzer::selectConstructor(cxx::ClassDecl *cd,
                                                     const std::vector<cc::Expr*> &args,
                                                     cc::ASTNode *at, const std::string &what) {
    if (!cd) return 0;
    const std::size_t argCount = args.size();

    // No constructor the USER wrote is legal: the fields are uninitialised, as
    // in C.  A constructor the compiler generated does not change that.  The
    // implicit copy constructor in particular must not take default
    // construction away from a class that never declared anything -- writing
    // nothing cannot be what makes `Derived a;` illegal.
    bool userWritten = false;
    for (std::size_t i = 0; i < cd->ctors.size(); ++i) {
        if (cd->ctors[i] && !cd->ctors[i]->isImplicit) { userWritten = true; break; }
    }

    if (!userWritten && argCount == 0) return 0;

    if (cd->ctors.empty()) {
        if (argCount > 0) {
            error(at, "class '" + cd->name + "' has no constructor to take arguments");
        }
        return 0;
    }

    // By SIGNATURE, like any other overload.  P(int,int) and P(double,double)
    // are two constructors, and choosing on the count alone could only ever
    // reject the pair.
    std::vector<cc::Type*> argTypes;
    for (std::size_t i = 0; i < argCount; ++i) {
        bool lv = false;
        argTypes.push_back(analyzeExpr(args[i], lv));
    }

    std::vector<cxx::MethodDecl*> exact, viable;
    for (std::size_t i = 0; i < cd->ctors.size(); ++i) {
        cxx::MethodDecl *c = cd->ctors[i];
        if (c->params.size() != argCount) continue;
        bool allExact = true, allOk = true;
        for (std::size_t k = 0; k < argCount; ++k) {
            if (!argTypes[k]) continue;
            cc::Type *want = c->params[k]->type;
            if (!exactForOverload(argTypes[k], want)) allExact = false;
            if (!convertible(args[k], argTypes[k], want)) allOk = false;
        }
        if (allExact) exact.push_back(c);
        if (allOk) viable.push_back(c);
    }

    if (exact.size() == 1) return exact[0];
    if (exact.size() > 1) {
        error(at, "ambiguous constructor for class '" + cd->name + "'");
        return exact[0];
    }
    if (viable.size() == 1) return viable[0];
    if (viable.size() > 1) {
        error(at, "ambiguous constructor for class '" + cd->name + "'");
        return viable[0];
    }

    error(at, "class '" + cd->name + "' has no constructor taking "
              + countText(argCount) + " argument(s), needed for " + what);
    return 0;
}

// --- Entry point ---

void SemanticAnalyzer::analyze(const std::vector<cc::Decl*> &units) {
    // 1) every class name, so declarations may refer to a class defined later
    collectClasses(units);
    // 2) link the hierarchy, before anything tries to look a member up
    resolveBases();
    // 2b) give each out-of-line body to the member it belongs to, so the rest
    //     of the pass sees one complete class
    attachOutOfLineDefinitions(units);
    // 2c) a class whose base or whose members need destroying needs a
    //     destructor of its own, whether or not one was written.  Without it
    //     emitEpilogue never runs and those members are never destroyed.
    synthesiseDestructors();
    // 2d) and a class whose base or whose members have a copy constructor
    //     needs one of its own, or copying it is a memcpy and those
    //     constructors never run.  After the destructors, because both walk
    //     the same members and neither depends on the other.
    synthesiseCopyConstructors();
    // 3) work out which methods override which, so virtualness is known before
    //    any body is analysed
    for (std::size_t i = 0; i < units.size(); ++i) {
        cxx::ClassDecl *cd = dynamic_cast<cxx::ClassDecl*>(units[i]);
        if (cd) resolveOverrides(cd);
    }
    // 3b) whole-class rules, once virtualness is settled
    for (std::size_t i = 0; i < units.size(); ++i) {
        cxx::ClassDecl *cd = dynamic_cast<cxx::ClassDecl*>(units[i]);
        if (cd) checkClassInvariants(cd);
    }
    // 4) every top-level name, so functions may call each other in any order
    declareTopLevel(units);
    // 5) bodies
    for (std::size_t i = 0; i < units.size(); ++i) analyzeDecl(units[i]);
}

void SemanticAnalyzer::collectClasses(const std::vector<cc::Decl*> &units) {
    for (std::size_t i = 0; i < units.size(); ++i) {
        cxx::ClassDecl *cd = dynamic_cast<cxx::ClassDecl*>(units[i]);
        if (!cd) continue;
        if (findClass(cd->name)) {
            error(cd, "class '" + cd->name + "' is already defined");
            continue;
        }
        classes[cd->name] = cd;
        cc::Type *ct = new cxx::ClassType(cd->name);
        ownedTypes.push_back(ct);
        Symbol *s = new Symbol(SYM_Type, cd->name, cd, ct);
        if (!symbols.insert(cd->name, s)) {
            error(cd, "'" + cd->name + "' is already declared");
            delete s;
        }
    }
}

bool SemanticAnalyzer::sameParams(cc::Function *a, cc::Function *b) {
    if (a->params.size() != b->params.size()) return false;
    for (std::size_t i = 0; i < a->params.size(); ++i) {
        if (!sameDeclaredType(a->params[i]->type, b->params[i]->type)) return false;
    }
    // A const member function is a different function from its non-const twin;
    // declaring both is the ordinary container idiom, not a redeclaration.
    cxx::MethodDecl *ma = dynamic_cast<cxx::MethodDecl*>(a);
    cxx::MethodDecl *mb = dynamic_cast<cxx::MethodDecl*>(b);
    if (ma && mb && ma->isConstMethod != mb->isConstMethod) return false;
    return true;
}

// An out-of-line definition is a body looking for its declaration.  Moving the
// body onto the member inside the class means everything downstream -- layout,
// vtables, lowering -- sees one complete class and needs no special case.
void SemanticAnalyzer::attachOutOfLineDefinitions(const std::vector<cc::Decl*> &units) {
    for (std::size_t i = 0; i < units.size(); ++i) {
        cxx::MethodDecl *def = dynamic_cast<cxx::MethodDecl*>(units[i]);
        if (!def || def->ownerClass.empty()) continue;

        cxx::ClassDecl *cd = findClass(def->ownerClass);
        if (!cd) {
            error(def, "'" + def->ownerClass + "' is not a class");
            continue;
        }

        cxx::MethodDecl *decl = 0;
        for (std::size_t m = 0; m < cd->members.size(); ++m) {
            cxx::MethodDecl *cand = dynamic_cast<cxx::MethodDecl*>(cd->members[m]);
            if (!cand || cand->name != def->name) continue;
            if (cand->isConstructor != def->isConstructor) continue;
            if (cand->isDestructor != def->isDestructor) continue;
            if (!sameParams(cand, def)) continue;
            decl = cand;
            break;
        }
        if (!decl) {
            error(def, "no member '" + def->name + "' of class '" + def->ownerClass
                       + "' matches this definition");
            continue;
        }
        if (decl->body) {
            error(def, "'" + def->ownerClass + "::" + def->name + "' is already defined");
            continue;
        }
        if (!def->isConstructor && !def->isDestructor &&
            !sameType(decl->retType, def->retType)) {
            error(def, "'" + def->ownerClass + "::" + def->name
                       + "' is defined with a different return type");
            continue;
        }

        // Hand the body and the initialiser list over; the definition node
        // keeps neither, so nothing is deleted twice.
        decl->body = def->body;
        def->body = 0;
        decl->memberInits = def->memberInits;
        def->memberInits.clear();
        // The parameter NAMES come from the definition -- the declaration may
        // have had none.
        for (std::size_t p = 0; p < decl->params.size() && p < def->params.size(); ++p) {
            if (decl->params[p]->name.empty()) decl->params[p]->name = def->params[p]->name;
        }
    }
}

void SemanticAnalyzer::declareTopLevel(const std::vector<cc::Decl*> &units) {
    for (std::size_t i = 0; i < units.size(); ++i) {
        cc::Function *fn = dynamic_cast<cc::Function*>(units[i]);
        // An out-of-line definition is not a free function; its body has
        // already been given to the member it belongs to.
        cxx::MethodDecl *asMethod = dynamic_cast<cxx::MethodDecl*>(units[i]);
        if (asMethod && !asMethod->ownerClass.empty()) continue;
        if (fn) {
            checkNativeDeclaration(fn);
            std::vector<cc::Function*> &set = overloads[fn->name];
            // Two declarations with the same parameters are the same function;
            // a definition following a declaration is normal, two definitions
            // are not.
            bool duplicate = false;
            for (std::size_t k = 0; k < set.size(); ++k) {
                if (!sameParams(set[k], fn)) continue;
                // Two functions cannot differ only in what they return: the
                // call site has no way to say which one it wanted.
                if (!sameType(set[k]->retType, fn->retType)) {
                    error(fn, "'" + fn->name + "' cannot be overloaded on return type alone");
                } else if (set[k]->body && fn->body) {
                    error(fn, "function '" + fn->name + "' is already defined");
                }
                if (fn->body) set[k] = fn;      // the definition wins
                duplicate = true;
                break;
            }
            if (!duplicate) set.push_back(fn);

            // One symbol per name is enough for lookup; the call site consults
            // the overload set.
            if (!symbols.lookup(fn->name)) {
                symbols.insert(fn->name, new Symbol(SYM_Method, fn->name, fn, fn->retType));
            }
            continue;
        }
        cc::VarDecl *vd = dynamic_cast<cc::VarDecl*>(units[i]);
        if (vd) {
            Symbol *s = new Symbol(SYM_Var, vd->name, vd, vd->type);
            if (!symbols.insert(vd->name, s)) {
                error(vd, "variable '" + vd->name + "' is already declared");
                delete s;
            }
            continue;
        }
    }
}

void SemanticAnalyzer::analyzeDecl(cc::Decl *d) {
    if (!d) return;
    cxx::ClassDecl *cd = dynamic_cast<cxx::ClassDecl*>(d);
    if (cd) { analyzeClass(cd); return; }
    // MethodDecl IS a Function, so this one test covers both.
    cc::Function *fn = dynamic_cast<cc::Function*>(d);
    if (fn) { analyzeFunction(fn); return; }
    cc::VarDecl *vd = dynamic_cast<cc::VarDecl*>(d);
    if (vd) { analyzeVarDecl(vd, false); return; }
}

void SemanticAnalyzer::analyzeClass(cxx::ClassDecl *cd) {
    // A field may not shadow a base field: legal C++, but in a teaching subset
    // it is far more often a mistake than an intention.
    if (cd->base) {
        for (std::size_t i = 0; i < cd->members.size(); ++i) {
            cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
            if (!fd) continue;
            cxx::ClassDecl *owner = 0;
            cc::Decl *hidden = findMember(cd->base, fd->name, &owner);
            if (hidden && dynamic_cast<cxx::FieldDecl*>(hidden)) {
                diag.warning(fd->line, fd->col,
                             "field '" + fd->name + "' hides '" + owner->name
                             + "::" + fd->name + "'");
            }
        }
    }
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
        if (fd) {
            checkTypeIsKnown(fd->type, fd, "field '" + fd->name + "'");
            if (isVoid(fd->type)) error(fd, "field '" + fd->name + "' cannot have type void");
            continue;
        }
        cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(cd->members[i]);
        if (md) analyzeFunction(md);
    }
}

// One routine for a free function and a method: a method IS a function, and
// differs only by having a class scope pushed underneath its parameters.
void SemanticAnalyzer::analyzeFunction(cc::Function *fn) {
    cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(fn);

    checkTypeIsKnown(fn->retType, fn, "return type of '" + fn->name + "'");

    cc::Type *savedReturn = currentReturnType;
    const std::string savedClass = currentClass;
    cc::Function *const savedFunction = currentFunction;
    const bool savedCtorDtor = currentIsCtorOrDtor;
    const bool savedMethodConst = currentMethodIsConst;
    currentMethodIsConst = (md && md->isConstMethod);
    currentReturnType = fn->retType;
    currentClass = md ? md->ownerClass : std::string();
    currentFunction = fn;
    currentIsCtorOrDtor = md && (md->isConstructor || md->isDestructor);

    // A by-value object parameter is a copy the callee owns, so the callee
    // destroys it -- on every path out, which is what putting it in the body's
    // exit list buys.  Appended after the locals, so it goes last.
    if (fn->body) {
        for (std::size_t i = 0; i < fn->params.size(); ++i) {
            cc::VarDecl *p = fn->params[i];
            if (p && !p->name.empty() && hasDestructor(p->type)) {
                fn->body->destroyAtExit.push_back(p);
            }
        }
    }

    if (md) pushClassScope(findClass(md->ownerClass));

    symbols.pushScope();                        // parameters
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
        cc::VarDecl *p = fn->params[i];
        checkTypeIsKnown(p->type, p, "parameter '" + p->name + "'");
        if (isVoid(p->type)) error(p, "parameter '" + p->name + "' cannot have type void");
        if (p->name.empty()) continue;
        Symbol *s = new Symbol(SYM_Var, p->name, p, p->type);
        if (!symbols.insert(p->name, s)) {
            error(p, "duplicate parameter name '" + p->name + "'");
            delete s;
        }
    }

    // The initialiser list is checked INSIDE the parameter scope, because that
    // is where its arguments live:  Point(int a) : x(a) { }
    if (md && md->isConstructor) analyzeMemberInits(md, findClass(md->ownerClass));

    if (fn->body) analyzeBlock(fn->body);

    symbols.popScope();                         // parameters
    if (md) symbols.popScope();                 // class members

    currentReturnType = savedReturn;
    currentClass = savedClass;
    currentFunction = savedFunction;
    currentMethodIsConst = savedMethodConst;
    currentIsCtorOrDtor = savedCtorDtor;
}

// --- Statements ---

void SemanticAnalyzer::analyzeBlock(cc::CompoundStmt *block) {
    symbols.pushScope();
    std::vector<cc::VarDecl*> declared;
    for (std::size_t i = 0; i < block->body.size(); ++i) {
        analyzeStmt(block->body[i]);
        // Remember the class-typed locals, in the order they were declared.
        cc::DeclStmt *ds = dynamic_cast<cc::DeclStmt*>(block->body[i]);
        if (ds && ds->var) declared.push_back(ds->var);
    }
    recordScopeExitDestruction(block, declared);
    symbols.popScope();
}

// Exact reverse of construction.  Recording it here lets the lowering phase
// emit the calls on every path out without redoing the scoping.  With no
// exceptions, those paths are just: end of block, return, break.
void SemanticAnalyzer::recordScopeExitDestruction(cc::CompoundStmt *block,
                                                  const std::vector<cc::VarDecl*> &declared) {
    block->destroyAtExit.clear();
    for (std::size_t i = declared.size(); i > 0; --i) {
        cc::VarDecl *vd = declared[i - 1];
        if (hasDestructor(vd->type)) block->destroyAtExit.push_back(vd);
    }
}

void SemanticAnalyzer::analyzeVarDecl(cc::VarDecl *vd, bool declareIt) {
    if (!vd) return;
    // An unknown type makes every later check about this variable noise.
    const bool typeKnown = checkTypeIsKnown(vd->type, vd, "declaration of '" + vd->name + "'");
    if (isVoid(vd->type)) error(vd, "variable '" + vd->name + "' cannot have type void");

    // A const variable can never be assigned to, so its declaration is the
    // only chance it has to be given a value.
    if (vd->type && vd->type->isConst && !vd->init && !vd->hasCtorArgs) {
        // An object with a constructor is initialised BY it, so it needs no
        // initialiser of its own.  Only a scalar is left with nothing.
        cxx::ClassType *cct = dynamic_cast<cxx::ClassType*>(vd->type);
        cxx::ClassDecl *ccd = cct ? findClass(cct->className) : 0;
        if (!ccd || ccd->ctors.empty()) {
            error(vd, "const '" + vd->name + "' must be initialised");
        }
    }

    cc::Type *initType = 0;
    bool initIsLValue = false;
    if (vd->init) initType = analyzeExpr(vd->init, initIsLValue);

    // A reference binds a name to an existing object, so its initialiser must
    // denote one.  `int &s = 1;` has nothing to bind to.  This is what
    // analyzeExpr()'s isLValue result exists for.
    cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(vd->type);
    if (rt) {
        if (!vd->init) {
            error(vd, "reference '" + vd->name + "' must be initialised");
        } else if (initType && !initIsLValue) {
            error(vd, "cannot bind reference '" + vd->name + "' of type "
                      + describe(vd->type) + " to a non-lvalue initialiser");
        } else if (initType && !convertible(vd->init, initType, vd->type)) {
            // The whole reference type, not rt->base: convertible() has the
            // rule that binding a T& to a const object discards the const,
            // and it can only apply that rule if it can SEE the reference.
            // Passing the base type asked "does const int convert to int?",
            // which is true -- for a copy.  A reference is not a copy, so
            // `const int c; int &r = c;` slipped through and let a const
            // object be assigned to.  The argument path already passes the
            // parameter's own type and has always rejected this.
            error(vd, "cannot bind '" + describe(vd->type) + " " + vd->name
                      + "' to an initialiser of type " + describe(initType));
        }
    } else if (dynamic_cast<cc::ArrayType*>(vd->type)) {
        if (vd->init) error(vd, "an array cannot be initialised from an expression");
    } else if (vd->init && initType && typeKnown && !convertible(vd->init, initType, vd->type)) {
        error(vd, "cannot initialise '" + describe(vd->type) + " " + vd->name
                  + "' from an expression of type " + describe(initType));
    } else if (vd->init && initType) {
        warnIfNarrowing(vd->init, initType, vd->type, vd,
                        "the initialisation of '" + vd->name + "'");
    }

    // A class-typed object is CONSTRUCTED.  `Point p;` needs a constructor
    // taking no arguments whenever the class declares any at all, and
    // `Point q(1, 2)` needs one taking two.
    cxx::ClassDecl *cls = 0;
    {
        cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(stripReference(vd->type));
        if (ct) cls = findClass(ct->className);
    }
    if (cls && !dynamic_cast<cxx::ReferenceType*>(vd->type)) {
        for (std::size_t i = 0; i < vd->ctorArgs.size(); ++i) {
            bool lv = false;
            analyzeExpr(vd->ctorArgs[i], lv);
        }
        if (!vd->init) {
            vd->resolvedCtor = selectConstructor(cls, vd->ctorArgs, vd,
                              "the declaration of '" + vd->name + "'");
        }
    } else if (vd->hasCtorArgs) {
        error(vd, "'" + describe(vd->type) + "' is not a class type, so '" + vd->name
                  + "' cannot be constructed with arguments");
    }

    if (!declareIt) return;
    if (symbols.lookupLocal(vd->name)) {
        error(vd, "redeclaration of '" + vd->name + "' in the same scope");
        return;
    }
    Symbol *s = new Symbol(SYM_Var, vd->name, vd, vd->type);
    if (!symbols.insert(vd->name, s)) delete s;
}

void SemanticAnalyzer::analyzeStmt(cc::Stmt *s) {
    if (!s) return;

    cc::CompoundStmt *block = dynamic_cast<cc::CompoundStmt*>(s);
    if (block) { analyzeBlock(block); return; }

    cc::DeclStmt *ds = dynamic_cast<cc::DeclStmt*>(s);
    if (ds) { analyzeVarDecl(ds->var, true); return; }

    cc::ExprStmt *es = dynamic_cast<cc::ExprStmt*>(s);
    if (es) { bool lv = false; analyzeExpr(es->expr, lv); return; }

    cc::ReturnStmt *rs = dynamic_cast<cc::ReturnStmt*>(s);
    if (rs) {
        bool lv = false;
        cc::Type *got = rs->expr ? analyzeExpr(rs->expr, lv) : 0;
        if (currentIsCtorOrDtor) {
            if (rs->expr) error(rs, "a constructor or destructor cannot return a value");
            return;
        }
        if (!currentReturnType) return;
        if (!rs->expr) {
            if (!isVoid(currentReturnType)) {
                error(rs, "return with no value in a function returning "
                          + describe(currentReturnType));
            }
        } else if (isVoid(currentReturnType)) {
            error(rs, "return with a value in a function returning void");
        } else if (got && !convertible(rs->expr, got, currentReturnType)) {
            error(rs, "returning " + describe(got) + " from a function returning "
                      + describe(currentReturnType));
        } else if (isNonConstReferenceTo(currentReturnType) && isConstExpr(rs->expr)) {
            // Handing out a writable reference to something const gives the
            // caller a way round every check.  A const member function
            // returning T& to one of its own fields is the usual way in.
            error(rs, "cannot return a non-const reference to something const");
        } else if (got) {
            warnIfNarrowing(rs->expr, got, currentReturnType, rs, "this return");
        }
        return;
    }

    cc::IfStmt *is = dynamic_cast<cc::IfStmt*>(s);
    if (is) {
        bool lv = false;
        analyzeExpr(is->cond, lv);
        analyzeStmt(is->thenBranch);
        analyzeStmt(is->elseBranch);
        return;
    }

    cc::DoWhileStmt *dws = dynamic_cast<cc::DoWhileStmt*>(s);
    if (dws) {
        ++loopDepth;
        analyzeStmt(dws->body);
        --loopDepth;
        bool lv = false;
        analyzeExpr(dws->cond, lv);
        return;
    }

    cc::SwitchStmt *sw = dynamic_cast<cc::SwitchStmt*>(s);
    if (sw) { analyzeSwitch(sw); return; }

    if (dynamic_cast<cc::CaseStmt*>(s)) {
        if (switchDepth == 0) error(s, "a case label outside a switch");
        return;
    }

    cc::WhileStmt *ws = dynamic_cast<cc::WhileStmt*>(s);
    if (ws) {
        bool lv = false;
        analyzeExpr(ws->cond, lv);
        ++loopDepth;
        analyzeStmt(ws->body);
        --loopDepth;
        return;
    }

    cc::ForStmt *fs = dynamic_cast<cc::ForStmt*>(s);
    if (fs) {
        symbols.pushScope();            // the init declaration is scoped to the loop
        analyzeStmt(fs->init);
        bool lv = false;
        if (fs->cond) analyzeExpr(fs->cond, lv);
        if (fs->step) analyzeExpr(fs->step, lv);
        ++loopDepth;
        analyzeStmt(fs->body);
        --loopDepth;
        symbols.popScope();
        return;
    }

    if (dynamic_cast<cc::BreakStmt*>(s)) {
        if (loopDepth == 0 && switchDepth == 0) error(s, "'break' outside a loop or switch");
        return;
    }
    if (dynamic_cast<cc::ContinueStmt*>(s)) {
        if (loopDepth == 0) error(s, "'continue' outside a loop");
        return;
    }
}

// A switch needs an integer subject, and its labels must be distinct -- two
// cases with the same value would make one unreachable.
void SemanticAnalyzer::analyzeSwitch(cc::SwitchStmt *s) {
    bool lv = false;
    cc::Type *ct = analyzeExpr(s->cond, lv);
    cc::BuiltinKind k;
    if (ct && (!arithmeticKind(ct, k) || !cc::builtinIsInteger(k))) {
        error(s, "a switch needs an integer subject, not " + describe(ct));
    }

    std::vector<long> seen;
    bool sawDefault = false;
    if (s->body) {
        for (std::size_t i = 0; i < s->body->body.size(); ++i) {
            cc::CaseStmt *c = dynamic_cast<cc::CaseStmt*>(s->body->body[i]);
            if (!c) continue;
            if (c->isDefault) {
                if (sawDefault) error(c, "a switch may have only one 'default'");
                sawDefault = true;
                continue;
            }
            for (std::size_t j = 0; j < seen.size(); ++j) {
                if (seen[j] == c->value) {
                    error(c, "duplicate case label");
                    break;
                }
            }
            seen.push_back(c->value);
        }
    }

    ++switchDepth;
    analyzeStmt(s->body);
    --switchDepth;
}

// Exact match first, then a single viable one.  Real C++ ranks conversions in
// far more detail; arity plus exactness settles everything this subset can say.
std::vector<cc::Function*> SemanticAnalyzer::findMethods(cxx::ClassDecl *cd,
                                                         const std::string &name) {
    std::vector<cc::Function*> out;
    for (cxx::ClassDecl *c = cd; c; c = c->base) {
        for (std::size_t i = 0; i < c->members.size(); ++i) {
            cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(c->members[i]);
            if (!md || md->isConstructor || md->isDestructor) continue;
            if (md->name != name) continue;
            // A derived overload with the same parameters hides the base one.
            bool hidden = false;
            for (std::size_t k = 0; k < out.size(); ++k) {
                if (sameParams(out[k], md)) { hidden = true; break; }
            }
            if (!hidden) out.push_back(md);
        }
    }
    return out;
}

cc::Function *SemanticAnalyzer::resolveOverload(const std::vector<cc::Function*> &candidates,
                                                cc::CallExpr *call, const std::string &name) {
    if (candidates.empty()) return 0;
    if (candidates.size() == 1) return candidates[0];

    std::vector<cc::Type*> argTypes;
    for (std::size_t i = 0; i < call->args.size(); ++i) {
        bool lv = false;
        argTypes.push_back(analyzeExpr(call->args[i], lv));
    }

    std::vector<cc::Function*> exact, viable;
    for (std::size_t c = 0; c < candidates.size(); ++c) {
        cc::Function *f = candidates[c];
        if (f->params.size() != argTypes.size()) continue;
        bool allExact = true, allOk = true;
        for (std::size_t i = 0; i < argTypes.size(); ++i) {
            if (!argTypes[i]) continue;
            if (!exactForOverload(argTypes[i], f->params[i]->type)) allExact = false;
            if (!convertible(call->args[i], argTypes[i], f->params[i]->type)) allOk = false;
        }
        if (allExact) exact.push_back(f);
        if (allOk) viable.push_back(f);
    }

    if (exact.size() == 1) return exact[0];
    if (exact.size() > 1) {
        error(call, "ambiguous call to '" + name + "'");
        return exact[0];
    }
    if (viable.size() == 1) return viable[0];
    if (viable.size() > 1) {
        error(call, "ambiguous call to '" + name + "'");
        return viable[0];
    }
    error(call, "no overload of '" + name + "' takes these arguments");
    return 0;
}

// A function with no body whose NAME is a native's is bound to that native --
// that declaration is the whole of the binding, there being no linker in this
// pipeline to check it against a symbol.  Nothing checked it against anything,
// so a declaration that merely spelled the name right was accepted and the
// call went through with whatever the machine's own signature was:
//
//     int sqrt(int x);      called as sqrt(4)     -> 0
//     double pow(double a); called as pow(2.0)    -> 1
//
// Both compiled, ran, and printed a wrong number in silence.  The table knows
// how many arguments each native takes and whether it answers in floating
// point, so a declaration that disagrees is refused by name here.  What the
// table does not record is each parameter's type -- except that a native
// answering in floating point takes floating point throughout, which is true
// of every entry that does.
namespace {
// Floating in the type model's own terms; a non-builtin is not.
bool declaresFloating(cc::Type *t) {
    cc::BuiltinType *bt = dynamic_cast<cc::BuiltinType*>(t);
    return bt != 0 && cc::builtinIsFloating(bt->kind);
}
}

void SemanticAnalyzer::checkNativeDeclaration(cc::Function *fn) {
    if (!fn || fn->body) return;                       // a body is its own binding
    const NativeId id = nativeByName(fn->name);
    if (id == NAT_Count) return;                       // not a native at all

    const std::size_t want = static_cast<std::size_t>(nativeArgCount(id));
    if (fn->params.size() != want) {
        error(fn, "'" + fn->name + "' is built in and takes " + countText(want)
                  + " argument(s), not " + countText(fn->params.size()));
        return;
    }

    const bool wantsFloat = nativeReturnsFloat(id);
    const bool declaresFloat = declaresFloating(fn->retType);
    if (wantsFloat != declaresFloat) {
        error(fn, "'" + fn->name + "' is built in and returns "
                  + (wantsFloat ? "a floating-point value" : "an integer")
                  + ", not " + describe(fn->retType));
        return;
    }
    if (wantsFloat) {
        for (std::size_t i = 0; i < fn->params.size(); ++i) {
            if (!declaresFloating(fn->params[i]->type)) {
                error(fn, "'" + fn->name + "' is built in and takes floating-point "
                          "arguments; " + parameterText(fn, i) + " is "
                          + describe(fn->params[i]->type));
                return;
            }
        }
    }
}

// --- Calls ---

void SemanticAnalyzer::checkCallArgs(cc::CallExpr *call, cc::Function *fn) {
    if (call->args.size() != fn->params.size()) {
        error(call, "'" + fn->name + "' expects " + countText(fn->params.size())
                    + " argument(s) but got " + countText(call->args.size()));
        return;
    }
    for (std::size_t i = 0; i < call->args.size(); ++i) {
        bool lv = false;
        cc::Type *at = analyzeExpr(call->args[i], lv);
        cc::Type *pt = fn->params[i]->type;
        if (!at || !pt) continue;
        // a reference parameter needs an lvalue, same rule as a variable
        cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(pt);
        if (rt && !lv) {
            error(call->args[i], "argument to reference " + parameterText(fn, i)
                                 + " must be an lvalue");
            continue;
        }
        if (!convertible(call->args[i], at, pt)) {
            error(call->args[i], "argument " + describe(at) + " does not match "
                                 + parameterText(fn, i) + " of type " + describe(pt));
        } else {
            warnIfNarrowing(call->args[i], at, pt, call->args[i],
                            "the argument to " + parameterText(fn, i));
        }
    }
}

// --- Expressions: one walk over a tree that mixes cc:: and cxx:: nodes ---

// Records what the analysis concluded, so lowering never has to guess.
cc::Type *SemanticAnalyzer::analyzeExpr(cc::Expr *e, bool &isLValue) {
    cc::Type *t = analyzeExprImpl(e, isLValue);
    if (e) e->resolvedType = t;
    return t;
}

cc::Type *SemanticAnalyzer::analyzeExprImpl(cc::Expr *e, bool &isLValue) {
    isLValue = false;
    if (!e) return 0;

    // --- LAYER 2 forms, tested first: their operands are expressions ---

    cxx::MemberAccessExpr *ma = dynamic_cast<cxx::MemberAccessExpr*>(e);
    if (ma) {
        bool baseLV = false;
        cc::Type *baseType = analyzeExpr(ma->base, baseLV);
        if (!baseType) return 0;
        if (ma->isArrow) {
            cc::PointerType *pt = dynamic_cast<cc::PointerType*>(baseType);
            if (!pt) { error(ma, "'->' applied to " + describe(baseType) + ", which is not a pointer"); return 0; }
            baseType = pt->base;
        } else if (dynamic_cast<cc::PointerType*>(baseType)) {
            error(ma, "'.' applied to a pointer; did you mean '->'?");
            return 0;
        }
        cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(baseType);
        if (!ct) { error(ma, "member access on non-class type " + describe(baseType)); return 0; }
        cxx::ClassDecl *cd = findClass(ct->className);
        if (!cd) { error(ma, "unknown class '" + ct->className + "'"); return 0; }
        cxx::ClassDecl *owner = 0;
        cc::Decl *m = findMember(cd, ma->member, &owner);
        if (!m) { error(ma, "no member named '" + ma->member + "' in class '" + ct->className + "'"); return 0; }
        if (!memberIsAccessible(m, owner)) {
            error(ma, "'" + ma->member + "' is " + cxx::accessText(memberAccess(m))
                      + " in class '" + owner->name + "'");
        }
        isLValue = (dynamic_cast<cxx::FieldDecl*>(m) != 0);
        if (dynamic_cast<cc::ArrayType*>(stripReference(memberType(m)))) {
            isLValue = false;
            return decay(memberType(m));
        }
        return stripReference(memberType(m));
    }

    cxx::ThisExpr *te = dynamic_cast<cxx::ThisExpr*>(e);
    if (te) {
        if (currentClass.empty()) { error(te, "'this' used outside a member function"); return 0; }
        cxx::ClassType self(currentClass);
        // Inside a const member function `this` points at a const object, so
        // everything reached through it is const.
        self.isConst = currentMethodIsConst;
        return makePointerTo(&self);
    }

    // T(args) -- an object built in place.  Its type is T; it is not an lvalue,
    // because it has no name to be one under.
    cxx::TempExpr *tmp = dynamic_cast<cxx::TempExpr*>(e);
    if (tmp) {
        checkTypeIsKnown(tmp->type, tmp, "a temporary");
        cxx::ClassType *tct = dynamic_cast<cxx::ClassType*>(tmp->type);
        cxx::ClassDecl *tcd = tct ? findClass(tct->className) : 0;
        tmp->resolvedCtor = selectConstructor(tcd, tmp->args, tmp,
                                              "a temporary " + describe(tmp->type));
        isLValue = false;
        return tmp->type;
    }

    cxx::NewExpr *ne = dynamic_cast<cxx::NewExpr*>(e);
    if (ne) {
        checkTypeIsKnown(ne->allocType, ne, "'new' expression");
        if (ne->count) {
            bool lv = false;
            cc::Type *nt = analyzeExpr(ne->count, lv);
            cc::BuiltinKind nk;
            if (nt && (!arithmeticKind(nt, nk) || !cc::builtinIsInteger(nk))) {
                error(ne, "the element count of 'new[]' must be an integer, not "
                          + describe(nt));
            }
        }
        for (std::size_t i = 0; i < ne->args.size(); ++i) {
            bool lv = false;
            analyzeExpr(ne->args[i], lv);
        }
        // new allocates AND constructs, so the arguments must match a ctor.
        cxx::ClassType *nct = dynamic_cast<cxx::ClassType*>(ne->allocType);
        if (ne->count) {
            // Every element gets the same constructor and there is nowhere to
            // write arguments for each, so the array form takes none and the
            // elements are default-constructed -- as in C++.
            if (!ne->args.empty()) {
                error(ne, "'new " + describe(ne->allocType)
                          + "[]' cannot take constructor arguments; its elements are"
                            " default-constructed");
            }
            if (nct) {
                std::vector<cc::Expr*> none;
                ne->resolvedCtor = selectConstructor(findClass(nct->className), none, ne,
                                                     "'new " + nct->className + "[]'");
            }
        } else if (nct) {
            ne->resolvedCtor = selectConstructor(findClass(nct->className),
                                                 ne->args, ne, "'new'");
        } else if (!ne->args.empty()) {
            error(ne, "'new " + describe(ne->allocType) + "' cannot take constructor arguments");
        }
        // new T yields T*, and the T belongs to the AST node, so it is copied
        return makePointerTo(ne->allocType);
    }

    cxx::DeleteExpr *de = dynamic_cast<cxx::DeleteExpr*>(e);
    if (de) {
        bool lv = false;
        cc::Type *t = analyzeExpr(de->operand, lv);
        cc::PointerType *pt = t ? dynamic_cast<cc::PointerType*>(t) : 0;
        if (t && !pt) {
            error(de, std::string(de->isArray ? "'delete[]'" : "'delete'") + " applied to "
                      + describe(t) + ", which is not a pointer");
        } else if (pt) {
            // Only a virtual destructor is found through the vtable.
            cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(pt->base);
            cxx::ClassDecl *cd = ct ? findClass(ct->className) : 0;
            if (cd && cd->dtor && !cd->dtor->isVirtual) {
                bool anyDerived = false;
                std::map<std::string, cxx::ClassDecl*>::iterator it;
                for (it = classes.begin(); it != classes.end(); ++it) {
                    if (it->second != cd && isDerivedFrom(it->second, cd)) { anyDerived = true; break; }
                }
                if (anyDerived) {
                    diag.warning(de->line, de->col,
                                 "deleting through '" + describe(t) + "' with a non-virtual destructor");
                }
            }
        }
        return makeBuiltin(cc::BK_Void);
    }

    // --- LAYER 1 forms ---

    cc::IdentExpr *id = dynamic_cast<cc::IdentExpr*>(e);
    if (id) {
        Symbol *s = symbols.lookup(id->name);
        if (!s) { error(id, "undeclared identifier '" + id->name + "'"); return 0; }
        if (s->kind == SYM_Type) { error(id, "type '" + id->name + "' used as an expression"); return 0; }
        // May have come from a base class scope, so it needs the same access
        // check obj.member gets -- or omitting `this->` would defeat it.
        if (s->kind == SYM_Field || s->kind == SYM_Method) {
            cc::Decl *m = dynamic_cast<cc::Decl*>(s->decl);
            cxx::ClassDecl *owner = m ? ownerClassOf(m) : 0;
            if (owner && !memberIsAccessible(m, owner)) {
                error(id, "'" + id->name + "' is " + cxx::accessText(memberAccess(m))
                          + " in class '" + owner->name + "'");
                return 0;
            }
        }
        // An array does not decay to an lvalue: `a` cannot be assigned to.
        if (dynamic_cast<cc::ArrayType*>(stripReference(s->type))) {
            isLValue = false;
            return decay(s->type);
        }
        isLValue = (s->kind == SYM_Var || s->kind == SYM_Field);
        return stripReference(s->type);
    }

    if (dynamic_cast<cxx::BoolExpr*>(e)) return makeBool();
    if (cc::NumberExpr *n = dynamic_cast<cc::NumberExpr*>(e)) return makeBuiltin(n->kind);
    if (cc::FloatExpr *fl = dynamic_cast<cc::FloatExpr*>(e)) return makeBuiltin(fl->kind);
    if (dynamic_cast<cc::StringExpr*>(e)) {
        // A string literal is a pointer into read-only data.
        cc::Type *p = new cc::PointerType(new cc::BuiltinType(cc::BK_Char));
        ownedTypes.push_back(p);
        return p;
    }

    cc::CallExpr *call = dynamic_cast<cc::CallExpr*>(e);
    if (call) {
        // Resolve the callee to a function without treating it as a value.
        cc::Function *fn = 0;
        cc::IdentExpr *cid = dynamic_cast<cc::IdentExpr*>(call->callee);
        // A bare name inside a member function that names a method of this
        // class IS `this->name`, and must resolve through the same path: the
        // member path is where virtual dispatch, overload resolution by
        // argument type, and the const check all live.  Resolving it here
        // instead gave three different wrong answers.
        if (cid && !currentClass.empty() && !symbols.lookupLocal(cid->name)) {
            std::map<std::string, cxx::ClassDecl*>::iterator ci = classes.find(currentClass);
            cxx::ClassDecl *self = (ci == classes.end()) ? 0 : ci->second;
            if (self && !findMethods(self, cid->name).empty()) {
                cxx::MemberAccessExpr *rewritten =
                    new cxx::MemberAccessExpr(new cxx::ThisExpr(), cid->name, true);
                rewritten->line = cid->line;
                rewritten->col = cid->col;
                delete call->callee;
                call->callee = rewritten;
                cid = 0;
            }
        }

        if (cid) {
            std::map<std::string, std::vector<cc::Function*> >::iterator ov =
                overloads.find(cid->name);
            if (ov != overloads.end()) {
                fn = resolveOverload(ov->second, call, cid->name);
                if (!fn) return 0;
            } else {
                Symbol *s = symbols.lookup(cid->name);
                if (!s) { error(cid, "undeclared function '" + cid->name + "'"); return 0; }
                fn = dynamic_cast<cc::Function*>(s->decl);
                // obj(args) -- not a function at all, but an object whose
                // class overloads the call operator.
                if (!fn && s->type && isClassType(s->type)) {
                    if (cxx::MethodDecl *op = findCallOperator(s->type, call)) {
                        call->resolved = op;
                        checkCallArgs(call, op);
                        if (dynamic_cast<cxx::ReferenceType*>(op->retType)) isLValue = true;
                        return stripReference(op->retType);
                    }
                    error(call, "class '" + describe(s->type) + "' has no operator()");
                    return 0;
                }
                if (!fn) { error(call, "'" + cid->name + "' is not a function"); return 0; }
            }
        } else {
            cxx::MemberAccessExpr *cma = dynamic_cast<cxx::MemberAccessExpr*>(call->callee);
            if (!cma) { error(call, "expression is not callable"); return 0; }
            bool baseLV = false;
            cc::Type *baseType = analyzeExpr(cma->base, baseLV);
            if (!baseType) return 0;
            if (cma->isArrow) {
                cc::PointerType *pt = dynamic_cast<cc::PointerType*>(baseType);
                if (!pt) { error(cma, "'->' applied to a non-pointer"); return 0; }
                baseType = pt->base;
            }
            cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(baseType);
            if (!ct) { error(cma, "method call on non-class type " + describe(baseType)); return 0; }
            cxx::ClassDecl *cd = findClass(ct->className);
            cxx::ClassDecl *owner = 0;
            cc::Decl *m = cd ? findMember(cd, cma->member, &owner) : 0;
            if (!m) { error(cma, "no member named '" + cma->member + "' in class '" + ct->className + "'"); return 0; }
            if (!memberIsAccessible(m, owner)) {
                error(cma, "'" + cma->member + "' is " + cxx::accessText(memberAccess(m))
                           + " in class '" + owner->name + "'");
            }
            const std::vector<cc::Function*> cands = findMethods(cd, cma->member);
            if (cands.empty()) {
                error(cma, "'" + cma->member + "' is not a method");
                return 0;
            }
            fn = resolveOverload(cands, call, cma->member);
            if (!fn) return 0;

            // A const object may only be asked for a const method: anything
            // else is free to modify it.
            cxx::MethodDecl *chosen = dynamic_cast<cxx::MethodDecl*>(fn);
            if (chosen && !chosen->isConstMethod && objectIsConst(cma)) {
                error(cma, "'" + cma->member + "' is not const, and '"
                           + ct->className + "' here is");
            }
        }
        // Recorded so lowering does not resolve the call a second time.
        call->resolved = fn;
        checkCallArgs(call, fn);
        // A function returning T& names an object, so its result may be
        // assigned to.
        if (dynamic_cast<cxx::ReferenceType*>(fn->retType)) isLValue = true;
        return stripReference(fn->retType);
    }

    cc::UnaryExpr *ue = dynamic_cast<cc::UnaryExpr*>(e);
    if (ue) {
        bool operandLV = false;
        cc::Type *t = analyzeExpr(ue->operand, operandLV);
        if (!t) return 0;

        // ++ and -- read and write their operand, so it must be an object --
        // the same rule assignment obeys.
        if (cc::unaryOpIsIncDec(ue->op)) {
            if (!operandLV) {
                error(ue, std::string("'") + cc::unaryOpText(ue->op)
                          + "' needs an lvalue");
                return 0;
            }
            if (isConstExpr(ue->operand)) {
                error(ue, std::string("cannot apply '") + cc::unaryOpText(ue->op)
                          + "' to " + describe(t));
                return 0;
            }
            cc::BuiltinKind k;
            const bool arith = arithmeticKind(t, k);
            if (!arith && !dynamic_cast<cc::PointerType*>(stripReference(t))) {
                error(ue, std::string("'") + cc::unaryOpText(ue->op)
                          + "' needs an arithmetic or pointer operand, got " + describe(t));
                return 0;
            }
            return t;
        }

        switch (ue->op) {
        case cc::UN_Neg: {
            // An object has no sign of its own.  A class that wants one says
            // so, and then this expression is a call to what it said.
            if (isClassType(t)) {
                cc::Function *op = findUnaryMinusOperator(ue->operand, t, ue);
                if (!op) {
                    error(ue, "no 'operator-' for " + describe(t));
                    return 0;
                }
                ue->resolvedOperator = op;
                return op->retType;
            }
            cc::BuiltinKind k;
            if (!arithmeticKind(t, k)) {
                error(ue, "unary '-' needs an arithmetic type, got " + describe(t));
                return 0;
            }
            return makeBuiltin(promote(k));
        }
        case cc::UN_Not:
            // Anything with a zero can be tested; an object has none.
            if (dynamic_cast<cxx::ClassType*>(stripReference(t))) {
                error(ue, "'!' needs a scalar, got " + describe(t));
                return 0;
            }
            return makeBool();
        case cc::UN_Deref: {
            cc::PointerType *pt = dynamic_cast<cc::PointerType*>(decay(t));
            if (!pt) { error(ue, "unary '*' applied to " + describe(t) + ", which is not a pointer"); return 0; }
            // *p on a pointer-to-array yields an array, which decays in turn.
            // That is what makes g[1][2] reach the right element.
            if (dynamic_cast<cc::ArrayType*>(pt->base)) {
                isLValue = false;
                return decay(pt->base);
            }
            isLValue = true;
            return pt->base;
        }
        case cc::UN_AddrOf: {
            if (!operandLV) { error(ue, "cannot take the address of a non-lvalue"); return 0; }
            return makePointerTo(t);
        }
        default:
            break;
        }
        return 0;
    }

    cc::CastExpr *ce = dynamic_cast<cc::CastExpr*>(e);
    if (ce) {
        bool lv = false;
        cc::Type *from = analyzeExpr(ce->expr, lv);
        checkTypeIsKnown(ce->type, ce, "a cast");
        // A cast is the programmer overriding the conversion rules, so it is
        // not checked against them -- but it still must not invent a value out
        // of nothing.
        if (from && isVoid(ce->type)) return makeBuiltin(cc::BK_Void);
        return ce->type;
    }

    // a[i] -- a class overloads it, everything else is pointer arithmetic.
    cc::IndexExpr *ie = dynamic_cast<cc::IndexExpr*>(e);
    if (ie) {
        bool baseLV = false, idxLV = false;
        cc::Type *bt = analyzeExpr(ie->base, baseLV);
        cc::Type *it = analyzeExpr(ie->index, idxLV);
        if (!bt) return 0;

        if (isClassType(bt)) {
            cxx::ClassDecl *cd = findClass(
                dynamic_cast<cxx::ClassType*>(stripReference(bt))->className);
            if (cxx::MethodDecl *op = findIndexOperator(cd, ie->index, it,
                                                       isConstExpr(ie->base), ie)) {
                ie->resolvedOperator = op;
                // T& out means the subscript names an object, so it may be
                // assigned to -- which is the whole point of  s[i] = 'x'.
                cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(op->retType);
                isLValue = (rt != 0);
                return rt ? rt->base : op->retType;
            }
            error(ie, "class '" + cd->name + "' has no operator[]");
            return 0;
        }

        cc::BuiltinKind ik;
        if (it && (!arithmeticKind(it, ik) || !cc::builtinIsInteger(ik))) {
            error(ie, "a subscript must be an integer, not " + describe(it));
        }
        cc::PointerType *pt = dynamic_cast<cc::PointerType*>(decay(bt));
        if (!pt) { error(ie, describe(bt) + " cannot be subscripted"); return 0; }
        // An array of arrays yields an array, which decays in turn -- that is
        // what makes g[1][2] reach the right element.
        if (dynamic_cast<cc::ArrayType*>(pt->base)) {
            isLValue = false;
            return decay(pt->base);
        }
        isLValue = true;
        return pt->base;
    }

    cc::BinaryExpr *be = dynamic_cast<cc::BinaryExpr*>(e);
    if (be) {
        bool lL = false, lR = false;
        cc::Type *lt = analyzeExpr(be->lhs, lL);
        cc::Type *rt = analyzeExpr(be->rhs, lR);
        if (!lt || !rt) return 0;       // the operand already reported its error

        // An operand that is an object sends the whole expression looking for
        // a member named for the operator.  Found, this is a call, and every
        // rule below belongs to the builtin operators it is not.
        if (cc::Function *op = findOperator(be->lhs, lt, be->rhs, rt, be->op, be)) {
            be->resolvedOperator = op;
            isLValue = false;
            return op->retType;
        }

        if (cc::binaryOpIsAssignment(be->op)) {
            // The other half of lvalue-ness: only an object can be assigned to.
            if (!lL) { error(be, "left side of assignment is not an lvalue"); return 0; }
            if (isConstExpr(be->lhs)) {
                // The const may be on the type itself, or inherited from the
                // object it belongs to -- and saying which is the difference
                // between a useful message and a puzzling one.
                cc::Type *slt = stripReference(lt);
                if (slt && slt->isConst) error(be, "cannot assign to " + describe(lt));
                else                     error(be, "cannot modify a const object");
                return 0;
            }

            // A compound assignment on an object needs an operator of its own.
            // Without one there is nothing sensible to do -- and doing the
            // arithmetic on the object's bytes, which is what fell out before,
            // is not an approximation of one.
            if (be->op != cc::BIN_Assign && (isClassType(lt) || isClassType(rt))) {
                error(be, "no 'operator" + std::string(cc::binaryOpText(be->op))
                          + "' for " + describe(lt));
                return 0;
            }
            if (!convertible(be->rhs, rt, lt)) {
                error(be, "cannot assign " + describe(rt) + " to " + describe(lt));
                return 0;
            }
            warnIfNarrowing(be->rhs, rt, lt, be, "this assignment");
            isLValue = true;            // in C++ an assignment yields an lvalue
            return lt;
        }


        if (cc::binaryOpIsLogical(be->op)) {
            // && and || only ask whether each side is true, and anything that
            // converts to bool can answer that -- a number, or a pointer.
            if (!isTestable(lt) || !isTestable(rt)) {
                error(be, std::string("'") + cc::binaryOpText(be->op)
                          + "' needs testable operands, not "
                          + describe(lt) + " and " + describe(rt));
                return 0;
            }
            return makeBool();
        }

        // Shift is not the usual arithmetic conversion: the result has the
        // LEFT operand's type, and the right one only says how far.
        if (be->op == cc::BIN_Shl || be->op == cc::BIN_Shr) {
            cc::BuiltinKind kl, kr;
            if (!arithmeticKind(lt, kl) || !cc::builtinIsInteger(kl) ||
                !arithmeticKind(rt, kr) || !cc::builtinIsInteger(kr)) {
                error(be, std::string("'") + cc::binaryOpText(be->op)
                          + "' needs integer operands, not "
                          + describe(lt) + " and " + describe(rt));
                return 0;
            }
            return makeBuiltin(promote(kl));
        }

        if (cc::binaryOpIsComparison(be->op)) {
            cc::BuiltinKind kl, kr;
            const bool bothArith = arithmeticKind(lt, kl) && arithmeticKind(rt, kr);
            // Two pointers compare when one converts to the other, and a
            // pointer compares with the literal 0 -- which is how a linked
            // list finds its end.
            const bool ptrCompare =
                (dynamic_cast<cc::PointerType*>(stripReference(decay(lt))) ||
                 dynamic_cast<cc::PointerType*>(stripReference(decay(rt)))) &&
                (convertible(be->rhs, rt, lt) || convertible(be->lhs, lt, rt));
            if (!bothArith && !ptrCompare) {
                error(be, std::string("cannot compare ") + describe(lt) + " with " + describe(rt));
                return 0;
            }
            // In C++ a comparison yields bool.
            return makeBool();
        }

        // Pointer arithmetic: p + n and p - n step by whole objects, so the
        // result is still a pointer.  p - q counts the objects between them.
        // Arrays decay first, which is what lets a + 1 work at all.
        lt = decay(lt);
        rt = decay(rt);
        cc::PointerType *pl = dynamic_cast<cc::PointerType*>(stripReference(lt));
        cc::PointerType *pr = dynamic_cast<cc::PointerType*>(stripReference(rt));
        if (pl || pr) {
            cc::BuiltinKind ik;
            const bool lIsInt = builtinKindOf(lt, ik) && cc::builtinIsInteger(ik);
            const bool rIsInt = builtinKindOf(rt, ik) && cc::builtinIsInteger(ik);
            if (be->op == cc::BIN_Add && pl && rIsInt) return lt;
            if (be->op == cc::BIN_Add && pr && lIsInt) return rt;
            if (be->op == cc::BIN_Sub && pl && rIsInt) return lt;
            if (be->op == cc::BIN_Sub && pl && pr && sameType(lt, rt)) {
                return makeBuiltin(cc::BK_Long);
            }
            error(be, std::string("invalid pointer arithmetic: ") + describe(lt)
                      + " " + cc::binaryOpText(be->op) + " " + describe(rt));
            return 0;
        }

        // Arithmetic: both operands meet in a common type, and that is the
        // type of the result.  % is integers only.
        cc::BuiltinKind kl, kr;
        if (arithmeticKind(lt, kl) && arithmeticKind(rt, kr)) {
            if (be->op == cc::BIN_Mod &&
                (cc::builtinIsFloating(kl) || cc::builtinIsFloating(kr))) {
                error(be, "'%' needs integer operands, not "
                          + describe(lt) + " and " + describe(rt));
                return 0;
            }
            return makeBuiltin(usualArithmetic(kl, kr));
        }
        error(be, std::string("invalid operands to '") + cc::binaryOpText(be->op)
                  + "': " + describe(lt) + " and " + describe(rt));
        return 0;
    }

    error(e, "unhandled expression kind in the semantic analyzer");
    return 0;
}

// ---------- Layout.cpp ----------
// Layout.cpp
//
// C++98 only.  See Layout.h for the three rules single inheritance buys.


#include <cstddef>
#include <iostream>
#include <sstream>

Layout::Layout(Diagnostics &d)
    : diag(d), reportedOversize(false), memoryLimit(MachineMemory), classIndex(0) {}

int Layout::roundUp(int value, int alignment) {
    if (alignment <= 1) return value;
    const int rem = value % alignment;
    return rem ? value + (alignment - rem) : value;
}

const ClassLayout *Layout::forClass(const std::string &name) const {
    std::map<std::string, ClassLayout>::const_iterator it = layouts.find(name);
    return (it == layouts.end()) ? 0 : &it->second;
}

int Layout::sizeOf(cc::Type *t) const {
    if (!t) return 0;
    // A reference is a pointer at runtime; that is the whole of its lowering.
    cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(t);
    if (rt) return PointerSize;
    if (dynamic_cast<cc::PointerType*>(t)) return PointerSize;
    // An array is its elements, laid end to end -- computed in the machine's
    // own word, not in int.  `int a[700000000];` overflowed this multiply,
    // and a wrapped size is not a diagnostic: the array was laid out at some
    // wrong size, the program compiled, it RAN, and it reported success. An
    // accepted program that means nothing is the worst answer available.
    //
    // The size is also what the whole object will occupy, so this is where it
    // can be compared against the memory the machine actually has, while there
    // is still a declaration to point at.  The VM checks the same thing when
    // it loads an image, but by then CodeGen has already materialised the
    // bytes in the host: `int a[300000000];` reached a gigabyte of host memory
    // before anything objected, which on a phone is not an error message.
    if (cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t)) {
        const vmword elem  = sizeOf(at->element);
        const vmword count = static_cast<vmword>(at->count);
        const vmword bytes = count * elem;
        if (elem > 0 && (count > memoryLimit / elem || bytes > memoryLimit)) {
            if (!reportedOversize) {
                reportedOversize = true;
                std::ostringstream ss;
                ss << "an array of " << count << " x " << elem
                   << " bytes does not fit in this machine's " << (memoryLimit / 1024 / 1024)
                   << "MB of memory";
                diag.error(at->line, at->col, ss.str());
            }
            return 0;
        }
        return static_cast<int>(bytes);
    }
    // bool is the C++ layer's, so the C layer's size table does not name it.
    if (dynamic_cast<cxx::BoolType*>(t)) return 1;
    // The type model owns every builtin's size; Layout does not restate them.
    cc::BuiltinType *bt = dynamic_cast<cc::BuiltinType*>(t);
    if (bt) return cc::builtinSize(bt->kind);
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    if (ct) {
        const ClassLayout *cl = forClass(ct->className);
        return cl ? cl->size : 0;
    }
    return 0;
}

int Layout::alignOf(cc::Type *t) const {
    // An array aligns like one element, however many it holds.
    if (cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t)) return alignOf(at->element);
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    if (ct) {
        const ClassLayout *cl = forClass(ct->className);
        return cl ? cl->align : 1;
    }
    const int s = sizeOf(t);
    return s > 0 ? s : 1;                       // scalars align to their size
}

void Layout::computeAll(const std::map<std::string, cxx::ClassDecl*> &classes) {
    classIndex = &classes;
    std::map<std::string, cxx::ClassDecl*>::const_iterator it;
    for (it = classes.begin(); it != classes.end(); ++it) {
        computeFor(it->second);
    }
    classIndex = 0;
}

// A field's class, if it has one.  An array of objects counts: its element
// needs a size before the array does.
cxx::ClassDecl *Layout::classDeclOf(cc::Type *t) const {
    while (cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t)) t = at->element;
    cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t);
    if (!ct || !classIndex) return 0;
    std::map<std::string, cxx::ClassDecl*>::const_iterator it = classIndex->find(ct->className);
    return (it == classIndex->end()) ? 0 : it->second;
}

void Layout::computeFor(cxx::ClassDecl *cd) {
    if (!cd) return;
    if (layouts.find(cd->name) != layouts.end()) return;    // already done

    // A class cannot contain itself, directly or through a chain of members --
    // that has no finite size.  Caught here because this is where the chain is
    // walked.
    if (inProgress.find(cd->name) != inProgress.end()) {
        diag.error(cd->line, cd->col,
                   "class '" + cd->name + "' cannot contain itself");
        return;
    }
    inProgress.insert(cd->name);

    // A base must be laid out first: the derived layout starts as a copy of it.
    // The semantic pass has already broken any cycle, so this recursion ends.
    if (cd->base) computeFor(cd->base);

    // So must any class a field is made of -- otherwise its size depends on
    // whether its name happened to sort before this one.
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
        if (fd) computeFor(classDeclOf(fd->type));
    }
    const ClassLayout *baseLayout = cd->base ? forClass(cd->base->name) : 0;

    ClassLayout cl;
    cl.name = cd->name;

    // Needed if this class declares a virtual, or its base already had one.
    bool declaresVirtual = false;
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(cd->members[i]);
        if (md && md->isVirtual) { declaresVirtual = true; break; }
    }
    cl.hasVPtr = declaresVirtual || (baseLayout && baseLayout->hasVPtr);

    int offset = 0;
    cl.align = 1;

    if (baseLayout) {
        // The base subobject sits at offset 0, so its fields keep their offsets.
        cl.fields = baseLayout->fields;
        offset = baseLayout->size;
        cl.align = baseLayout->align;
        // That would need the vptr in front and the base pushed down.
        // Rejecting it keeps every upcast a no-op.
        if (cl.hasVPtr && !baseLayout->hasVPtr) {
            diag.error(cd->line, cd->col,
                       "class '" + cd->name + "' adds a virtual function but base '"
                       + cd->base->name + "' has none");
            cl.hasVPtr = false;
        }
    } else if (cl.hasVPtr) {
        offset = PointerSize;                   // the vptr occupies offset 0
        cl.align = PointerSize;
    }

    cl.firstOwnField = static_cast<int>(cl.fields.size());

    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
        if (!fd) continue;
        const int fsize = sizeOf(fd->type);
        const int falign = alignOf(fd->type);
        if (fsize == 0) {
            // Unless sizeOf has already said why, in which case this is the
            // same mistake a second time.  One mistake costs one line.
            if (!reportedOversize) {
                diag.error(fd->line, fd->col,
                           "field '" + fd->name + "' has no size in class '" + cd->name + "'");
            }
            continue;
        }
        offset = roundUp(offset, falign);
        FieldLayout f;
        f.name = fd->name;
        f.ownerClass = cd->name;
        f.type = fd->type;
        f.offset = offset;
        f.size = fsize;
        cl.fields.push_back(f);
        offset += fsize;
        if (falign > cl.align) cl.align = falign;
    }

    // Rounded up so an array of them stays aligned.
    cl.size = roundUp(offset, cl.align);
    if (cl.size == 0) cl.size = 1;              // an empty class still occupies a byte

    // --- construction and destruction order -------------------------------
    // Base first, then this class's own fields in DECLARATION order -- not the
    // order an initialiser list is written in, hence that warning -- then the
    // constructor body, which can now rely on everything being in place.
    cl.hasCtor = !cd->ctors.empty();
    if (cd->base) cl.constructionPlan.push_back(InitStep(InitStep::StepBase, cd->base->name));
    // AFTER the base and BEFORE this class's fields: that is what makes a
    // virtual call inside a constructor reach THIS class's override.
    if (cl.hasVPtr) cl.constructionPlan.push_back(InitStep(InitStep::StepVPtr, cd->name));
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        cxx::FieldDecl *fd = dynamic_cast<cxx::FieldDecl*>(cd->members[i]);
        if (fd) cl.constructionPlan.push_back(InitStep(InitStep::StepField, fd->name));
    }
    // Without a constructor there is nothing to run after the fields.
    if (cl.hasCtor || cd->dtor) cl.constructionPlan.push_back(InitStep(InitStep::StepBody, cd->name));

    // Exact reverse: body first (it may still need the members), then members
    // backwards, then the base -- which outlives the derived part.
    for (std::size_t i = cl.constructionPlan.size(); i > 0; --i) {
        cl.destructionPlan.push_back(cl.constructionPlan[i - 1]);
    }

    cl.hasDtor = (cd->dtor != 0) || (baseLayout && baseLayout->hasDtor);

    // --- the vtable -------------------------------------------------------
    // Start from the base's, so a slot means the same thing down the chain.
    // An override REPLACES its slot; a new virtual APPENDS one.
    if (baseLayout) cl.vtable = baseLayout->vtable;
    if (cl.hasVPtr) {
        for (std::size_t i = 0; i < cd->members.size(); ++i) {
            cxx::MethodDecl *md = dynamic_cast<cxx::MethodDecl*>(cd->members[i]);
            if (!md || !md->isVirtual) continue;
            bool replaced = false;
            if (md->overrides) {
                for (std::size_t s = 0; s < cl.vtable.size(); ++s) {
                    if (cl.vtable[s] == md->overrides) { cl.vtable[s] = md; replaced = true; break; }
                }
            }
            if (!replaced) cl.vtable.push_back(md);
        }
    }

    layouts[cd->name] = cl;
    inProgress.erase(cd->name);
}

void Layout::print() const {
    std::map<std::string, ClassLayout>::const_iterator it;
    for (it = layouts.begin(); it != layouts.end(); ++it) {
        const ClassLayout &cl = it->second;
        std::cout << "class " << cl.name
                  << "  size=" << cl.size
                  << " align=" << cl.align
                  << (cl.hasVPtr ? "  [has vptr]" : "")
                  << std::endl;
        if (cl.hasVPtr) {
            std::cout << "     +0  __vptr (" << PointerSize << " bytes)" << std::endl;
        }
        for (std::size_t i = 0; i < cl.fields.size(); ++i) {
            const FieldLayout &f = cl.fields[i];
            std::cout << "    +" << f.offset << "  " << f.name
                      << " (" << f.size << " bytes)";
            if (f.ownerClass != cl.name) std::cout << "  inherited from " << f.ownerClass;
            std::cout << std::endl;
        }
        for (std::size_t s = 0; s < cl.vtable.size(); ++s) {
            cxx::MethodDecl *m = cl.vtable[s];
            std::cout << "    vtable[" << s << "] = " << m->ownerClass << "::" << m->name << std::endl;
        }
        // Only worth showing when the order can matter.
        if (cl.hasCtor || cl.hasDtor || cl.hasVPtr) {
            std::cout << "    construct:";
            for (std::size_t i = 0; i < cl.constructionPlan.size(); ++i) {
                const InitStep &st = cl.constructionPlan[i];
                std::cout << " " << (i ? "-> " : "");
                switch (st.kind) {
                case InitStep::StepBase:  std::cout << "base " << st.name; break;
                case InitStep::StepVPtr:  std::cout << "set vptr"; break;
                case InitStep::StepField: std::cout << "field " << st.name; break;
                case InitStep::StepBody:  std::cout << st.name << "() body"; break;
                }
            }
            std::cout << std::endl;
            std::cout << "    destroy:  ";
            for (std::size_t i = 0; i < cl.destructionPlan.size(); ++i) {
                const InitStep &st = cl.destructionPlan[i];
                std::cout << " " << (i ? "-> " : "");
                switch (st.kind) {
                case InitStep::StepBase:  std::cout << "base " << st.name; break;
                case InitStep::StepVPtr:  std::cout << "reset vptr"; break;
                case InitStep::StepField: std::cout << "field " << st.name; break;
                case InitStep::StepBody:  std::cout << "~" << st.name << "() body"; break;
                }
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }
}

// ---------- IR.cpp ----------
// IR.cpp
//
// C++98 only.  See IR.h for why this layer has no cxx:: counterpart.


#include <cstddef>
#include <iostream>
#include <sstream>


const char *irOpName(IROp op) {
    switch (op) {
    case IR_Const:        return "const";
    case IR_FConst:       return "fconst";
    case IR_StringAddr:   return "straddr";
    case IR_Move:         return "move";
    case IR_Add:          return "add";
    case IR_Sub:          return "sub";
    case IR_Mul:          return "mul";
    case IR_Div:          return "div";
    case IR_Mod:          return "mod";
    case IR_UDiv:         return "udiv";
    case IR_UMod:         return "umod";
    case IR_FAdd:         return "fadd";
    case IR_FSub:         return "fsub";
    case IR_FMul:         return "fmul";
    case IR_FDiv:         return "fdiv";
    case IR_FNeg:         return "fneg";
    case IR_Shl:          return "shl";
    case IR_Shr:          return "shr";
    case IR_UShr:         return "ushr";
    case IR_Neg:          return "neg";
    case IR_LogicalNot:   return "not";
    case IR_CmpEQ:        return "cmp.eq";
    case IR_CmpNE:        return "cmp.ne";
    case IR_CmpLT:        return "cmp.lt";
    case IR_CmpGT:        return "cmp.gt";
    case IR_CmpLE:        return "cmp.le";
    case IR_CmpGE:        return "cmp.ge";
    case IR_UCmpLT:       return "ucmp.lt";
    case IR_UCmpGT:       return "ucmp.gt";
    case IR_UCmpLE:       return "ucmp.le";
    case IR_UCmpGE:       return "ucmp.ge";
    case IR_FCmpEQ:       return "fcmp.eq";
    case IR_FCmpNE:       return "fcmp.ne";
    case IR_FCmpLT:       return "fcmp.lt";
    case IR_FCmpGT:       return "fcmp.gt";
    case IR_FCmpLE:       return "fcmp.le";
    case IR_FCmpGE:       return "fcmp.ge";
    case IR_IntToFloat:   return "itof";
    case IR_FloatToInt:   return "ftoi";
    case IR_FloatResize:  return "fresize";
    case IR_IntResize:    return "iresize";
    case IR_LocalAddr:    return "local";
    case IR_GlobalAddr:   return "global";
    case IR_FieldAddr:    return "field";
    case IR_FuncAddr:     return "funcaddr";
    case IR_Load:         return "load";
    case IR_Store:        return "store";
    case IR_MemCopy:      return "memcpy";
    case IR_Call:         return "call";
    case IR_CallIndirect: return "call.ind";
    case IR_VCallTarget:  return "vtable";
    case IR_Alloc:        return "alloc";
    case IR_ArrayCount:   return "arraycount";
    case IR_Free:         return "free";
    case IR_Label:        return "label";
    case IR_Jump:         return "jump";
    case IR_BranchZero:   return "brz";
    case IR_BranchNZ:     return "brnz";
    case IR_Return:       return "ret";
    }
    return "?";
}

// --- name mangling ----------------------------------------------------

// A short code per type: i d c s l f v, P for a pointer, R for a reference,
// and a class by name.  Readable in a dump, and unique.
static std::string typeCode(cc::Type *t) {
    if (!t) return "v";
    // The mangler and the semantic pass must mean the same thing by
    // "signature", or two distinct overloads end up sharing one symbol.
    // 'K' is const, as in the Itanium ABI this borrows its spirit from.
    const std::string k = t->isConst ? "K" : "";
    if (cc::PointerType *pt = dynamic_cast<cc::PointerType*>(t)) return k + "P" + typeCode(pt->base);
    if (cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t)) return k + "P" + typeCode(at->element);
    if (cc::BuiltinType *bt = dynamic_cast<cc::BuiltinType*>(t)) {
        switch (bt->kind) {
        case cc::BK_Void:   return k + "v";
        case cc::BK_Char:   return k + "c";
        case cc::BK_SChar:  return k + "a";
        case cc::BK_UChar:  return k + "h";
        case cc::BK_Short:  return k + "s";
        case cc::BK_UShort: return k + "t";
        case cc::BK_Int:    return k + "i";
        case cc::BK_UInt:   return k + "j";
        case cc::BK_Long:   return k + "l";
        case cc::BK_ULong:  return k + "m";
        case cc::BK_Float:  return k + "f";
        case cc::BK_Double: return k + "d";
        }
    }
    if (dynamic_cast<cxx::BoolType*>(t)) return k + "b";
    if (cxx::ReferenceType *rt = dynamic_cast<cxx::ReferenceType*>(t)) {
        return "R" + typeCode(rt->base);
    }
    if (cxx::ClassType *ct = dynamic_cast<cxx::ClassType*>(t)) {
        std::ostringstream ss;
        ss << k << ct->className.size() << ct->className;
        return ss.str();
    }
    return "X";
}

std::string mangleSignature(const std::vector<cc::VarDecl*> &params) {
    if (params.empty()) return "v";
    std::string out;
    for (std::size_t i = 0; i < params.size(); ++i) {
        out += typeCode(params[i]->type);
    }
    return out;
}

std::string mangleOverload(const std::string &className, const std::string &name,
                           const std::vector<cc::VarDecl*> &params, bool isConstMethod) {
    // A const member function is a different function from its non-const twin,
    // so it needs a different symbol -- otherwise the pair a container declares
    // would collide.
    return mangleFunction(className, name) + "$" + mangleSignature(params)
         + (isConstMethod ? "K" : "");
}

std::string mangleFunction(const std::string &className, const std::string &name) {
    if (className.empty()) return name;
    return className + "__" + name;
}

// Constructors overload by argument count, so the count distinguishes them.
std::string mangleConstructor(const std::string &className,
                              const std::vector<cc::VarDecl*> &params) {
    // By SIGNATURE, not by argument count: P(int,int) and P(double,double) are
    // two constructors, and encoding only the arity gave them one symbol.
    return className + "__ctor$" + mangleSignature(params);
}

std::string mangleDestructor(const std::string &className) {
    return className + "__dtor";
}

std::string mangleVTable(const std::string &className) {
    return className + "__vtable";
}

// --- the builder ------------------------------------------------------

int IRFunction::addLocal(const std::string &n, int size, bool isParam, bool isFloat,
                         bool isObject) {
    const int slot = static_cast<int>(locals.size());
    locals.push_back(IRLocal(n, slot, size, isParam, isFloat, isObject));
    return slot;
}

bool IRFunction::endsWithTerminator() const {
    if (code.empty()) return false;
    const IROp last = code.back().op;
    return last == IR_Return || last == IR_Jump;
}

IRReg IRFunction::emitConst(long value, int line) {
    IRInstr i(IR_Const);
    i.dest = newReg();
    i.imm = value;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitFConst(double value, int line) {
    IRInstr i(IR_FConst);
    i.dest = newReg();
    i.fimm = value;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitStringAddr(const std::string &sym, int line) {
    IRInstr i(IR_StringAddr);
    i.dest = newReg();
    i.sym = sym;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitConvert(IROp op, IRReg a, long imm, IRReg signFlag, int line) {
    IRInstr i(op);
    i.dest = newReg();
    i.a = a;
    i.b = signFlag;
    i.imm = imm;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitUnary(IROp op, IRReg a, int line) {
    IRInstr i(op);
    i.dest = newReg();
    i.a = a;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitBinary(IROp op, IRReg a, IRReg b, int line) {
    IRInstr i(op);
    i.dest = newReg();
    i.a = a;
    i.b = b;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitLocalAddr(int slot, int line) {
    IRInstr i(IR_LocalAddr);
    i.dest = newReg();
    i.imm = slot;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitGlobalAddr(const std::string &sym, int line) {
    IRInstr i(IR_GlobalAddr);
    i.dest = newReg();
    i.sym = sym;
    i.line = line;
    push(i);
    return i.dest;
}

// Offset zero still emits, so a dump shows every member access as one step.
IRReg IRFunction::emitFieldAddr(IRReg base, long offset, int line) {
    IRInstr i(IR_FieldAddr);
    i.dest = newReg();
    i.a = base;
    i.imm = offset;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitFuncAddr(const std::string &sym, int line) {
    IRInstr i(IR_FuncAddr);
    i.dest = newReg();
    i.sym = sym;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitLoad(IRReg addr, int size, bool isFloat, int line) {
    IRInstr i(IR_Load);
    i.dest = newReg();
    i.a = addr;
    i.imm = size;
    i.isFloat = isFloat;
    i.line = line;
    push(i);
    return i.dest;
}

void IRFunction::emitStore(IRReg addr, IRReg value, int size, bool isFloat, int line) {
    IRInstr i(IR_Store);
    i.a = addr;
    i.b = value;
    i.imm = size;
    i.isFloat = isFloat;
    i.line = line;
    push(i);
}

void IRFunction::emitMemCopy(IRReg dst, IRReg src, int size, int line) {
    IRInstr i(IR_MemCopy);
    i.a = dst;
    i.b = src;
    i.imm = size;
    i.line = line;
    push(i);
}

IRReg IRFunction::emitCall(const std::string &sym, const std::vector<IRReg> &args,
                           bool wantsResult, int line) {
    IRInstr i(IR_Call);
    i.dest = wantsResult ? newReg() : IR_NoReg;
    i.sym = sym;
    i.args = args;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitCallIndirect(IRReg target, const std::vector<IRReg> &args,
                                   bool wantsResult, int line) {
    IRInstr i(IR_CallIndirect);
    i.dest = wantsResult ? newReg() : IR_NoReg;
    i.a = target;
    i.args = args;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitVCallTarget(IRReg object, long slot, int line) {
    IRInstr i(IR_VCallTarget);
    i.dest = newReg();
    i.a = object;
    i.imm = slot;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitAlloc(long bytes, int line) {
    IRInstr i(IR_Alloc);
    i.dest = newReg();
    i.imm = bytes;
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitAllocN(IRReg bytes, IRReg count, int line) {
    IRInstr i(IR_Alloc);
    i.dest = newReg();
    i.a = bytes;                // a register here means "the size is this"
    i.b = count;                // and a register here means "and it is new[]"
    i.line = line;
    push(i);
    return i.dest;
}

IRReg IRFunction::emitArrayCount(IRReg ptr, int line) {
    IRInstr i(IR_ArrayCount);
    i.dest = newReg();
    i.a = ptr;
    i.line = line;
    push(i);
    return i.dest;
}

void IRFunction::emitFree(IRReg ptr, int line, bool isArray) {
    IRInstr i(IR_Free);
    i.a = ptr;
    i.isArray = isArray;
    i.line = line;
    push(i);
}

void IRFunction::emitLabel(int label) {
    IRInstr i(IR_Label);
    i.imm = label;
    push(i);
}

void IRFunction::emitJump(int label, int line) {
    IRInstr i(IR_Jump);
    i.imm = label;
    i.line = line;
    push(i);
}

void IRFunction::emitBranchZero(IRReg cond, int label, int line) {
    IRInstr i(IR_BranchZero);
    i.a = cond;
    i.imm = label;
    i.line = line;
    push(i);
}

void IRFunction::emitBranchNZ(IRReg cond, int label, int line) {
    IRInstr i(IR_BranchNZ);
    i.a = cond;
    i.imm = label;
    i.line = line;
    push(i);
}

void IRFunction::emitReturn(IRReg value, int line) {
    IRInstr i(IR_Return);
    i.a = value;
    i.line = line;
    push(i);
}

// --- the module and its dump ------------------------------------------

std::string IRModule::internString(const std::string &value) {
    for (std::size_t i = 0; i < strings.size(); ++i) {
        if (strings[i].value == value) return strings[i].name;
    }
    std::ostringstream ss;
    ss << "str" << strings.size();
    strings.push_back(IRString(ss.str(), value));
    return strings.back().name;
}

IRModule::~IRModule() {
    for (std::size_t i = 0; i < functions.size(); ++i) delete functions[i];
}

static std::string regName(IRReg r) {
    if (r == IR_NoReg) return "_";
    std::ostringstream ss;
    ss << "%" << r;
    return ss.str();
}

void IRModule::printInstr(const IRInstr &i) {
    std::cout << "    ";
    if (i.op == IR_Label) {
        std::cout << "L" << i.imm << ":" << std::endl;
        return;
    }
    if (i.dest != IR_NoReg) std::cout << regName(i.dest) << " = ";
    else                    std::cout << "        ";
    std::cout << irOpName(i.op);

    switch (i.op) {
    case IR_Const:
        std::cout << " " << i.imm;
        break;
    case IR_Alloc:
        // The size is a constant or a register, and a count beside it says the
        // instruction is a new[]; both show in the dump.
        if (i.a != IR_NoReg) std::cout << " " << regName(i.a);
        else                 std::cout << " " << i.imm;
        if (i.b != IR_NoReg) std::cout << " [" << regName(i.b) << "]";
        break;
    case IR_FConst:
        std::cout << " " << i.fimm;
        break;
    case IR_StringAddr:
        std::cout << " " << i.sym;
        break;
    case IR_IntResize:
        std::cout << " " << regName(i.a) << " :" << i.imm
                  << (i.b == 1 ? " signed" : " unsigned");
        break;
    case IR_IntToFloat:
        std::cout << " " << regName(i.a) << (i.imm ? " (unsigned)" : "");
        break;
    case IR_FloatToInt:
    case IR_FloatResize:
        std::cout << " " << regName(i.a) << " :" << i.imm;
        break;
    case IR_LocalAddr:
        std::cout << " #" << i.imm;
        break;
    case IR_GlobalAddr:
    case IR_FuncAddr:
        std::cout << " " << i.sym;
        break;
    case IR_FieldAddr:
        std::cout << " " << regName(i.a) << " +" << i.imm;
        break;
    case IR_Load:
        std::cout << " [" << regName(i.a) << "] :" << i.imm << (i.isFloat ? "f" : "");
        break;
    case IR_Store:
        std::cout << " [" << regName(i.a) << "] <- " << regName(i.b)
                  << " :" << i.imm << (i.isFloat ? "f" : "");
        break;
    case IR_MemCopy:
        std::cout << " [" << regName(i.a) << "] <- [" << regName(i.b)
                  << "] :" << i.imm;
        break;
    case IR_VCallTarget:
        std::cout << " [" << regName(i.a) << "] slot " << i.imm;
        break;
    case IR_Call:
    case IR_CallIndirect:
        if (i.op == IR_Call) std::cout << " " << i.sym;
        else                 std::cout << " " << regName(i.a);
        std::cout << "(";
        for (std::size_t k = 0; k < i.args.size(); ++k) {
            if (k) std::cout << ", ";
            std::cout << regName(i.args[k]);
        }
        std::cout << ")";
        break;
    case IR_Jump:
        std::cout << " L" << i.imm;
        break;
    case IR_BranchZero:
    case IR_BranchNZ:
        std::cout << " " << regName(i.a) << " L" << i.imm;
        break;
    case IR_Return:
        if (i.a != IR_NoReg) std::cout << " " << regName(i.a);
        break;
    case IR_Free:
        std::cout << " " << regName(i.a);
        if (i.isArray) std::cout << " []";
        break;
    default:
        if (i.a != IR_NoReg) std::cout << " " << regName(i.a);
        if (i.b != IR_NoReg) std::cout << ", " << regName(i.b);
        break;
    }
    std::cout << std::endl;
}

void IRModule::print() const {
    for (std::size_t s = 0; s < strings.size(); ++s) {
        std::cout << "string " << strings[s].name << "  \"" << strings[s].value
                  << "\"" << std::endl;
    }
    if (!strings.empty()) std::cout << std::endl;

    for (std::size_t g = 0; g < globals.size(); ++g) {
        std::cout << "global " << globals[g].name
                  << "  " << globals[g].size << " bytes" << std::endl;
    }
    if (!globals.empty()) std::cout << std::endl;

    for (std::size_t v = 0; v < vtables.size(); ++v) {
        const IRVTable &vt = vtables[v];
        std::cout << "vtable " << mangleVTable(vt.className) << std::endl;
        for (std::size_t s = 0; s < vt.slots.size(); ++s) {
            std::cout << "    [" << s << "] " << vt.slots[s] << std::endl;
        }
        std::cout << std::endl;
    }

    for (std::size_t f = 0; f < functions.size(); ++f) {
        const IRFunction &fn = *functions[f];
        std::cout << "function " << fn.name;
        if (fn.sourceName != fn.name) std::cout << "   ; " << fn.sourceName;
        std::cout << std::endl;
        for (std::size_t l = 0; l < fn.locals.size(); ++l) {
            const IRLocal &loc = fn.locals[l];
            std::cout << "  #" << loc.slot << " " << loc.name
                      << "  " << loc.size << " bytes"
                      << (loc.isParam ? "  (param)" : "") << std::endl;
        }
        for (std::size_t c = 0; c < fn.code.size(); ++c) printInstr(fn.code[c]);
        std::cout << std::endl;
    }
}

// ---------- Lower.cpp ----------
// Lower.cpp
//
// C++98 only.  See Lower.h for the address/value idea this pass turns on.


#include <cstddef>

namespace cc {

Lowering::Lowering(IRModule &module, const Layout &l, Diagnostics &d)
    : mod(module), layout(l), diag(d), fn(0), objectDest(IR_NoReg),
      currentReturnType(0) {}

Lowering::~Lowering() {
    for (std::map<int, Type*>::iterator it = builtinCache.begin();
         it != builtinCache.end(); ++it) {
        delete it->second;
    }
    for (std::size_t i = 0; i < ownedDecays.size(); ++i) delete ownedDecays[i];
}

// --- Scopes and slots ---

void Lowering::pushScope() {
    scopeMarks.push_back(static_cast<int>(scopeNames.size()));
}

void Lowering::popScope() {
    if (scopeMarks.empty()) return;
    const int mark = scopeMarks.back();
    scopeMarks.pop_back();
    while (static_cast<int>(scopeNames.size()) > mark) {
        const Shadowed &s = scopeNames.back();
        if (s.prevSlot >= 0) slots[s.name] = s.prevSlot;
        else                 slots.erase(s.name);
        if (s.prevType)      localTypes[s.name] = s.prevType;
        else                 localTypes.erase(s.name);
        scopeNames.pop_back();
    }
}

int Lowering::declareLocal(const std::string &name, int size, bool isParam, bool isFloat,
                           bool isObject) {
    const int slot = fn->addLocal(name, size, isParam, isFloat, isObject);

    // Remember what this name meant before, so the block can put it back.
    Shadowed prev;
    prev.name = name;
    std::map<std::string, int>::const_iterator os = slots.find(name);
    if (os != slots.end()) prev.prevSlot = os->second;
    std::map<std::string, Type*>::const_iterator ot = localTypes.find(name);
    if (ot != localTypes.end()) prev.prevType = ot->second;
    scopeNames.push_back(prev);

    slots[name] = slot;
    return slot;
}

int Lowering::findSlot(const std::string &name) const {
    std::map<std::string, int>::const_iterator it = slots.find(name);
    return (it == slots.end()) ? -1 : it->second;
}

int Lowering::sizeOfType(Type *t) const {
    const int s = layout.sizeOf(t);
    return s > 0 ? s : Layout::IntSize;
}

// Types the lowering pass forms for literals and for the common type of a
// binary operator.  Owned here, because they belong to no AST node.
// Lowering does pointer arithmetic on decayed types, so an array is turned
// into a pointer to its element here, exactly as the semantic pass did.
Type *Lowering::decayType(Type *t) {
    ArrayType *at = dynamic_cast<ArrayType*>(t);
    if (!at) return t;
    Type *element = cloneTypeShallow(at->element);
    if (!element) return t;             // nothing safe to point at
    Type *p = new PointerType(element);
    ownedDecays.push_back(p);
    return p;
}

// A copy the CALLER owns.  ~PointerType deletes its base, so a formed pointer
// must never be handed a node anything else owns -- an array of objects gave
// its ClassType two owners and freed it twice.
Type *Lowering::cloneTypeShallow(Type *t) {
    if (!t) return 0;
    if (BuiltinType *bt = dynamic_cast<BuiltinType*>(t)) return new BuiltinType(bt->kind);
    if (PointerType *pt = dynamic_cast<PointerType*>(t)) {
        Type *base = cloneTypeShallow(pt->base);
        return base ? new PointerType(base) : 0;
    }
    if (ArrayType *at = dynamic_cast<ArrayType*>(t)) {
        Type *element = cloneTypeShallow(at->element);
        return element ? new ArrayType(element, at->count) : 0;
    }
    return cloneForeignType(t);         // virtual: class and reference types
}

Type *Lowering::literalType(BuiltinKind k) {
    std::map<int, Type*>::iterator it = builtinCache.find(static_cast<int>(k));
    if (it != builtinCache.end()) return it->second;
    Type *t = new BuiltinType(k);
    builtinCache[static_cast<int>(k)] = t;
    return t;
}

Type *Lowering::commonType(BuiltinKind k) { return literalType(k); }

// The same rule the semantic pass applied, restated where lowering needs it.
BuiltinKind Lowering::commonKind(BuiltinKind a, BuiltinKind b) {
    if (a == BK_Double || b == BK_Double) return BK_Double;
    if (a == BK_Float  || b == BK_Float)  return BK_Float;
    if (builtinRank(a) < builtinRank(BK_Int)) a = BK_Int;
    if (builtinRank(b) < builtinRank(BK_Int)) b = BK_Int;
    if (a == b) return a;
    const int ra = builtinRank(a), rb = builtinRank(b);
    if (ra != rb) return (ra > rb) ? a : b;
    return builtinIsSigned(a) ? b : a;
}

bool Lowering::isArrayType(Type *t) {
    return dynamic_cast<ArrayType*>(t) != 0;
}

bool Lowering::isFloatType(Type *t) {
    BuiltinKind k;
    return arithKind(t, k) && builtinIsFloating(k);
}

bool Lowering::arithKind(Type *t, BuiltinKind &out) {
    BuiltinType *bt = dynamic_cast<BuiltinType*>(t);
    if (!bt || !builtinIsArithmetic(bt->kind)) return false;
    out = bt->kind;
    return true;
}

// A conversion is never free: a narrower integer must be truncated, a wider one
// sign- or zero-extended, and int and float do not even share a register file
// on most machines.  Emitting them explicitly is what stops a size mismatch
// slipping silently into the code generator.
IRReg Lowering::convert(IRReg value, Type *from, Type *to, int line) {
    // Converting TO bool is a test against zero, whatever the source -- an
    // integer, a floating value or a pointer.  That is the one conversion in
    // the language that is a comparison rather than a resize.
    if (isBoolType(to)) {
        if (isBoolType(from)) return value;
        if (isFloatType(from)) {
            const IRReg zero = fn->emitFConst(0.0, line);
            return fn->emitBinary(IR_FCmpNE, value, zero, line);
        }
        const IRReg zero = fn->emitConst(0, line);
        return fn->emitBinary(IR_CmpNE, value, zero, line);
    }
    // Converting FROM bool: the value is already 0 or 1, so only its width
    // may need adjusting.
    if (isBoolType(from)) {
        BuiltinKind k;
        if (!arithKind(to, k)) return value;
        if (builtinIsFloating(k)) {
            return fn->emitConvert(IR_IntToFloat, value, 1, IR_NoReg, line);
        }
        return fn->emitConvert(IR_IntResize, value, builtinSize(k), 0, line);
    }

    BuiltinKind kf, kt;
    if (!arithKind(from, kf) || !arithKind(to, kt)) return value;
    if (kf == kt) return value;

    const bool ff = builtinIsFloating(kf);
    const bool ft = builtinIsFloating(kt);

    if (ff && ft) {
        return fn->emitConvert(IR_FloatResize, value, builtinSize(kt), IR_NoReg, line);
    }
    if (!ff && ft) {
        // Integer to floating; the source's signedness decides the instruction.
        return fn->emitConvert(IR_IntToFloat, value,
                               builtinIsSigned(kf) ? 0 : 1, IR_NoReg, line);
    }
    if (ff && !ft) {
        const IRReg asInt = fn->emitConvert(IR_FloatToInt, value,
                                            builtinSize(kt), IR_NoReg, line);
        return asInt;
    }
    // Integer to integer: resize, sign-extending only from a signed source.
    return fn->emitConvert(IR_IntResize, value, builtinSize(kt),
                           builtinIsSigned(kf) ? 1 : 0, line);
}

// --- types, recomputed cheaply ---------------------------------------

Type *Lowering::typeOf(Expr *e) {
    if (!e) return 0;
    // A name keeps its DECLARED type.  Semantic reports what an expression
    // sees -- an array decayed to a pointer, a reference already stripped --
    // but lowering has to know the storage before it can address it.
    if (IdentExpr *id = dynamic_cast<IdentExpr*>(e)) {
        std::map<std::string, Type*>::iterator it = localTypes.find(id->name);
        if (it != localTypes.end()) return it->second;
        it = globalTypes.find(id->name);
        if (it != globalTypes.end()) return it->second;
        return e->resolvedType;
    }
    // *p likewise: Semantic decays the inner array of g[1][2] to a pointer,
    // and lowering would then load an address out of the array's own bytes.
    if (UnaryExpr *ud = dynamic_cast<UnaryExpr*>(e)) {
        if (ud->op == UN_Deref) {
            Type *base = decayType(typeOf(ud->operand));
            if (PointerType *pt = dynamic_cast<PointerType*>(base)) return pt->base;
            return e->resolvedType;
        }
    }
    // Everywhere else Semantic's answer is the complete one; what follows is
    // the fallback for a node the analysis never reached.
    if (e->resolvedType) return e->resolvedType;
    if (NumberExpr *n = dynamic_cast<NumberExpr*>(e)) return literalType(n->kind);
    if (FloatExpr *f = dynamic_cast<FloatExpr*>(e))   return literalType(f->kind);
    if (UnaryExpr *u = dynamic_cast<UnaryExpr*>(e)) {
        // ++x and x-- have the type of what they step, which is what keeps a
        // double out of the integer opcodes.
        if (u->op == UN_Neg || unaryOpIsIncDec(u->op)) return typeOf(u->operand);
        if (u->op == UN_Deref) {
            Type *base = decayType(typeOf(u->operand));
            if (PointerType *pt = dynamic_cast<PointerType*>(base)) return pt->base;
        }
        return 0;                                   // &x, !x
    }
    if (CastExpr *c = dynamic_cast<CastExpr*>(e)) return c->type;
    // A call has the type its function returns.  Without this a double-valued
    // function handed to an int parameter arrives as raw bits.
    if (CallExpr *call = dynamic_cast<CallExpr*>(e)) {
        if (call->resolved) return call->resolved->retType;
        if (IdentExpr *cid = dynamic_cast<IdentExpr*>(call->callee)) {
            std::map<std::string, Function*>::const_iterator it = functions.find(cid->name);
            if (it != functions.end()) return it->second->retType;
        }
        return 0;
    }
    if (BinaryExpr *b = dynamic_cast<BinaryExpr*>(e)) {
        if (binaryOpIsAssignment(b->op)) return typeOf(b->lhs);
        // p + n and p - n stay pointers, which is what makes a[i] load the
        // right width.
        if (b->op == BIN_Add || b->op == BIN_Sub) {
            Type *lt = decayType(typeOf(b->lhs));
            if (dynamic_cast<PointerType*>(lt)) return lt;
            Type *rt = decayType(typeOf(b->rhs));
            if (b->op == BIN_Add && dynamic_cast<PointerType*>(rt)) return rt;
        }
        // Arithmetic yields the type its operands met in.  Without this a
        // nested expression loses its type and the next operator falls back to
        // integer -- so 3.14 * r * r would multiply with the wrong opcode.
        if (!binaryOpIsComparison(b->op) && !binaryOpIsLogical(b->op)) {
            BuiltinKind kl, kr;
            if (arithKind(typeOf(b->lhs), kl) && arithKind(typeOf(b->rhs), kr)) {
                return literalType(commonKind(kl, kr));
            }
        }
        return 0;
    }
    return 0;
}

bool Lowering::isReferenceExpr(Expr *) {
    return false;               // C has no references
}

bool Lowering::isReferenceType(Type *) {
    return false;
}

Type *Lowering::referentType(Type *t) {
    return t;                   // C has no references
}

bool Lowering::isObjectType(Type *) {
    return false;               // C has no class types
}

void Lowering::reassertVPtr(Type *, IRReg, int) {}      // C has no vptr

bool Lowering::yieldsObject(Expr *) const { return false; }   // C has no objects

const char *Lowering::ReturnSlotName = "$ret";   // '$' keeps it out of reach of user code

bool Lowering::returnsObject(Function *f) {
    return f && isObjectType(f->retType);
}

IRReg Lowering::takeObjectDest() {
    const IRReg d = objectDest;
    objectDest = IR_NoReg;      // at most once: the outermost expression gets it
    return d;
}

IRReg Lowering::allocReturnSlot(Function *target, int line, IRReg given) {
    // Given a destination, the callee writes the result straight into it: no
    // temporary to copy out of, and none to destroy afterwards.
    if (given != IR_NoReg) return given;

    const int size = sizeOfType(target->retType);
    const int slot = declareLocal("$result", size > 0 ? size : Layout::PointerSize, false);
    return fn->emitLocalAddr(slot, line);
}

// The C layer has no constructors: a returned object is its bytes.
void Lowering::emitReturnObject(IRReg dest, Expr *e, int line) {
    fn->emitMemCopy(dest, lowerObjectValue(e), sizeOfType(currentReturnType), line);
}

IRReg Lowering::lowerByValueObject(Type *, Expr *e, int) {
    return lowerObjectValue(e);         // C has no copy constructor to call
}

void Lowering::destroyArgTempsDownTo(std::size_t mark, int) {
    while (argTemps.size() > mark) argTemps.pop_back();   // C has none to destroy
}

IRReg Lowering::lowerObjectValue(Expr *e) {
    // A call already yields the address of the caller-supplied slot; a
    // temporary yields the space it was built in; anything else with a place
    // in memory yields that place.
    if (dynamic_cast<CallExpr*>(e)) return lowerValue(e);
    if (yieldsObject(e)) return lowerValue(e);
    if (BinaryExpr *b = dynamic_cast<BinaryExpr*>(e)) {
        if (b->resolvedOperator) return lowerValue(e);
    }
    return lowerAddress(e);
}

// --- Declarations ---

void Lowering::lowerUnit(const std::vector<Decl*> &units) {
    // Globals first, so a function body can refer to any of them.
    for (std::size_t i = 0; i < units.size(); ++i) {
        VarDecl *vd = dynamic_cast<VarDecl*>(units[i]);
        if (vd) {
            mod.globals.push_back(IRGlobal(vd->name, sizeOfType(vd->type)));
            globalTypes[vd->name] = vd->type;
        }
    }
    // Record every function first, bodiless declarations included: an argument
    // must be converted to its parameter's type, and a native is declared and
    // never defined.
    for (std::size_t i = 0; i < units.size(); ++i) {
        Function *f = dynamic_cast<Function*>(units[i]);
        if (f) functions[f->name] = f;
    }
    for (std::size_t i = 0; i < units.size(); ++i) lowerDecl(units[i]);
    emitGlobalInit(units);
    emitGlobalFini(units);
}

const char *Lowering::GlobalInitName = "__global_init";
const char *Lowering::GlobalFiniName = "__global_fini";

// Everything a global needs before main runs: scalar initialisers, and (in the
// layer above) constructors for global objects.  One function, called once.
void Lowering::emitGlobalInit(const std::vector<Decl*> &units) {
    IRFunction *irf = new IRFunction(GlobalInitName, GlobalInitName);
    mod.functions.push_back(irf);

    IRFunction *savedFn = fn;
    Type *savedReturn = currentReturnType;
    fn = irf;
    currentReturnType = 0;

    int line = 0;
    for (std::size_t i = 0; i < units.size(); ++i) {
        VarDecl *vd = dynamic_cast<VarDecl*>(units[i]);
        if (!vd) continue;
        line = vd->line;
        initGlobal(vd, fn->emitGlobalAddr(vd->name, vd->line));
    }
    fn->emitReturn(IR_NoReg, line);

    fn = savedFn;
    currentReturnType = savedReturn;
}

// The mirror of emitGlobalInit: reverse order, as every other scope exit is.
void Lowering::emitGlobalFini(const std::vector<Decl*> &units) {
    IRFunction *irf = new IRFunction(GlobalFiniName, GlobalFiniName);
    mod.functions.push_back(irf);

    IRFunction *savedFn = fn;
    Type *savedReturn = currentReturnType;
    fn = irf;
    currentReturnType = 0;

    int line = 0;
    for (std::size_t i = units.size(); i > 0; --i) {
        VarDecl *vd = dynamic_cast<VarDecl*>(units[i - 1]);
        if (!vd) continue;
        line = vd->line;
        destroyGlobal(vd);
    }
    fn->emitReturn(IR_NoReg, line);

    fn = savedFn;
    currentReturnType = savedReturn;
}

void Lowering::destroyGlobal(VarDecl *) {}              // C has no destructors

void Lowering::initGlobal(VarDecl *vd, IRReg addr) {
    if (!vd->init) return;
    Type *t = referentType(vd->type);
    IRReg v = lowerValue(vd->init);
    v = convert(v, referentType(typeOf(vd->init)), t, vd->line);
    fn->emitStore(addr, v, sizeOfType(t), isFloatType(t), vd->line);
}

// A native keeps its plain name so the VM can recognise it; everything else
// carries its signature, because a name alone no longer identifies a function.
std::string Lowering::symbolFor(Function *f, const std::string &className) {
    if (className.empty()) {
        // main is the entry point and cannot be overloaded, so it keeps its
        // plain name -- as it does in a real toolchain.
        if (f->name == "main") return f->name;
        // A native is recognised by name, so it keeps its own too.
        if (!f->body && nativeByName(f->name) != NAT_Count) return f->name;
    }
    return mangleOverload(className, f->name, f->params);
}

void Lowering::lowerDecl(Decl *d) {
    Function *f = dynamic_cast<Function*>(d);
    if (f && f->body) lowerFunction(f, symbolFor(f, ""), f->name, false);
}

void Lowering::lowerFunction(Function *f, const std::string &mangled,
                             const std::string &sourceName, bool hasThis) {
    IRFunction *irf = new IRFunction(mangled, sourceName);
    mod.functions.push_back(irf);

    IRFunction *savedFn = fn;
    Type *savedReturn = currentReturnType;
    fn = irf;
    currentReturnType = f->retType;
    // A fresh naming environment: nothing from the caller's scope is visible.
    std::map<std::string, int> savedSlots;
    savedSlots.swap(slots);
    std::map<std::string, Type*> savedTypes;
    savedTypes.swap(localTypes);

    pushScope();

    // An ordinary first parameter, once the C++ is erased.
    if (hasThis) {
        declareLocal("this", Layout::PointerSize, true);
        ++irf->paramCount;
    }
    // Then the hidden result pointer, if the function returns an object.
    if (returnsObject(f)) {
        declareLocal(ReturnSlotName, Layout::PointerSize, true);
        ++irf->paramCount;
    }
    for (std::size_t i = 0; i < f->params.size(); ++i) {
        VarDecl *p = f->params[i];
        const std::string pname = p->name.empty() ? "_" : p->name;
        declareLocal(pname, sizeOfType(p->type), true, isFloatType(referentType(p->type)),
                     isObjectType(p->type));
        localTypes[pname] = p->type;
        ++irf->paramCount;
    }
    irf->returnsValue = (f->retType != 0);

    // Globals are initialised before the first statement of main, which is
    // where "before the program runs" actually means something.
    if (mangled == "main") {
        std::vector<IRReg> none;
        irf->emitCall(GlobalInitName, none, false, f->line);
    }

    emitPrologue(f);                        // virtual: a constructor's preamble
    if (f->body) lowerBlock(f->body);
    // A body that already returned emitted its tail on the path that left.
    if (!irf->endsWithTerminator()) {
        emitEpilogue(f);                    // virtual: a destructor's tail
        irf->emitReturn(IR_NoReg, f->line);
    }

    popScope();
    fn = savedFn;
    currentReturnType = savedReturn;
    slots.swap(savedSlots);
    localTypes.swap(savedTypes);
}

void Lowering::emitPrologue(Function *) {}
void Lowering::emitEpilogue(Function *) {}
void Lowering::emitScopeExit(CompoundStmt *) {}
void Lowering::emitAllOpenScopeExits() {}
void Lowering::emitScopeExitsDownTo(std::size_t) {}

// --- Statements ---

void Lowering::lowerBlock(CompoundStmt *block) {
    pushScope();
    openBlocks.push_back(block);
    for (std::size_t i = 0; i < block->body.size(); ++i) lowerStmt(block->body[i]);
    // If the block already returned, its destructors ran on that path.
    if (!fn->endsWithTerminator()) emitScopeExit(block);
    openBlocks.pop_back();
    popScope();
}

void Lowering::lowerStmt(Stmt *s) {
    if (!s) return;

    if (CompoundStmt *b = dynamic_cast<CompoundStmt*>(s)) { lowerBlock(b); return; }
    if (DeclStmt *ds = dynamic_cast<DeclStmt*>(s))        { lowerVarDecl(ds->var); return; }

    if (ExprStmt *es = dynamic_cast<ExprStmt*>(s)) {
        // The value is discarded, but the call still happens.
        if (CallExpr *call = dynamic_cast<CallExpr*>(es->expr)) lowerCall(call, false);
        else if (es->expr) lowerValue(es->expr);
        return;
    }

    if (ReturnStmt *rs = dynamic_cast<ReturnStmt*>(s)) {
        IRReg v = IR_NoReg;
        if (rs->expr && isObjectType(currentReturnType)) {
            // Copy into the caller's slot BEFORE the destructors run: the
            // object being returned is one of the locals about to be destroyed.
            const int slot = findSlot(ReturnSlotName);
            if (slot >= 0) {
                const IRReg dest = fn->emitLoad(fn->emitLocalAddr(slot, rs->line),
                                                Layout::PointerSize, false, rs->line);
                emitReturnObject(dest, rs->expr, rs->line);
                v = dest;
            }
        } else if (rs->expr && isReferenceType(currentReturnType)) {
            // T& hands back the ADDRESS of what it names -- that is the whole
            // of what a reference return is, and what makes  t[1] = 42;  work.
            v = lowerAddress(rs->expr);
        } else if (rs->expr) {
            v = lowerValue(rs->expr);
            v = convert(v, typeOf(rs->expr), currentReturnType, rs->line);
        }
        // Everything this return leaves is torn down first.
        emitAllOpenScopeExits();
        fn->emitReturn(v, rs->line);
        return;
    }

    if (IfStmt *is = dynamic_cast<IfStmt*>(s))    { lowerIf(is); return; }
    if (DoWhileStmt *dw = dynamic_cast<DoWhileStmt*>(s)) { lowerDoWhile(dw); return; }
    if (SwitchStmt *sw = dynamic_cast<SwitchStmt*>(s))   { lowerSwitch(sw); return; }
    if (CaseStmt *cs = dynamic_cast<CaseStmt*>(s)) {
        // A case label is exactly that: a place to jump to.
        std::map<const CaseStmt*, int>::const_iterator it = caseLabels.find(cs);
        if (it != caseLabels.end()) fn->emitLabel(it->second);
        return;
    }
    if (WhileStmt *ws = dynamic_cast<WhileStmt*>(s)) { lowerWhile(ws); return; }
    if (ForStmt *fs = dynamic_cast<ForStmt*>(s))  { lowerFor(fs); return; }

    if (dynamic_cast<BreakStmt*>(s)) {
        if (!breakTargets.empty()) {
            emitScopeExitsDownTo(breakScopeDepth.back());
            fn->emitJump(breakTargets.back(), s->line);
        }
        return;
    }
    if (dynamic_cast<ContinueStmt*>(s)) {
        if (!continueTargets.empty()) {
            emitScopeExitsDownTo(continueScopeDepth.back());
            fn->emitJump(continueTargets.back(), s->line);
        }
        return;
    }
}

void Lowering::lowerVarDecl(VarDecl *vd) {
    if (!vd) return;
    const int size = sizeOfType(vd->type);
    const int slot = declareLocal(vd->name, size, false, isFloatType(referentType(vd->type)));
    localTypes[vd->name] = vd->type;
    if (vd->init) {
        // A reference stores the ADDRESS of what it binds to -- the whole of
        // what a reference becomes.  The DECLARED type decides that, not the
        // initialiser: `Base& r = obj;` binds to obj, it does not copy it.
        IRReg value;
        if (isReferenceType(vd->type)) {
            value = lowerAddress(vd->init);
        } else {
            value = lowerValue(vd->init);
            value = convert(value, typeOf(vd->init), vd->type, vd->line);
        }
        const IRReg addr = fn->emitLocalAddr(slot, vd->line);
        fn->emitStore(addr, value, size, isFloatType(vd->type), vd->line);
    }
}

void Lowering::lowerIf(IfStmt *s) {
    const int elseLabel = fn->newLabel();
    const int endLabel = s->elseBranch ? fn->newLabel() : elseLabel;

    const IRReg cond = lowerValue(s->cond);
    fn->emitBranchZero(cond, elseLabel, s->line);
    lowerStmt(s->thenBranch);
    if (s->elseBranch) {
        fn->emitJump(endLabel, s->line);
        fn->emitLabel(elseLabel);
        lowerStmt(s->elseBranch);
        fn->emitLabel(endLabel);
    } else {
        fn->emitLabel(elseLabel);
    }
}

// The body runs before the condition is first tested, which is the whole of
// the difference from `while`.
void Lowering::lowerDoWhile(DoWhileStmt *s) {
    const int top = fn->newLabel();
    const int test = fn->newLabel();
    const int done = fn->newLabel();

    fn->emitLabel(top);
    breakTargets.push_back(done);
    breakScopeDepth.push_back(openBlocks.size());
    continueTargets.push_back(test);                        // `continue` goes to the test
    continueScopeDepth.push_back(openBlocks.size());
    lowerStmt(s->body);
    continueTargets.pop_back();
    continueScopeDepth.pop_back();
    breakTargets.pop_back();
    breakScopeDepth.pop_back();

    fn->emitLabel(test);
    const IRReg cond = lowerValue(s->cond);
    fn->emitBranchNZ(cond, top, s->line);
    fn->emitLabel(done);
}

// A comparison chain, then the body emitted straight through -- so control
// enters at the matching label and runs on until a break, which is what
// fall-through is.  A jump table would be faster and would hide that.
void Lowering::lowerSwitch(SwitchStmt *s) {
    const int done = fn->newLabel();
    int defaultLabel = done;

    std::map<const CaseStmt*, int> saved;
    saved.swap(caseLabels);

    const IRReg subject = lowerValue(s->cond);

    std::vector<const CaseStmt*> cases;
    if (s->body) {
        for (std::size_t i = 0; i < s->body->body.size(); ++i) {
            CaseStmt *c = dynamic_cast<CaseStmt*>(s->body->body[i]);
            if (!c) continue;
            const int label = fn->newLabel();
            caseLabels[c] = label;
            if (c->isDefault) defaultLabel = label;
            else              cases.push_back(c);
        }
    }

    for (std::size_t i = 0; i < cases.size(); ++i) {
        const IRReg want = fn->emitConst(cases[i]->value, s->line);
        const IRReg eq = fn->emitBinary(IR_CmpEQ, subject, want, s->line);
        fn->emitBranchNZ(eq, caseLabels[cases[i]], s->line);
    }
    fn->emitJump(defaultLabel, s->line);

    breakTargets.push_back(done);
    breakScopeDepth.push_back(openBlocks.size());
    lowerStmt(s->body);
    breakTargets.pop_back();
    breakScopeDepth.pop_back();
    fn->emitLabel(done);

    caseLabels.swap(saved);
}

void Lowering::lowerWhile(WhileStmt *s) {
    const int top = fn->newLabel();
    const int done = fn->newLabel();

    fn->emitLabel(top);
    const IRReg cond = lowerValue(s->cond);
    fn->emitBranchZero(cond, done, s->line);

    breakTargets.push_back(done);
    breakScopeDepth.push_back(openBlocks.size());
    continueTargets.push_back(top);
    continueScopeDepth.push_back(openBlocks.size());
    lowerStmt(s->body);
    continueTargets.pop_back();
    continueScopeDepth.pop_back();
    breakTargets.pop_back();
    breakScopeDepth.pop_back();

    fn->emitJump(top, s->line);
    fn->emitLabel(done);
}

void Lowering::lowerFor(ForStmt *s) {
    // The init declaration belongs to the loop.
    pushScope();
    if (s->init) lowerStmt(s->init);

    const int top = fn->newLabel();
    const int step = fn->newLabel();
    const int done = fn->newLabel();

    fn->emitLabel(top);
    if (s->cond) {
        const IRReg cond = lowerValue(s->cond);
        fn->emitBranchZero(cond, done, s->line);
    }

    breakTargets.push_back(done);
    breakScopeDepth.push_back(openBlocks.size());
    continueTargets.push_back(step);                        // continue runs the step first
    continueScopeDepth.push_back(openBlocks.size());
    lowerStmt(s->body);
    continueTargets.pop_back();
    continueScopeDepth.pop_back();
    breakTargets.pop_back();
    breakScopeDepth.pop_back();

    fn->emitLabel(step);
    if (s->step) lowerValue(s->step);
    fn->emitJump(top, s->line);
    fn->emitLabel(done);
    popScope();
}

// --- Expressions ---

IRReg Lowering::lowerAddress(Expr *e) {
    if (!e) return IR_NoReg;

    IRReg out = IR_NoReg;
    if (lowerLayerAddress(e, out)) return out;      // virtual: a.b, this, ...

    if (IdentExpr *id = dynamic_cast<IdentExpr*>(e)) {
        const int slot = findSlot(id->name);
        if (slot >= 0) {
            const IRReg addr = fn->emitLocalAddr(slot, e->line);
            // A reference's slot holds another object's address, so the address
            // OF the reference is the value IN its slot.
            if (isReferenceExpr(e)) return fn->emitLoad(addr, Layout::PointerSize, false, e->line);
            return addr;
        }
        return fn->emitGlobalAddr(id->name, e->line);
    }

    if (UnaryExpr *u = dynamic_cast<UnaryExpr*>(e)) {
        if (u->op == UN_Deref) return lowerValue(u->operand);   // *p: p's value
    }

    if (IndexExpr *ix = dynamic_cast<IndexExpr*>(e)) return lowerIndexAddress(ix);

    // A call, or an overloaded operator, whose result is an object: it has no
    // name, but it does have a place -- the slot the caller supplied for it.
    // That is what makes  (a + b).x  addressable.
    if (CallExpr *c = dynamic_cast<CallExpr*>(e)) {
        // A call returning T& already yields an address; one returning an
        // object yields the slot the caller supplied for it.
        if (c->resolved && (isObjectType(c->resolved->retType) ||
                            isReferenceType(c->resolved->retType))) {
            return lowerValue(e);
        }
    }
    if (BinaryExpr *bo = dynamic_cast<BinaryExpr*>(e)) {
        if (bo->resolvedOperator && isObjectType(bo->resolvedOperator->retType)) {
            return lowerValue(e);
        }
    }
    if (UnaryExpr *uo = dynamic_cast<UnaryExpr*>(e)) {
        if (uo->resolvedOperator && isObjectType(uo->resolvedOperator->retType)) {
            return lowerValue(e);
        }
    }

    if (BinaryExpr *b = dynamic_cast<BinaryExpr*>(e)) {
        // An assignment is an lvalue; its address is the left side's.
        if (binaryOpIsAssignment(b->op)) { lowerAssign(b); return lowerAddress(b->lhs); }
    }

    diag.error(e->line, e->col, "internal: expression has no address to lower");
    return fn->emitConst(0, e->line);
}

IRReg Lowering::lowerValue(Expr *e) {
    if (!e) return IR_NoReg;

    IRReg out = IR_NoReg;
    if (lowerLayerValue(e, out)) return out;        // virtual: this, new, a.b

    if (NumberExpr *n = dynamic_cast<NumberExpr*>(e)) {
        return fn->emitConst(n->value, e->line);
    }
    if (FloatExpr *f = dynamic_cast<FloatExpr*>(e)) {
        return fn->emitFConst(f->value, e->line);
    }
    if (StringExpr *str = dynamic_cast<StringExpr*>(e)) {
        return fn->emitStringAddr(mod.internString(str->value), e->line);
    }

    if (IdentExpr *id = dynamic_cast<IdentExpr*>(e)) {
        const IRReg addr = lowerAddress(e);
        Type *t = referentType(typeOf(e));
        (void)id;
        // An array decays: its value IS its address, with nothing loaded.
        if (isArrayType(t)) return addr;
        return fn->emitLoad(addr, t ? sizeOfType(t) : Layout::IntSize,
                            isFloatType(t), e->line);
    }

    if (IndexExpr *ix = dynamic_cast<IndexExpr*>(e)) {
        // An overload that returns by VALUE has no address to load from.
        if (ix->resolvedOperator && !isReferenceType(ix->resolvedOperator->retType)) {
            return lowerIndexOperator(ix);
        }
        const IRReg addr = lowerIndexAddress(ix);
        Type *t = ix->resolvedOperator ? referentType(typeOf(e)) : elementTypeOf(ix);
        if (!t) t = referentType(typeOf(e));
        // An array element that is itself an array, or an object, has no value
        // to load: its address IS the value.
        if (isArrayType(t) || isObjectType(t)) return addr;
        return fn->emitLoad(addr, t ? sizeOfType(t) : Layout::IntSize,
                            isFloatType(t), e->line);
    }

    if (CastExpr *ce = dynamic_cast<CastExpr*>(e)) {
        // A cast is an explicit conversion; the same machinery serves.
        const IRReg v = lowerValue(ce->expr);
        return convert(v, typeOf(ce->expr), ce->type, e->line);
    }
    if (CallExpr *call = dynamic_cast<CallExpr*>(e)) return lowerCall(call, true);
    if (UnaryExpr *u = dynamic_cast<UnaryExpr*>(e))  return lowerUnary(u);
    if (BinaryExpr *b = dynamic_cast<BinaryExpr*>(e)) return lowerBinary(b);

    diag.error(e->line, e->col, "internal: unhandled expression in lowering");
    return fn->emitConst(0, e->line);
}

// a[i] is base + i * sizeof(element) -- the same arithmetic the desugared
// *(a + i) used to produce, so the emitted IR is unchanged.  A class that
// overloads it takes the other branch, and the call IS the address when the
// overload returns a reference.
Type *Lowering::elementTypeOf(IndexExpr *e) {
    Type *bt = decayType(referentType(typeOf(e->base)));
    PointerType *pt = dynamic_cast<PointerType*>(bt);
    return pt ? pt->base : 0;
}

IRReg Lowering::lowerIndexAddress(IndexExpr *e) {
    if (e->resolvedOperator) return lowerIndexOperator(e);

    Type *bt = decayType(referentType(typeOf(e->base)));
    const IRReg base = lowerValue(e->base);
    IRReg index = lowerValue(e->index);

    PointerType *pt = dynamic_cast<PointerType*>(bt);
    const int step = pt ? sizeOfType(pt->base) : 1;
    if (step > 1) {
        index = fn->emitBinary(IR_Mul, index, fn->emitConst(step, e->line), e->line);
    }
    return fn->emitBinary(IR_Add, base, index, e->line);
}

IRReg Lowering::lowerIndexOperator(IndexExpr *e) {
    diag.error(e->line, e->col, "internal: operator[] outside the C++ layer");
    return fn->emitConst(0, e->line);
}

// ++p on a pointer moves by one object, not one byte.
IRReg Lowering::stepFor(Type *t, int line) {
    PointerType *pt = dynamic_cast<PointerType*>(t);
    return fn->emitConst(pt ? sizeOfType(pt->base) : 1, line);
}

// The target's address is taken ONCE and reused for the load and the store.
// Prefix yields the new value, postfix the old one; nothing else differs.
IRReg Lowering::lowerIncDec(UnaryExpr *e) {
    Type *t = referentType(typeOf(e->operand));
    const int size = t ? sizeOfType(t) : Layout::IntSize;
    const bool flt = isFloatType(t);

    const IRReg addr = lowerAddress(e->operand);
    const IRReg oldValue = fn->emitLoad(addr, size, flt, e->line);
    const IRReg step = stepFor(t, e->line);
    const bool up = (e->op == UN_PreInc || e->op == UN_PostInc);

    IRReg newValue;
    if (flt) {
        const IRReg fstep = fn->emitConvert(IR_IntToFloat, step, 0, IR_NoReg, e->line);
        newValue = fn->emitBinary(up ? IR_FAdd : IR_FSub, oldValue, fstep, e->line);
    } else {
        newValue = fn->emitBinary(up ? IR_Add : IR_Sub, oldValue, step, e->line);
    }
    fn->emitStore(addr, newValue, size, flt, e->line);
    return (e->op == UN_PreInc || e->op == UN_PreDec) ? newValue : oldValue;
}

IRReg Lowering::lowerUnary(UnaryExpr *e) {
    if (unaryOpIsIncDec(e->op)) return lowerIncDec(e);
    switch (e->op) {
    case UN_Neg: {
        BuiltinKind k;
        const bool flt = arithKind(referentType(typeOf(e->operand)), k) && builtinIsFloating(k);
        return fn->emitUnary(flt ? IR_FNeg : IR_Neg, lowerValue(e->operand), e->line);
    }
    case UN_Not:    return fn->emitUnary(IR_LogicalNot,
                        truth(lowerValue(e->operand), referentType(typeOf(e->operand)), e->line),
                        e->line);
    case UN_AddrOf: return lowerAddress(e->operand);        // &x IS the address
    case UN_Deref: {
        const IRReg addr = lowerValue(e->operand);
        Type *t = typeOf(e);
        // *p on a pointer-to-array yields the array's address; there is
        // nothing to load, because an array is not a register-sized value.
        if (isArrayType(t)) return addr;
        return fn->emitLoad(addr, t ? sizeOfType(t) : Layout::IntSize,
                            isFloatType(t), e->line);
    }
    default:
        break;
    }
    return IR_NoReg;
}

// Both operands are converted to the type they meet in before the operator
// runs, and the operator chosen depends on that type: integer, unsigned and
// floating arithmetic are three different machine operations.
IRReg Lowering::lowerBinary(BinaryExpr *e) {
    if (binaryOpIsAssignment(e->op)) return lowerAssign(e);
    if (e->op == BIN_LAnd || e->op == BIN_LOr) return lowerShortCircuit(e);

    Type *lt = referentType(typeOf(e->lhs));
    Type *rt = referentType(typeOf(e->rhs));
    IRReg a = lowerValue(e->lhs);
    IRReg b = lowerValue(e->rhs);

    // Pointer arithmetic counts objects, not bytes, so the integer side is
    // scaled by the pointee's size before the add.  This is the whole of what
    // makes a[i] reach element i rather than byte i.
    lt = decayType(lt);
    rt = decayType(rt);
    PointerType *pl = dynamic_cast<PointerType*>(lt);
    PointerType *pr = dynamic_cast<PointerType*>(rt);
    if ((pl || pr) && (e->op == BIN_Add || e->op == BIN_Sub)) {
        if (pl && pr) {
            // p - q: the byte difference divided by the element size.
            const IRReg diff = fn->emitBinary(IR_Sub, a, b, e->line);
            const int step = sizeOfType(pl->base);
            if (step <= 1) return diff;
            const IRReg by = fn->emitConst(step, e->line);
            return fn->emitBinary(IR_Div, diff, by, e->line);
        }
        PointerType *p = pl ? pl : pr;
        IRReg &index = pl ? b : a;
        const int step = sizeOfType(p->base);
        if (step > 1) {
            const IRReg by = fn->emitConst(step, e->line);
            index = fn->emitBinary(IR_Mul, index, by, e->line);
        }
        return fn->emitBinary(e->op == BIN_Add ? IR_Add : IR_Sub, a, b, e->line);
    }

    // Shift takes its type from the LEFT operand alone; the right one is a
    // count, not something to meet it in a common type.
    if (e->op == BIN_Shl || e->op == BIN_Shr) {
        BuiltinKind k;
        const bool uns = arithKind(lt, k) && !builtinIsSigned(k);
        const IROp op = (e->op == BIN_Shl) ? IR_Shl : (uns ? IR_UShr : IR_Shr);
        return fn->emitBinary(op, a, b, e->line);
    }

    BuiltinKind kl, kr;
    BuiltinKind common = BK_Int;
    if (arithKind(lt, kl) && arithKind(rt, kr)) {
        common = commonKind(kl, kr);
        a = convert(a, lt, commonType(common), e->line);
        b = convert(b, rt, commonType(common), e->line);
    }
    const bool flt = builtinIsFloating(common);
    const bool uns = !builtinIsSigned(common);

    IROp op = IR_Add;
    switch (e->op) {
    case BIN_Add: op = flt ? IR_FAdd : IR_Add; break;
    case BIN_Sub: op = flt ? IR_FSub : IR_Sub; break;
    case BIN_Mul: op = flt ? IR_FMul : IR_Mul; break;
    case BIN_Div: op = flt ? IR_FDiv : (uns ? IR_UDiv : IR_Div); break;
    case BIN_Mod: op = uns ? IR_UMod : IR_Mod; break;
    case BIN_EQ:  op = flt ? IR_FCmpEQ : IR_CmpEQ; break;
    case BIN_NE:  op = flt ? IR_FCmpNE : IR_CmpNE; break;
    case BIN_LT:  op = flt ? IR_FCmpLT : (uns ? IR_UCmpLT : IR_CmpLT); break;
    case BIN_GT:  op = flt ? IR_FCmpGT : (uns ? IR_UCmpGT : IR_CmpGT); break;
    case BIN_LE:  op = flt ? IR_FCmpLE : (uns ? IR_UCmpLE : IR_CmpLE); break;
    case BIN_GE:  op = flt ? IR_FCmpGE : (uns ? IR_UCmpGE : IR_CmpGE); break;
    default: break;
    }
    return fn->emitBinary(op, a, b, e->line);
}

IRReg Lowering::lowerAssign(BinaryExpr *e) {
    Type *t = referentType(typeOf(e->lhs));
    const int size = t ? sizeOfType(t) : Layout::IntSize;
    const bool flt = isFloatType(t);

    if (e->op == BIN_Assign) {
        // An object is copied byte for byte: it does not fit in a register,
        // and load-then-store would shift its bytes off the end of one.
        if (isObjectType(t)) {
            const IRReg src = lowerObjectValue(e->rhs);
            const IRReg dst = lowerAddress(e->lhs);
            fn->emitMemCopy(dst, src, size, e->line);
            reassertVPtr(t, dst, e->line);      // the copy is the target's class
            return dst;
        }
        // Right side first: the order the language leaves open, and the one
        // that keeps the address live for the shortest time.
        IRReg value = lowerValue(e->rhs);
        value = convert(value, referentType(typeOf(e->rhs)), t, e->line);
        const IRReg addr = lowerAddress(e->lhs);
        fn->emitStore(addr, value, size, flt, e->line);
        return value;
    }

    // a += b: the address is taken ONCE.  Lowering it as a = a + b would
    // evaluate a twice, which is wrong the moment a has a side effect.
    const IRReg addr = lowerAddress(e->lhs);
    const IRReg oldValue = fn->emitLoad(addr, size, flt, e->line);
    IRReg rhs = lowerValue(e->rhs);
    rhs = convert(rhs, referentType(typeOf(e->rhs)), t, e->line);

    const BinaryOp under = binaryOpUnderlying(e->op);
    IROp op = IR_Add;
    if (flt) {
        switch (under) {
        case BIN_Add: op = IR_FAdd; break;
        case BIN_Sub: op = IR_FSub; break;
        case BIN_Mul: op = IR_FMul; break;
        default:      op = IR_FDiv; break;
        }
    } else {
        BuiltinKind k;
        const bool uns = arithKind(t, k) && !builtinIsSigned(k);
        switch (under) {
        case BIN_Add: op = IR_Add; break;
        case BIN_Sub: op = IR_Sub; break;
        case BIN_Mul: op = IR_Mul; break;
        case BIN_Div: op = uns ? IR_UDiv : IR_Div; break;
        default:      op = uns ? IR_UMod : IR_Mod; break;
        }
    }
    const IRReg result = fn->emitBinary(op, oldValue, rhs, e->line);
    fn->emitStore(addr, result, size, flt, e->line);
    return result;
}

// Comparing against zero yields 0 or 1 and, for a double, collapses eight
// bytes to four -- both of which a logical operand needs.
IRReg Lowering::truth(IRReg value, Type *t, int line) {
    if (isFloatType(t)) {
        return fn->emitBinary(IR_FCmpNE, value, fn->emitFConst(0.0, line), line);
    }
    return fn->emitBinary(IR_CmpNE, value, fn->emitConst(0, line), line);
}

// The right side must not run when the left already decides -- a control-flow
// fact, so it lowers to branches rather than an operator.  The stored value is
// the truth of each side: `2 && 4` is 1, not 4.
IRReg Lowering::lowerShortCircuit(BinaryExpr *e) {
    const int slot = fn->addLocal("$sc", Layout::IntSize, false);
    const int done = fn->newLabel();

    const IRReg left = truth(lowerValue(e->lhs), referentType(typeOf(e->lhs)), e->line);
    IRReg addr = fn->emitLocalAddr(slot, e->line);
    fn->emitStore(addr, left, Layout::IntSize, false, e->line);

    if (e->op == BIN_LAnd) fn->emitBranchZero(left, done, e->line);
    else                   fn->emitBranchNZ(left, done, e->line);

    const IRReg right = truth(lowerValue(e->rhs), referentType(typeOf(e->rhs)), e->line);
    addr = fn->emitLocalAddr(slot, e->line);
    fn->emitStore(addr, right, Layout::IntSize, false, e->line);

    fn->emitLabel(done);
    addr = fn->emitLocalAddr(slot, e->line);
    return fn->emitLoad(addr, Layout::IntSize, false, e->line);
}

IRReg Lowering::lowerCall(CallExpr *e, bool wantsResult) {
    IdentExpr *callee = dynamic_cast<IdentExpr*>(e->callee);
    if (!callee) {
        diag.error(e->line, e->col, "internal: unsupported callee in lowering");
        return fn->emitConst(0, e->line);
    }
    // The semantic pass already chose which overload this is.
    Function *target = e->resolved;
    if (!target) {
        std::map<std::string, Function*>::const_iterator it = functions.find(callee->name);
        if (it != functions.end()) target = it->second;
    }
    const std::string sym = target ? symbolFor(target, "") : callee->name;
    const IRReg dest = takeObjectDest();
    const std::size_t mark = argTemps.size();
    std::vector<IRReg> args;
    if (returnsObject(target)) args.push_back(allocReturnSlot(target, e->line, dest));
    const std::vector<IRReg> rest = lowerArgs(e, target, 0);
    args.insert(args.end(), rest.begin(), rest.end());
    const IRReg out = fn->emitCall(sym, args, wantsResult, e->line);
    destroyArgTempsDownTo(mark, e->line);
    return out;
}

// Each argument is converted to its parameter's declared type.  Without this a
// double handed to an int parameter would arrive as raw bits.  `skip` is 1 when
// the callee is a method and `this` already occupies the first slot.
std::vector<IRReg> Lowering::lowerArgs(CallExpr *e, Function *target, std::size_t skip) {
    std::vector<IRReg> args;
    for (std::size_t i = 0; i < e->args.size(); ++i) {
        const std::size_t p = i + skip;
        Type *want = (target && p < target->params.size()) ? target->params[p]->type : 0;
        IRReg v;
        if (want && isReferenceType(want)) {
            // A reference parameter receives the object's address, never a
            // copy of its bytes.
            v = lowerAddress(e->args[i]);
        } else if (want && isObjectType(want)) {
            // By value: the address goes over, and the VM copies the object
            // into the parameter's own slot -- but a declared copy constructor
            // is what makes the copy, if the class wrote one.
            v = lowerByValueObject(want, e->args[i], e->line);
        } else {
            v = lowerValue(e->args[i]);
            if (want) v = convert(v, typeOf(e->args[i]), want, e->line);
        }
        args.push_back(v);
    }
    return args;
}

bool Lowering::lowerLayerValue(Expr *, IRReg &)   { return false; }
bool Lowering::lowerLayerAddress(Expr *, IRReg &) { return false; }

} // namespace cc

// ---------- Lower1.cpp ----------
// Lower1.cpp
//
// C++98 only.  See Lower1.h for the table of what becomes what.


#include <cstddef>

namespace cxx {

Lowering::Lowering(IRModule &module, const Layout &l, Diagnostics &d,
                   const std::map<std::string, ClassDecl*> &cls)
    : cc::Lowering(module, l, d), classes(cls), cachedBool(0) {}

// --- Looking things up ---

Lowering::~Lowering() {
    for (std::size_t i = 0; i < ownedTypes.size(); ++i) delete ownedTypes[i];
}

cc::Type *Lowering::boolType() {
    if (!cachedBool) {
        cachedBool = new BoolType();
        ownedTypes.push_back(cachedBool);
    }
    return cachedBool;
}

cc::Type *Lowering::makePointerToClass(const std::string &className) {
    cc::Type *t = new cc::PointerType(new ClassType(className));
    ownedTypes.push_back(t);
    return t;
}

cc::Type *Lowering::cloneType(cc::Type *t) {
    if (!t) return 0;
    if (cc::BuiltinType *bt = dynamic_cast<cc::BuiltinType*>(t)) return new cc::BuiltinType(bt->kind);
    if (ClassType *ct = dynamic_cast<ClassType*>(t)) return new ClassType(ct->className);
    if (cc::PointerType *pt = dynamic_cast<cc::PointerType*>(t)) return new cc::PointerType(cloneType(pt->base));
    if (ReferenceType *rt = dynamic_cast<ReferenceType*>(t)) return new ReferenceType(cloneType(rt->base));
    return 0;
}

ClassDecl *Lowering::findClass(const std::string &name) const {
    std::map<std::string, ClassDecl*>::const_iterator it = classes.find(name);
    return (it == classes.end()) ? 0 : it->second;
}

ClassDecl *Lowering::classOfType(cc::Type *t) const {
    if (!t) return 0;
    if (ReferenceType *rt = dynamic_cast<ReferenceType*>(t)) t = rt->base;
    if (cc::PointerType *pt = dynamic_cast<cc::PointerType*>(t)) t = pt->base;
    ClassType *ct = dynamic_cast<ClassType*>(t);
    return ct ? findClass(ct->className) : 0;
}

// Layout already flattened the base chain: an inherited field sits at the
// offset it had in the base, because the base subobject is at zero.
const FieldLayout *Lowering::findField(const std::string &className,
                                       const std::string &member) const {
    const ClassLayout *cl = layout.forClass(className);
    if (!cl) return 0;
    for (std::size_t i = 0; i < cl->fields.size(); ++i) {
        if (cl->fields[i].name == member) return &cl->fields[i];
    }
    return 0;
}

// The semantic pass already chose the overload; this finds one by name only,
// for the cases where no call is involved.
MethodDecl *Lowering::findMethod(ClassDecl *cd, const std::string &member) const {
    for (ClassDecl *c = cd; c; c = c->base) {
        for (std::size_t i = 0; i < c->members.size(); ++i) {
            MethodDecl *md = dynamic_cast<MethodDecl*>(c->members[i]);
            if (md && !md->isConstructor && !md->isDestructor && md->name == member) return md;
        }
    }
    return 0;
}

int Lowering::vtableSlotOf(const std::string &className, MethodDecl *m) const {
    const ClassLayout *cl = layout.forClass(className);
    if (!cl) return -1;
    const std::string want = mangleSignature(m->params);
    for (std::size_t s = 0; s < cl->vtable.size(); ++s) {
        // The slot holds the FINAL override, a different MethodDecl than the
        // one lookup found -- but the same NAME AND SIGNATURE.  Two virtuals
        // may share a name, and matching on the name alone picks whichever
        // was declared first.
        if (cl->vtable[s]->name == m->name &&
            mangleSignature(cl->vtable[s]->params) == want) {
            return static_cast<int>(s);
        }
    }
    return -1;
}

bool Lowering::classHasDestructor(ClassDecl *cd) const {
    for (ClassDecl *c = cd; c; c = c->base) if (c->dtor) return true;
    return false;
}

// --- Vtables, emitted as module data ---

void Lowering::lowerClasses() {
    std::map<std::string, ClassDecl*>::const_iterator it;
    for (it = classes.begin(); it != classes.end(); ++it) {
        const ClassLayout *cl = layout.forClass(it->first);
        if (!cl || !cl->hasVPtr) continue;
        IRVTable vt;
        vt.className = it->first;
        for (std::size_t s = 0; s < cl->vtable.size(); ++s) {
            MethodDecl *m = cl->vtable[s];
            if (m->isDestructor) vt.slots.push_back(mangleDestructor(m->ownerClass));
            else vt.slots.push_back(mangleOverload(m->ownerClass, m->name, m->params, m->isConstMethod));
        }
        mod.vtables.push_back(vt);
    }
}

// --- Declarations ---

void Lowering::lowerDecl(cc::Decl *d) {
    ClassDecl *cd = dynamic_cast<ClassDecl*>(d);
    if (cd) {
        const std::string saved = currentClass;
        currentClass = cd->name;
        for (std::size_t i = 0; i < cd->members.size(); ++i) {
            MethodDecl *md = dynamic_cast<MethodDecl*>(cd->members[i]);
            if (!md || !md->body) continue;
            std::string mangled;
            if (md->isConstructor)     mangled = mangleConstructor(cd->name, md->params);
            else if (md->isDestructor) mangled = mangleDestructor(cd->name);
            else                       mangled = mangleOverload(cd->name, md->name, md->params, md->isConstMethod);
            // A function with `this` in front -- all "member function" means
            // once the C++ is gone.
            lowerFunction(md, mangled, cd->name + "::" + md->name, true);
        }
        currentClass = saved;
        return;
    }
    cc::Lowering::lowerDecl(d);
}

// --- `this`, member addresses ---

IRReg Lowering::loadThis(int line) {
    const int slot = findSlot("this");
    if (slot < 0) return fn->emitConst(0, line);
    const IRReg addr = fn->emitLocalAddr(slot, line);
    return fn->emitLoad(addr, Layout::PointerSize, false, line);
}

// o.x needs o's ADDRESS, p->x needs p's VALUE.  Both end as an object address,
// so the arrow/dot distinction ends here and never reaches the IR.
IRReg Lowering::lowerObjectAddress(MemberAccessExpr *ma) {
    if (ma->isArrow) return lowerValue(ma->base);
    return lowerAddress(ma->base);
}

bool Lowering::lowerLayerAddress(cc::Expr *e, IRReg &out) {
    if (MemberAccessExpr *ma = dynamic_cast<MemberAccessExpr*>(e)) {
        const IRReg obj = lowerObjectAddress(ma);
        ClassDecl *cd = classOfType(typeOf(ma->base));
        // The base may be `this`, or a type lowering did not recompute.
        if (!cd) cd = findClass(currentClass);
        if (!cd) return false;
        const FieldLayout *f = findField(cd->name, ma->member);
        if (!f) return false;
        out = fn->emitFieldAddr(obj, f->offset, e->line);
        return true;
    }

    // An unqualified member name inside a method is `this->name`.
    if (cc::IdentExpr *id = dynamic_cast<cc::IdentExpr*>(e)) {
        if (findSlot(id->name) >= 0) return false;      // a real local wins
        if (currentClass.empty()) return false;
        const FieldLayout *f = findField(currentClass, id->name);
        if (!f) return false;
        out = fn->emitFieldAddr(loadThis(e->line), f->offset, e->line);
        return true;
    }
    return false;
}

bool Lowering::lowerLayerValue(cc::Expr *e, IRReg &out) {
    // An overloaded operator IS a call, so it is lowered as one: the left
    // operand is the object, the right is the single argument.  Semantic chose
    // the member; nothing is re-resolved here.
    if (cc::BinaryExpr *be = dynamic_cast<cc::BinaryExpr*>(e)) {
        if (be->resolvedOperator) {
            out = emitOperatorCall(be->resolvedOperator, be->lhs, be->rhs, be->line);
            return true;
        }
    }

    // Unary minus on an object is the same call with the right operand
    // missing -- the object is `this`, and there is nothing after it.
    if (cc::UnaryExpr *ue = dynamic_cast<cc::UnaryExpr*>(e)) {
        if (ue->resolvedOperator) {
            out = emitOperatorCall(ue->resolvedOperator, ue->operand, 0, ue->line);
            return true;
        }
    }

    if (dynamic_cast<ThisExpr*>(e)) {
        out = loadThis(e->line);
        return true;
    }

    if (BoolExpr *b = dynamic_cast<BoolExpr*>(e)) {
        out = fn->emitConst(b->value ? 1 : 0, e->line);
        return true;
    }

    if (MemberAccessExpr *ma = dynamic_cast<MemberAccessExpr*>(e)) {
        IRReg addr = IR_NoReg;
        if (!lowerLayerAddress(e, addr)) return false;
        ClassDecl *cd = classOfType(typeOf(ma->base));
        if (!cd) cd = findClass(currentClass);
        const FieldLayout *f = cd ? findField(cd->name, ma->member) : 0;
        // An array field decays, and an object field is not register-sized:
        // for both, the value IS the address, with nothing loaded.
        if (f && (isArrayType(f->type) || isObjectType(f->type))) {
            out = addr;
            return true;
        }
        out = fn->emitLoad(addr, f ? f->size : Layout::IntSize,
                           f && isFloatType(f->type), e->line);
        return true;
    }

    // An unqualified field name inside a method.
    if (cc::IdentExpr *id = dynamic_cast<cc::IdentExpr*>(e)) {
        if (findSlot(id->name) >= 0) return false;
        if (currentClass.empty()) return false;
        const FieldLayout *f = findField(currentClass, id->name);
        if (!f) return false;
        const IRReg addr = fn->emitFieldAddr(loadThis(e->line), f->offset, e->line);
        if (isArrayType(f->type) || isObjectType(f->type)) { out = addr; return true; }
        out = fn->emitLoad(addr, f->size, isFloatType(f->type), e->line);
        return true;
    }

    // T(args): space of the caller's own, constructed in place.  Its value is
    // its address, which is what every object-valued expression yields here.
    if (TempExpr *te = dynamic_cast<TempExpr*>(e)) {
        ClassDecl *cd = classOfMemberType(te->type);
        IRReg addr = takeObjectDest();
        if (addr == IR_NoReg) {
            const int size = layout.sizeOf(te->type);
            const int slot = declareLocal("$temp", size > 0 ? size : Layout::PointerSize, false);
            addr = fn->emitLocalAddr(slot, e->line);
        }
        if (cd) emitConstruct(cd, addr, te->args, e->line, te->resolvedCtor);
        out = addr;
        return true;
    }

    // Allocate, then construct -- two steps, in the order the language says.
    if (NewExpr *ne = dynamic_cast<NewExpr*>(e)) {
        ClassDecl *cd = classOfType(ne->allocType);
        int elemSize = layout.sizeOf(ne->allocType);
        if (elemSize <= 0) elemSize = Layout::IntSize;

        if (!ne->count) {
            out = fn->emitAlloc(elemSize, e->line);
            if (cd) emitConstruct(cd, out, ne->args, e->line, ne->resolvedCtor);
            return true;
        }

        // new T[n].  A literal bound is multiplied out here -- there is no
        // reason to compute at run time a product the compiler already knows --
        // and anything else is multiplied at run time, because the bound of a
        // heap array is a value like any other.  Either way the count ends up
        // in a register, because the constructor loop needs it.
        cc::NumberExpr *lit = dynamic_cast<cc::NumberExpr*>(ne->count);
        IRReg count, bytes;
        if (lit && lit->value >= 0) {
            count = fn->emitConst(lit->value, e->line);
            bytes = fn->emitConst(lit->value * elemSize, e->line);
        } else {
            count = lowerValue(ne->count);
            bytes = fn->emitBinary(IR_Mul, count, fn->emitConst(elemSize, e->line), e->line);
        }
        out = fn->emitAllocN(bytes, count, e->line);
        if (cd) emitHeapArrayConstruct(cd, out, count, elemSize, e->line);
        return true;
    }

    // Destroy, then release: the reverse of `new`.  The destructor reached is
    // whichever the vtable names, hence the non-virtual-destructor warning.
    if (DeleteExpr *de = dynamic_cast<DeleteExpr*>(e)) {
        const IRReg ptr = lowerValue(de->operand);
        ClassDecl *cd = classOfType(typeOf(de->operand));
        if (de->isArray) {
            // How many elements?  Not in the type -- the pointer is a T* -- and
            // not in a cookie either: new[] wrote the count into the block's
            // header, in the field the free list only uses while the block is
            // free.  A real ABI has to put a cookie in front of the payload
            // because free() cannot be asked anything; this machine can be
            // asked.
            if (cd) {
                int elemSize = Layout::PointerSize;
                if (cc::PointerType *pt = dynamic_cast<cc::PointerType*>(typeOf(de->operand))) {
                    elemSize = layout.sizeOf(pt->base);
                }
                if (elemSize <= 0) elemSize = Layout::IntSize;
                emitHeapArrayDestruct(cd, ptr, fn->emitArrayCount(ptr, e->line),
                                      elemSize, e->line);
            }
            fn->emitFree(ptr, e->line, true);
            out = ptr;
            return true;
        }
        if (cd) emitDestruct(cd, ptr, e->line);
        fn->emitFree(ptr, e->line);
        out = ptr;              // delete has no value; reusing ptr emits nothing
        return true;
    }
    return false;
}

// Member access, calls, `new` and `this` need the class table.
cc::Type *Lowering::typeOf(cc::Expr *e) {
    if (!e) return 0;

    if (dynamic_cast<BoolExpr*>(e)) return boolType();
    // A comparison yields bool, so a value stored from one is one byte wide.
    if (cc::BinaryExpr *be = dynamic_cast<cc::BinaryExpr*>(e)) {
        if (cc::binaryOpIsComparison(be->op) || cc::binaryOpIsLogical(be->op)) {
            return boolType();
        }
    }
    if (cc::UnaryExpr *ue = dynamic_cast<cc::UnaryExpr*>(e)) {
        if (ue->resolvedOperator) return ue->resolvedOperator->retType;
        if (ue->op == cc::UN_Not) return boolType();
    }

    if (dynamic_cast<ThisExpr*>(e)) {
        if (currentClass.empty()) return 0;
        return makePointerToClass(currentClass);
    }
    if (MemberAccessExpr *ma = dynamic_cast<MemberAccessExpr*>(e)) {
        ClassDecl *cd = classOfType(typeOf(ma->base));
        if (!cd) cd = findClass(currentClass);
        if (!cd) return 0;
        for (ClassDecl *c = cd; c; c = c->base) {
            for (std::size_t i = 0; i < c->members.size(); ++i) {
                FieldDecl *fd = dynamic_cast<FieldDecl*>(c->members[i]);
                if (fd && fd->name == ma->member) return fd->type;
                MethodDecl *md = dynamic_cast<MethodDecl*>(c->members[i]);
                if (md && !md->isConstructor && !md->isDestructor && md->name == ma->member) {
                    return md->retType;
                }
            }
        }
        return 0;
    }
    if (NewExpr *ne = dynamic_cast<NewExpr*>(e)) {
        cc::Type *p = new cc::PointerType(cloneType(ne->allocType));
        ownedTypes.push_back(p);
        return p;
    }
    if (cc::CallExpr *call = dynamic_cast<cc::CallExpr*>(e)) {
        if (MemberAccessExpr *cma = dynamic_cast<MemberAccessExpr*>(call->callee)) {
            return typeOf(cma);
        }
        // A plain call is the C layer's to answer.
        return cc::Lowering::typeOf(e);
    }
    // An unqualified field name inside a method.
    if (cc::IdentExpr *id = dynamic_cast<cc::IdentExpr*>(e)) {
        if (findSlot(id->name) < 0 && !currentClass.empty()) {
            for (ClassDecl *c = findClass(currentClass); c; c = c->base) {
                for (std::size_t i = 0; i < c->members.size(); ++i) {
                    FieldDecl *fd = dynamic_cast<FieldDecl*>(c->members[i]);
                    if (fd && fd->name == id->name) return fd->type;
                }
            }
        }
    }
    return cc::Lowering::typeOf(e);
}

// Declaring one CONSTRUCTS it; the block already knows to destroy it.
void Lowering::lowerVarDecl(cc::VarDecl *vd) {
    if (!vd) return;

    // P ps[3];  is three objects, each constructed.
    long count = 0;
    if (ClassDecl *elem = elementClassOf(vd->type, count)) {
        const int total = layout.sizeOf(vd->type);
        const int slot = declareLocal(vd->name, total > 0 ? total : Layout::PointerSize, false);
        localTypes[vd->name] = vd->type;
        const ClassLayout *cl = layout.forClass(elem->name);
        if (cl && count > 0) {
            emitArrayConstruct(elem, fn->emitLocalAddr(slot, vd->line), count,
                               cl->size, vd->line);
        }
        return;
    }

    ClassDecl *cd = 0;
    if (!dynamic_cast<ReferenceType*>(vd->type)) {
        ClassType *ct = dynamic_cast<ClassType*>(vd->type);
        if (ct) cd = findClass(ct->className);
    }
    if (!cd) { cc::Lowering::lowerVarDecl(vd); return; }

    const int size = layout.sizeOf(vd->type);
    const int slot = declareLocal(vd->name, size > 0 ? size : Layout::PointerSize, false);
    localTypes[vd->name] = vd->type;

    // T b = f();  or  T b = T(1);  -- the expression has an object to build and
    // b is where it belongs, so it is built THERE.  Copying it out of a
    // temporary afterwards would run the copy constructor a second time and
    // leave the temporary's own destructor unrun.
    if (vd->init && !vd->hasCtorArgs && yieldsObject(vd->init)) {
        const IRReg dest = fn->emitLocalAddr(slot, vd->line);
        objectDest = dest;
        const IRReg got = lowerObjectValue(vd->init);
        const bool builtInPlace = (objectDest == IR_NoReg);
        objectDest = IR_NoReg;
        // A shape that made its own space anyway still has to reach b.
        if (!builtInPlace) fn->emitMemCopy(dest, got, size, vd->line);
        return;
    }

    // P b = a;  copies a.  A declared copy constructor is the copy if there is
    // one; otherwise the copy is memberwise, which carries the vptr and is
    // right for two objects of one class.
    if (vd->init && !vd->hasCtorArgs) {
        if (MethodDecl *copyCtor = copyConstructorOf(cd)) {
            // Name the constructor: by argument COUNT alone this picked
            // whichever one-argument constructor came first, which for
            // `T(int)` beside `T(const T&)` was the wrong one.
            std::vector<cc::Expr*> one;
            one.push_back(vd->init);
            emitConstruct(cd, fn->emitLocalAddr(slot, vd->line), one, vd->line, copyCtor);
            return;
        }
        if (!isAddressable(vd->init) && !yieldsObject(vd->init)) {
            diag.error(vd->line, vd->col,
                       "an object can only be copied from another object");
            return;
        }
        const IRReg src = lowerObjectValue(vd->init);
        const IRReg dst = fn->emitLocalAddr(slot, vd->line);
        fn->emitMemCopy(dst, src, size, vd->line);
        // The bytes copied include the source's vptr, and the source may be a
        // DERIVED object being sliced into a base.  Rewriting the vptr makes
        // the copy the class it was declared as, rather than one it is not.
        emitVPtrStore(cd, dst, vd->line);
        return;
    }
    emitConstruct(cd, fn->emitLocalAddr(slot, vd->line), vd->ctorArgs, vd->line,
                  vd->resolvedCtor);
}

// A constructor taking one argument of this same class -- by reference, since
// taking it by value would need the very copy being defined.
MethodDecl *Lowering::copyConstructorOf(ClassDecl *cd) const {
    if (!cd) return 0;
    for (std::size_t i = 0; i < cd->ctors.size(); ++i) {
        MethodDecl *c = cd->ctors[i];
        if (c->params.size() != 1) continue;
        cc::Type *p = c->params[0]->type;
        ReferenceType *rt = dynamic_cast<ReferenceType*>(p);
        if (!rt) continue;
        ClassType *ct = dynamic_cast<ClassType*>(rt->base);
        if (ct && ct->className == cd->name) return c;
    }
    return 0;
}

ClassDecl *Lowering::elementClassOf(cc::Type *t, long &count) const {
    count = 1;
    cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t);
    if (!at) return 0;
    while (at) {
        count *= at->count;
        t = at->element;
        at = dynamic_cast<cc::ArrayType*>(t);
    }
    ClassType *ct = dynamic_cast<ClassType*>(t);
    return ct ? findClass(ct->className) : 0;
}

// The same two runs as emitArrayConstruct/emitArrayDestruct below, but as
// loops: a heap array's length is not known until the program says it, so the
// run cannot be unrolled -- and should not be even for `new C[1000]`, where
// unrolling would emit a thousand calls.
void Lowering::emitHeapArrayConstruct(ClassDecl *cd, IRReg base, IRReg count,
                                      int elemSize, int line) {
    const int i = declareLocal("$i", 8, false);
    const IRReg slot = fn->emitLocalAddr(i, line);
    fn->emitStore(slot, fn->emitConst(0, line), 8, false, line);

    const int top = fn->newLabel();
    const int end = fn->newLabel();
    fn->emitLabel(top);
    const IRReg iv = fn->emitLoad(fn->emitLocalAddr(i, line), 8, false, line);
    fn->emitBranchZero(fn->emitBinary(IR_CmpLT, iv, count, line), end, line);

    const IRReg off = fn->emitBinary(IR_Mul, iv, fn->emitConst(elemSize, line), line);
    std::vector<cc::Expr*> none;
    emitConstruct(cd, fn->emitBinary(IR_Add, base, off, line), none, line);

    const IRReg next = fn->emitBinary(IR_Add, iv, fn->emitConst(1, line), line);
    fn->emitStore(fn->emitLocalAddr(i, line), next, 8, false, line);
    fn->emitJump(top, line);
    fn->emitLabel(end);
}

// Backwards, for the same reason the unrolled one runs backwards: the last
// element built is the first destroyed.
void Lowering::emitHeapArrayDestruct(ClassDecl *cd, IRReg base, IRReg count,
                                     int elemSize, int line) {
    const int i = declareLocal("$i", 8, false);
    fn->emitStore(fn->emitLocalAddr(i, line), count, 8, false, line);

    const int top = fn->newLabel();
    const int end = fn->newLabel();
    fn->emitLabel(top);
    const IRReg iv = fn->emitLoad(fn->emitLocalAddr(i, line), 8, false, line);
    fn->emitBranchZero(iv, end, line);

    const IRReg prev = fn->emitBinary(IR_Sub, iv, fn->emitConst(1, line), line);
    fn->emitStore(fn->emitLocalAddr(i, line), prev, 8, false, line);
    const IRReg off = fn->emitBinary(IR_Mul, prev, fn->emitConst(elemSize, line), line);
    emitDestruct(cd, fn->emitBinary(IR_Add, base, off, line), line, true);
    fn->emitJump(top, line);
    fn->emitLabel(end);
}

// Element 0 first, exactly as a single object is built before the next one.
void Lowering::emitArrayConstruct(ClassDecl *cd, IRReg base, long count,
                                  int elemSize, int line) {
    std::vector<cc::Expr*> none;
    for (long i = 0; i < count; ++i) {
        emitConstruct(cd, fn->emitFieldAddr(base, static_cast<int>(i) * elemSize, line),
                      none, line);
    }
}

// Each element from the element it corresponds to.  No syntax reaches inside
// an array member, so an initialiser list cannot say this -- but the copy
// constructor can still be CALLED on every element, which is what a copy of an
// array member is.  Unrolled for the same reason the default form is: the
// bound is a constant here.
void Lowering::emitArrayCopyConstruct(ClassDecl *cd, IRReg dst, IRReg src,
                                      long count, int elemSize, int line) {
    MethodDecl *copyCtor = copyConstructorOf(cd);
    if (!copyCtor) return;
    for (long i = 0; i < count; ++i) {
        const int off = static_cast<int>(i) * elemSize;
        std::vector<IRReg> callArgs;
        callArgs.push_back(fn->emitFieldAddr(dst, off, line));
        // A reference parameter receives the address, exactly as a call does.
        callArgs.push_back(fn->emitFieldAddr(src, off, line));
        fn->emitCall(mangleConstructor(cd->name, copyCtor->params), callArgs, false, line);
    }
}

// Reverse, for the same reason members are destroyed in reverse.
void Lowering::emitArrayDestruct(ClassDecl *cd, IRReg base, long count,
                                 int elemSize, int line) {
    for (long i = count; i > 0; --i) {
        emitDestruct(cd, fn->emitFieldAddr(base, static_cast<int>(i - 1) * elemSize, line),
                     line, true);
    }
}

// A call or an overloaded operator whose result is an object: it has no name,
// but it does have a place -- the slot the caller supplied for it.
bool Lowering::yieldsObject(cc::Expr *e) const {
    if (dynamic_cast<TempExpr*>(e)) return true;
    if (cc::CallExpr *c = dynamic_cast<cc::CallExpr*>(e)) {
        return c->resolved && dynamic_cast<ClassType*>(c->resolved->retType) != 0;
    }
    if (cc::BinaryExpr *b = dynamic_cast<cc::BinaryExpr*>(e)) {
        return b->resolvedOperator
            && dynamic_cast<ClassType*>(b->resolvedOperator->retType) != 0;
    }
    if (cc::UnaryExpr *u = dynamic_cast<cc::UnaryExpr*>(e)) {
        return u->resolvedOperator
            && dynamic_cast<ClassType*>(u->resolvedOperator->retType) != 0;
    }
    return false;
}

// Only something with a place in memory can be copied from.
bool Lowering::isAddressable(cc::Expr *e) const {
    if (dynamic_cast<cc::IdentExpr*>(e))    return true;
    if (dynamic_cast<MemberAccessExpr*>(e)) return true;
    if (cc::UnaryExpr *u = dynamic_cast<cc::UnaryExpr*>(e)) return u->op == cc::UN_Deref;
    return false;
}

// A global object is constructed exactly as a local one is; only where its
// storage lives differs.
void Lowering::initGlobal(cc::VarDecl *vd, IRReg addr) {
    if (ClassDecl *cd = classOfMemberType(vd->type)) {
        emitConstruct(cd, addr, vd->ctorArgs, vd->line, vd->resolvedCtor);
        return;
    }
    cc::Lowering::initGlobal(vd, addr);
}

void Lowering::destroyGlobal(cc::VarDecl *vd) {
    long count = 1;
    ClassDecl *cd = classOfMemberType(vd->type, count);
    // Ask before addressing it: a global with nothing to destroy should leave
    // no trace in the teardown function at all.
    if (!cd || !classHasDestructor(cd)) return;
    const IRReg addr = fn->emitGlobalAddr(vd->name, vd->line);
    if (count != 1) {
        const ClassLayout *cl = layout.forClass(cd->name);
        if (cl) emitArrayDestruct(cd, addr, count, cl->size, vd->line);
        return;
    }
    emitDestruct(cd, addr, vd->line, true);
}

bool Lowering::isReferenceExpr(cc::Expr *e) {
    cc::Type *t = typeOf(e);
    return t && dynamic_cast<ReferenceType*>(t) != 0;
}

// The C layer cannot copy a class or reference type; this layer can.  The
// copy is the caller's -- whoever asked is about to own it.
cc::Type *Lowering::cloneForeignType(cc::Type *t) {
    return cloneType(t);
}

bool Lowering::isReferenceType(cc::Type *t) {
    return dynamic_cast<ReferenceType*>(t) != 0;
}

bool Lowering::isObjectType(cc::Type *t) {
    return classOfMemberType(t) != 0;
}

void Lowering::reassertVPtr(cc::Type *t, IRReg addr, int line) {
    if (ClassDecl *cd = classOfMemberType(t)) emitVPtrStore(cd, addr, line);
}

cc::Type *Lowering::referentType(cc::Type *t) {
    ReferenceType *rt = dynamic_cast<ReferenceType*>(t);
    return rt ? rt->base : t;
}

bool Lowering::isBoolType(cc::Type *t) {
    return dynamic_cast<BoolType*>(t) != 0;
}

// --- Calls, including the one that matters ---

// object.operatorX(argument) -- with the object passed as `this`, and the
// argument obeying the same by-reference rule every other parameter does.
// A copy is a construction, so a declared copy constructor makes it.  Without
// this the bytes were copied and the constructor never ran, which is a silent
// wrong answer for any class whose copy does more than move bytes.
// `return obj;` is a copy into the caller's slot, and a copy is a construction:
// a declared copy constructor is what has to make it.  Copying the bytes was a
// silent wrong answer for any class whose copy does more than move them, and a
// dangerous one for a class that owns memory -- the destructors that run
// immediately after this then free what the caller has just been handed.
//
// The construction goes straight into `dest`, so there is no temporary to
// destroy, and it happens before emitAllOpenScopeExits() for the same reason
// the byte copy did: the object being returned is one of the locals about to
// be torn down.
void Lowering::emitReturnObject(IRReg dest, cc::Expr *e, int line) {
    ClassDecl *cd = classOfMemberType(currentReturnType);
    MethodDecl *copyCtor = copyConstructorOf(cd);
    if (!cd || !copyCtor) { cc::Lowering::emitReturnObject(dest, e, line); return; }

    std::vector<cc::Expr*> one;
    one.push_back(e);
    emitConstruct(cd, dest, one, line, copyCtor);
}

IRReg Lowering::lowerByValueObject(cc::Type *want, cc::Expr *e, int line) {
    ClassDecl *cd = classOfMemberType(want);
    MethodDecl *copyCtor = copyConstructorOf(cd);
    if (!cd || !copyCtor) return lowerObjectValue(e);

    const int size = layout.sizeOf(want);
    const int tmp = declareLocal("$arg", size > 0 ? size : Layout::PointerSize, false);
    const IRReg addr = fn->emitLocalAddr(tmp, line);
    std::vector<cc::Expr*> one;
    one.push_back(e);
    emitConstruct(cd, addr, one, line, copyCtor);

    // The copy is the caller's, so the caller destroys it once the call it was
    // made for has returned.
    if (classHasDestructor(cd)) {
        ArgTemp t;
        t.slot = tmp;
        t.type = want;
        argTemps.push_back(t);
    }
    return addr;
}

void Lowering::destroyArgTempsDownTo(std::size_t mark, int line) {
    while (argTemps.size() > mark) {
        const ArgTemp t = argTemps.back();
        argTemps.pop_back();
        if (ClassDecl *cd = classOfMemberType(t.type)) {
            emitDestruct(cd, fn->emitLocalAddr(t.slot, line), line, true);
        }
    }
}

// One operand at a time, each obeying the rule its parameter declares.
IRReg Lowering::lowerOperandFor(cc::Type *want, cc::Expr *e, int line) {
    if (want && isReferenceType(want)) return lowerAddress(e);
    // By value: an object's ADDRESS goes over and the VM copies it into the
    // parameter's own slot, which is the copy the callee owns.
    if (want && isObjectType(want))    return lowerByValueObject(want, e, line);
    IRReg v = lowerValue(e);
    if (want) v = convert(v, referentType(typeOf(e)), want, line);
    return v;
}

// a[i] on a class is a method call: the object is `this`, the index the one
// argument.  A reference return makes the RESULT an address, which is what
// lets  t[1] = 42;  assign through it.
IRReg Lowering::lowerIndexOperator(cc::IndexExpr *e) {
    const IRReg dest = takeObjectDest();   // before any operand is lowered
    MethodDecl *op = dynamic_cast<MethodDecl*>(e->resolvedOperator);
    if (!op) {
        diag.error(e->line, e->col, "internal: operator[] was not resolved");
        return fn->emitConst(0, e->line);
    }
    std::vector<IRReg> args;
    args.push_back(lowerObjectValue(e->base));
    if (returnsObject(op)) args.push_back(allocReturnSlot(op, e->line, dest));
    args.push_back(lowerOperandFor(op->params.empty() ? 0 : op->params[0]->type,
                                   e->index, e->line));
    return fn->emitCall(mangleOverload(op->ownerClass, op->name, op->params, op->isConstMethod),
                        args, true, e->line);
}

// A member operator is a method call on the left operand.  A non-member is an
// ordinary two-argument call -- and the only form that can take a class on the
// RIGHT, which is what makes  3 * v  work.
IRReg Lowering::emitOperatorCall(cc::Function *op, cc::Expr *lhsExpr,
                                 cc::Expr *rhsExpr, int line) {
    const IRReg dest = takeObjectDest();   // before any operand is lowered
    MethodDecl *asMember = dynamic_cast<MethodDecl*>(op);
    std::vector<IRReg> args;

    if (asMember) {
        args.push_back(lowerObjectValue(lhsExpr));          // `this`
        if (returnsObject(op)) args.push_back(allocReturnSlot(op, line, dest));
        // A unary operator has no right operand and no parameter for one.
        if (rhsExpr)
            args.push_back(lowerOperandFor(op->params.empty() ? 0 : op->params[0]->type,
                                           rhsExpr, line));
        return fn->emitCall(mangleOverload(asMember->ownerClass, op->name, op->params, asMember->isConstMethod),
                            args, true, line);
    }

    if (returnsObject(op)) args.push_back(allocReturnSlot(op, line, dest));
    args.push_back(lowerOperandFor(op->params.size() > 0 ? op->params[0]->type : 0,
                                   lhsExpr, line));
    if (rhsExpr)
        args.push_back(lowerOperandFor(op->params.size() > 1 ? op->params[1]->type : 0,
                                       rhsExpr, line));
    return fn->emitCall(symbolFor(op, ""), args, true, line);
}

IRReg Lowering::lowerCall(cc::CallExpr *e, bool wantsResult) {
    const IRReg dest = takeObjectDest();   // before any operand is lowered
    MemberAccessExpr *ma = dynamic_cast<MemberAccessExpr*>(e->callee);

    // obj(args) -- the callee is an OBJECT, and the analysis resolved the call
    // to its operator().  The object is `this`; everything else is an ordinary
    // method call.
    if (!ma) {
        MethodDecl *callOp = dynamic_cast<MethodDecl*>(e->resolved);
        if (callOp && callOp->name == "operator()") {
            const std::size_t mark = argTemps.size();
            std::vector<IRReg> args;
            args.push_back(lowerObjectValue(e->callee));
            if (returnsObject(callOp)) args.push_back(allocReturnSlot(callOp, e->line, dest));
            const std::vector<IRReg> rest = lowerArgs(e, callOp, 0);
            args.insert(args.end(), rest.begin(), rest.end());
            const IRReg out = fn->emitCall(
                mangleOverload(callOp->ownerClass, callOp->name,
                               callOp->params, callOp->isConstMethod),
                args, wantsResult, e->line);
            destroyArgTempsDownTo(mark, e->line);
            return out;
        }
    }

    if (!ma) {
        // A bare name inside a method may still be a method call on `this`.
        cc::IdentExpr *id = dynamic_cast<cc::IdentExpr*>(e->callee);
        if (id && !currentClass.empty() && findSlot(id->name) < 0) {
            ClassDecl *cd = findClass(currentClass);
            MethodDecl *m = dynamic_cast<MethodDecl*>(e->resolved);
            if (!m && cd) m = findMethod(cd, id->name);
            if (m) {
                std::vector<IRReg> args;
                args.push_back(loadThis(e->line));
                if (returnsObject(m)) args.push_back(allocReturnSlot(m, e->line, dest));
                const std::vector<IRReg> rest = lowerArgs(e, m, 0);
                args.insert(args.end(), rest.begin(), rest.end());
                return fn->emitCall(mangleOverload(m->ownerClass, m->name, m->params, m->isConstMethod),
                                    args, wantsResult, e->line);
            }
        }
        // Not a C++ call after all.  Hand the destination back, or the C
        // layer makes a temporary for a result that already has a home.
        objectDest = dest;
        return cc::Lowering::lowerCall(e, wantsResult);
    }

    const IRReg object = lowerObjectAddress(ma);
    ClassDecl *cd = classOfType(typeOf(ma->base));
    if (!cd) cd = findClass(currentClass);
    // The overload was decided during analysis; using it here keeps one answer.
    MethodDecl *m = dynamic_cast<MethodDecl*>(e->resolved);
    if (!m && cd) m = findMethod(cd, ma->member);
    if (!m) {
        diag.error(e->line, e->col, "internal: method not found while lowering a call");
        return fn->emitConst(0, e->line);
    }

    // Every method takes `this`, virtual or not: dispatch decides WHICH
    // function runs, not how it is called.
    std::vector<IRReg> args;
    args.push_back(object);
    if (returnsObject(m)) args.push_back(allocReturnSlot(m, e->line, dest));
    const std::vector<IRReg> rest = lowerArgs(e, m, 0);
    args.insert(args.end(), rest.begin(), rest.end());

    // Through a pointer or reference the static type is only a lower bound, so
    // the call must dispatch.  On a named object it cannot be anything but that
    // class's override, so the slot lookup is pure overhead.  This is the one
    // optimisation the pass performs, and every real compiler performs it too.
    bool dynamicType = true;
    if (!ma->isArrow) {
        cc::Type *bt = typeOf(ma->base);
        if (bt && dynamic_cast<ClassType*>(bt) != 0) dynamicType = false;
    }

    if (m->isVirtual && dynamicType) {
        // All of dynamic dispatch: the slot is a compile-time constant, the
        // function found there is not.
        const int slot = vtableSlotOf(cd->name, m);
        if (slot >= 0) {
            const IRReg target = fn->emitVCallTarget(object, slot, e->line);
            return fn->emitCallIndirect(target, args, wantsResult, e->line);
        }
    }
    if (m->isVirtual && !dynamicType) {
        // The final override for this exact class.
        const ClassLayout *cl = layout.forClass(cd->name);
        if (cl) {
            const std::string want = mangleSignature(m->params);
            for (std::size_t s = 0; s < cl->vtable.size(); ++s) {
                if (cl->vtable[s]->name == m->name &&
                    mangleSignature(cl->vtable[s]->params) == want) {
                    m = cl->vtable[s];
                    break;
                }
            }
        }
    }
    return fn->emitCall(mangleOverload(m->ownerClass, m->name, m->params, m->isConstMethod), args,
                        wantsResult, e->line);
}

// --- Object lifetime ---

void Lowering::emitVPtrStore(ClassDecl *cd, IRReg objectAddr, int line) {
    const ClassLayout *cl = layout.forClass(cd->name);
    if (!cl || !cl->hasVPtr) return;
    // Offset 0 -- the decision that makes an upcast free.
    const IRReg vt = fn->emitGlobalAddr(mangleVTable(cd->name), line);
    const IRReg at = fn->emitFieldAddr(objectAddr, 0, line);
    fn->emitStore(at, vt, Layout::PointerSize, false, line);
}

void Lowering::emitConstruct(ClassDecl *cd, IRReg objectAddr,
                             const std::vector<cc::Expr*> &args, int line,
                             cc::Function *chosen) {
    // The semantic pass chose, by signature.  The fallback by argument count
    // is for the constructions lowering makes itself -- a member, an array
    // element, a base -- which are always the no-argument one.
    MethodDecl *ctor = dynamic_cast<MethodDecl*>(chosen);
    if (!ctor) {
        for (std::size_t i = 0; i < cd->ctors.size(); ++i) {
            if (cd->ctors[i]->params.size() == args.size()) { ctor = cd->ctors[i]; break; }
        }
    }
    if (!ctor) {
        // Nothing to call: either the class declares no constructor, or it
        // declares none that takes these arguments and the analysis has
        // already said so.  A default construction still has to set the vptr,
        // which is the one thing an object needs before anything can be
        // called on it.  Testing for a missing constructor rather than for an
        // EMPTY constructor list matters now that a class may hold nothing but
        // constructors the compiler generated.
        if (args.empty()) emitVPtrStore(cd, objectAddr, line);
        return;
    }

    std::vector<IRReg> callArgs;
    callArgs.push_back(objectAddr);
    for (std::size_t i = 0; i < args.size(); ++i) {
        cc::Type *want = i < ctor->params.size() ? ctor->params[i]->type : 0;
        // Same rules a call obeys: a reference parameter receives the object's
        // ADDRESS, an object by value receives one too and the VM copies it.
        callArgs.push_back(lowerOperandFor(want, args[i], line));
    }
    fn->emitCall(mangleConstructor(cd->name, ctor->params), callArgs, false, line);
}

// `concreteType`: the exact class is known (a named local), so the destructor
// is called directly, for the same reason a method call on one is.
void Lowering::emitDestruct(ClassDecl *cd, IRReg objectAddr, int line, bool concreteType) {
    if (!classHasDestructor(cd)) return;
    // Find the class that actually declares one, walking up.
    ClassDecl *owner = cd;
    while (owner && !owner->dtor) owner = owner->base;
    if (!owner) return;
    if (concreteType) {
        const ClassLayout *cl = layout.forClass(cd->name);
        if (cl) {
            for (std::size_t s = 0; s < cl->vtable.size(); ++s) {
                if (cl->vtable[s]->isDestructor) { owner = findClass(cl->vtable[s]->ownerClass); break; }
            }
        }
        if (!owner) owner = cd;
    }

    std::vector<IRReg> callArgs;
    callArgs.push_back(objectAddr);
    if (owner->dtor->isVirtual && !concreteType) {
        const ClassLayout *cl = layout.forClass(cd->name);
        int slot = -1;
        if (cl) {
            for (std::size_t s = 0; s < cl->vtable.size(); ++s) {
                if (cl->vtable[s]->isDestructor) { slot = static_cast<int>(s); break; }
            }
        }
        if (slot >= 0) {
            const IRReg target = fn->emitVCallTarget(objectAddr, slot, line);
            fn->emitCallIndirect(target, callArgs, false, line);
            return;
        }
    }
    fn->emitCall(mangleDestructor(owner->name), callArgs, false, line);
}

// In the order Layout fixed: base, vptr, members.
void Lowering::emitPrologue(cc::Function *f) {
    MethodDecl *md = dynamic_cast<MethodDecl*>(f);
    if (!md || !md->isConstructor) return;
    ClassDecl *cd = findClass(md->ownerClass);
    if (!cd) return;

    const IRReg self = loadThis(f->line);

    // 1. the base subobject
    if (cd->base) {
        bool wroteBase = false;
        for (std::size_t i = 0; i < md->memberInits.size(); ++i) {
            if (!md->memberInits[i].isBase) continue;
            emitConstruct(cd->base, self, md->memberInits[i].args, f->line,
                          md->memberInits[i].resolvedCtor);
            wroteBase = true;
            break;
        }
        if (!wroteBase) {
            std::vector<cc::Expr*> none;
            emitConstruct(cd->base, self, none, f->line);
        }
    }

    // 2. the vptr: AFTER the base, so the base ctor still dispatched as a
    //    base; BEFORE the members, so the body can call its own virtuals.
    emitVPtrStore(cd, self, f->line);

    // 3. members, in DECLARATION order however the list was written
    for (std::size_t i = 0; i < cd->members.size(); ++i) {
        FieldDecl *fd = dynamic_cast<FieldDecl*>(cd->members[i]);
        if (!fd) continue;

        const FieldLayout *fl = findField(cd->name, fd->name);
        if (!fl) continue;

        // Whatever the initialiser list says about this member, if anything.
        const MemberInit *mi = 0;
        for (std::size_t k = 0; k < md->memberInits.size(); ++k) {
            if (!md->memberInits[k].isBase && md->memberInits[k].name == fd->name) {
                mi = &md->memberInits[k];
                break;
            }
        }
        const int line = mi ? mi->line : f->line;

        // A member that is itself a class is CONSTRUCTED, not assigned -- and
        // it is constructed even when the initialiser list never mentions it.
        long memberCount = 1;
        if (ClassDecl *member = classOfMemberType(fd->type, memberCount)) {
            const IRReg addr = fn->emitFieldAddr(loadThis(f->line), fl->offset, line);
            if (memberCount != 1) {
                const ClassLayout *ml = layout.forClass(member->name);
                if (!mi || mi->args.empty()) {
                    if (ml) emitArrayConstruct(member, addr, memberCount, ml->size, line);
                } else if (ml && copyConstructorOf(member)) {
                    // Named by a generated copy constructor: every element is
                    // copy-constructed from its opposite number.
                    emitArrayCopyConstruct(member, addr, lowerAddress(mi->args[0]),
                                           memberCount, ml->size, line);
                } else {
                    // Elements with no copy constructor to call are their
                    // bytes, and a whole-array move is the copy.
                    fn->emitMemCopy(addr, lowerAddress(mi->args[0]), fl->size, line);
                }
            } else if (mi) {
                emitConstruct(member, addr, mi->args, line, mi->resolvedCtor);
            } else {
                std::vector<cc::Expr*> none;
                emitConstruct(member, addr, none, line);
            }
            continue;
        }

        // An array of scalars, named by a generated copy constructor: the same
        // whole-array move, and for these it is the whole answer -- the
        // elements are values, and their bytes are what a copy of them is.
        if (mi && !mi->args.empty() && dynamic_cast<cc::ArrayType*>(fd->type)) {
            const IRReg addr = fn->emitFieldAddr(loadThis(f->line), fl->offset, line);
            fn->emitMemCopy(addr, lowerAddress(mi->args[0]), fl->size, line);
            continue;
        }

        // A scalar member is only touched when the list names it; C leaves an
        // uninitialised variable alone and so does this.
        if (!mi || mi->args.empty()) continue;
        IRReg value = lowerValue(mi->args[0]);
        value = convert(value, typeOf(mi->args[0]), fd->type, line);
        const IRReg addr = fn->emitFieldAddr(loadThis(f->line), fl->offset, line);
        fn->emitStore(addr, value, fl->size, isFloatType(fd->type), line);
    }
}

// A field's class, when the field is an object rather than a pointer or a
// reference to one -- only an object is constructed with its container.
ClassDecl *Lowering::classOfMemberType(cc::Type *t) const {
    long ignored = 0;
    return classOfMemberType(t, ignored);
}

// `count` comes back as the number of objects the member holds: 1 for a plain
// one, the product of the dimensions for an array of them.  The array case was
// missed here while the local-variable path handled it, so `E arr[2];` as a
// member was neither constructed nor destroyed.
ClassDecl *Lowering::classOfMemberType(cc::Type *t, long &count) const {
    count = 1;
    if (dynamic_cast<cc::PointerType*>(t)) return 0;
    if (dynamic_cast<ReferenceType*>(t))   return 0;
    while (cc::ArrayType *at = dynamic_cast<cc::ArrayType*>(t)) {
        count *= at->count;
        t = at->element;
    }
    ClassType *ct = dynamic_cast<ClassType*>(t);
    return ct ? findClass(ct->className) : 0;
}

// Members backwards, then the base.  The body has already run, which makes
// this the exact reverse of construction.
void Lowering::emitEpilogue(cc::Function *f) {
    MethodDecl *md = dynamic_cast<MethodDecl*>(f);
    if (!md || !md->isDestructor) return;
    ClassDecl *cd = findClass(md->ownerClass);
    if (!cd) return;

    // Members in reverse declaration order, before the base -- the exact
    // reverse of the order emitPrologue built them in.
    for (std::size_t i = cd->members.size(); i > 0; --i) {
        FieldDecl *fd = dynamic_cast<FieldDecl*>(cd->members[i - 1]);
        if (!fd) continue;
        long memberCount = 1;
        ClassDecl *member = classOfMemberType(fd->type, memberCount);
        if (!member || !classHasDestructor(member)) continue;
        const FieldLayout *fl = findField(cd->name, fd->name);
        if (!fl) continue;
        const IRReg at = fn->emitFieldAddr(loadThis(f->line), fl->offset, f->line);
        if (memberCount != 1) {
            const ClassLayout *ml = layout.forClass(member->name);
            if (ml) emitArrayDestruct(member, at, memberCount, ml->size, f->line);
            continue;
        }
        emitDestruct(member, at, f->line, true);
    }

    if (!cd->base) return;
    if (!classHasDestructor(cd->base)) return;

    ClassDecl *owner = cd->base;
    while (owner && !owner->dtor) owner = owner->base;
    if (!owner) return;
    std::vector<IRReg> args;
    args.push_back(loadThis(f->line));
    // Always direct, never through the vtable: the derived part is gone.
    fn->emitCall(mangleDestructor(owner->name), args, false, f->line);
}

// In the order the semantic pass recorded: reverse of construction.
void Lowering::emitScopeExit(cc::CompoundStmt *block) {
    for (std::size_t i = 0; i < block->destroyAtExit.size(); ++i) {
        cc::VarDecl *vd = block->destroyAtExit[i];
        const int slot = findSlot(vd->name);
        if (slot < 0) continue;

        long count = 0;
        if (ClassDecl *elem = elementClassOf(vd->type, count)) {
            const ClassLayout *cl = layout.forClass(elem->name);
            if (cl && count > 0) {
                emitArrayDestruct(elem, fn->emitLocalAddr(slot, vd->line), count,
                                  cl->size, vd->line);
            }
            continue;
        }

        ClassDecl *cd = classOfType(vd->type);
        if (!cd) continue;
        emitDestruct(cd, fn->emitLocalAddr(slot, vd->line), vd->line, true);
    }
}

// A return leaves every open block at once, innermost first.
void Lowering::emitScopeExitsDownTo(std::size_t depth) {
    for (std::size_t i = openBlocks.size(); i > depth; --i) emitScopeExit(openBlocks[i - 1]);
}

void Lowering::emitAllOpenScopeExits() {
    for (std::size_t i = openBlocks.size(); i > 0; --i) emitScopeExit(openBlocks[i - 1]);
}

} // namespace cxx

// ---------- Bytecode.cpp ----------
// Bytecode.cpp
//
// C++98 only.


#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

const char *opCodeName(OpCode op) {
    switch (op) {
    case OP_PushConst:    return "push";
    case OP_PushFConst:   return "fpush";
    case OP_LoadReg:      return "ldr";
    case OP_StoreReg:     return "str";
    case OP_Pop:          return "pop";
    case OP_LocalAddr:    return "local";
    case OP_StaticAddr:   return "static";
    case OP_FieldAddr:    return "field";
    case OP_FuncAddr:     return "funcaddr";
    case OP_Load:         return "load";
    case OP_Store:        return "store";
    case OP_MemCopy:      return "memcpy";
    case OP_Add:          return "add";
    case OP_Sub:          return "sub";
    case OP_Mul:          return "mul";
    case OP_Div:          return "div";
    case OP_Mod:          return "mod";
    case OP_UDiv:         return "udiv";
    case OP_UMod:         return "umod";
    case OP_Shl:          return "shl";
    case OP_Shr:          return "shr";
    case OP_UShr:         return "ushr";
    case OP_Neg:          return "neg";
    case OP_Not:          return "not";
    case OP_FAdd:         return "fadd";
    case OP_FSub:         return "fsub";
    case OP_FMul:         return "fmul";
    case OP_FDiv:         return "fdiv";
    case OP_FNeg:         return "fneg";
    case OP_CmpEQ:        return "cmp.eq";
    case OP_CmpNE:        return "cmp.ne";
    case OP_CmpLT:        return "cmp.lt";
    case OP_CmpGT:        return "cmp.gt";
    case OP_CmpLE:        return "cmp.le";
    case OP_CmpGE:        return "cmp.ge";
    case OP_UCmpLT:       return "ucmp.lt";
    case OP_UCmpGT:       return "ucmp.gt";
    case OP_UCmpLE:       return "ucmp.le";
    case OP_UCmpGE:       return "ucmp.ge";
    case OP_FCmpEQ:       return "fcmp.eq";
    case OP_FCmpNE:       return "fcmp.ne";
    case OP_FCmpLT:       return "fcmp.lt";
    case OP_FCmpGT:       return "fcmp.gt";
    case OP_FCmpLE:       return "fcmp.le";
    case OP_FCmpGE:       return "fcmp.ge";
    case OP_IntToFloat:   return "itof";
    case OP_FloatToInt:   return "ftoi";
    case OP_FloatResize:  return "fresize";
    case OP_IntResize:    return "iresize";
    case OP_Jump:         return "jump";
    case OP_BranchZero:   return "brz";
    case OP_BranchNZ:     return "brnz";
    case OP_Call:         return "call";
    case OP_CallIndirect: return "call.ind";
    case OP_VTableLoad:   return "vtable";
    case OP_Native:       return "native";
    case OP_Return:       return "ret";
    case OP_ReturnVoid:   return "ret.void";
    case OP_Alloc:        return "alloc";
    case OP_Free:         return "free";
    case OP_Halt:         return "halt";
    case OP_AllocN:       return "alloc.n";
    case OP_ArrayCount:   return "arraycount";
    case OP_Count:        break;         // not an instruction
    }
    return "?";
}

namespace {
struct NativeEntry { const char *name; NativeId id; int args; bool retFloat; };
const NativeEntry NativeTable[] = {
    { "print_int",    NAT_PrintInt,    1, false },
    { "print_char",   NAT_PrintChar,   1, false },
    { "print_double", NAT_PrintDouble, 1, false },
    { "print_string", NAT_PrintString, 1, false },
    { "print_line",   NAT_PrintLine,   0, false },
    { "err_int",      NAT_ErrInt,      1, false },
    { "err_char",     NAT_ErrChar,     1, false },
    { "err_double",   NAT_ErrDouble,   1, false },
    { "err_string",   NAT_ErrString,   1, false },
    { "err_line",     NAT_ErrLine,     0, false },
    { "sqrt",         NAT_Sqrt,        1, true  },
    { "sin",          NAT_Sin,         1, true  },
    { "cos",          NAT_Cos,         1, true  },
    { "tan",          NAT_Tan,         1, true  },
    { "asin",         NAT_Asin,        1, true  },
    { "acos",         NAT_Acos,        1, true  },
    { "atan",         NAT_Atan,        1, true  },
    { "atan2",        NAT_Atan2,       2, true  },
    { "sinh",         NAT_Sinh,        1, true  },
    { "cosh",         NAT_Cosh,        1, true  },
    { "tanh",         NAT_Tanh,        1, true  },
    { "pow",          NAT_Pow,         2, true  },
    { "fabs",         NAT_Fabs,        1, true  },
    { "floor",        NAT_Floor,       1, true  },
    { "ceil",         NAT_Ceil,        1, true  },
    { "fmod",         NAT_Fmod,        2, true  },
    { "trunc",        NAT_Trunc,       1, true  },
    { "round",        NAT_Round,       1, true  },
    { "log",          NAT_Log,         1, true  },
    { "log10",        NAT_Log10,       1, true  },
    { "exp",          NAT_Exp,         1, true  },
    { "abs",          NAT_Abs,         1, false },
    { "print_pointer", NAT_PrintPointer, 1, false },
    { "err_pointer",   NAT_ErrPointer,   1, false },
    { "read_int",     NAT_ReadInt,     0, false },
    { "read_double",  NAT_ReadDouble,  0, true  },
    { "read_char",    NAT_ReadChar,    0, false },
    { "read_string",  NAT_ReadString,  2, false },
    { "read_line",    NAT_ReadLine,    2, false },
    { "input_good",   NAT_InputGood,   0, false }
};
const int NativeCount = static_cast<int>(sizeof(NativeTable) / sizeof(NativeTable[0]));
}

NativeId nativeByName(const std::string &name) {
    for (int i = 0; i < NativeCount; ++i) {
        if (name == NativeTable[i].name) return NativeTable[i].id;
    }
    return NAT_Count;
}

const char *nativeName(NativeId id) {
    for (int i = 0; i < NativeCount; ++i) {
        if (NativeTable[i].id == id) return NativeTable[i].name;
    }
    return "?";
}

int nativeArgCount(NativeId id) {
    for (int i = 0; i < NativeCount; ++i) {
        if (NativeTable[i].id == id) return NativeTable[i].args;
    }
    return 0;
}

bool nativeReturnsFloat(NativeId id) {
    for (int i = 0; i < NativeCount; ++i) {
        if (NativeTable[i].id == id) return NativeTable[i].retFloat;
    }
    return false;
}

// --- the object file ---------------------------------------------------
//
// Everything is written little-endian and fixed-width.  A variable-length
// encoding would make the file smaller and the reader harder to follow, and
// the file is not the interesting part of this compiler.

namespace {

void putU(std::string &out, uvmword v, int bytes) {
    for (int i = 0; i < bytes; ++i) out += static_cast<char>((v >> (i * 8)) & 0xFF);
}

void putI64(std::string &out, vmword v) {
    putU(out, static_cast<uvmword>(v), 8);
}

void putF64(std::string &out, double v) {
    // The bit pattern, so the value survives the trip exactly.
    unsigned char buf[8];
    std::memcpy(buf, &v, 8);
    for (int i = 0; i < 8; ++i) out += static_cast<char>(buf[i]);
}

void putStr(std::string &out, const std::string &s) {
    putU(out, static_cast<unsigned long>(s.size()), 4);
    out += s;
}

struct Reader {
    const std::string &data;
    std::size_t at;
    bool ok;
    Reader(const std::string &d) : data(d), at(0), ok(true) {}

    bool need(std::size_t n) {
        if (!ok || at + n > data.size()) { ok = false; return false; }
        return true;
    }
    uvmword getU(int bytes) {
        if (!need(static_cast<std::size_t>(bytes))) return 0;
        uvmword v = 0;
        for (int i = bytes - 1; i >= 0; --i) {
            v = (v << 8) | static_cast<unsigned char>(data[at + i]);
        }
        at += bytes;
        return v;
    }
    vmword getI64() { return static_cast<vmword>(getU(8)); }
    double getF64() {
        if (!need(8)) return 0.0;
        double v = 0.0;
        std::memcpy(&v, &data[at], 8);
        at += 8;
        return v;
    }
    std::string getStr() {
        const uvmword n = getU(4);
        if (!need(n)) return std::string();
        const std::string s = data.substr(at, n);
        at += n;
        return s;
    }
};

} // namespace

bool Image::write(const std::string &path, std::string &error) const {
    std::string out;
    putU(out, Magic, 4);
    putU(out, Version, 4);
    putI64(out, entry);
    putI64(out, fini);

    putU(out, static_cast<uvmword>(staticData.size()), 4);
    for (std::size_t i = 0; i < staticData.size(); ++i) {
        out += static_cast<char>(staticData[i]);
    }

    putU(out, static_cast<uvmword>(functions.size()), 4);
    for (std::size_t f = 0; f < functions.size(); ++f) {
        const FuncImage &fi = functions[f];
        putStr(out, fi.name);
        putI64(out, fi.paramCount);
        putI64(out, fi.frameSize);
        putI64(out, fi.registerCount);

        putU(out, static_cast<uvmword>(fi.localOffset.size()), 4);
        for (std::size_t i = 0; i < fi.localOffset.size(); ++i) putI64(out, fi.localOffset[i]);
        putU(out, static_cast<uvmword>(fi.localSize.size()), 4);
        for (std::size_t i = 0; i < fi.localSize.size(); ++i) putI64(out, fi.localSize[i]);
        putU(out, static_cast<uvmword>(fi.localFloat.size()), 4);
        for (std::size_t i = 0; i < fi.localFloat.size(); ++i) putU(out, fi.localFloat[i], 1);
        putU(out, static_cast<uvmword>(fi.localObject.size()), 4);
        for (std::size_t i = 0; i < fi.localObject.size(); ++i) putU(out, fi.localObject[i], 1);

        putU(out, static_cast<uvmword>(fi.code.size()), 4);
        for (std::size_t i = 0; i < fi.code.size(); ++i) {
            const Instr &n = fi.code[i];
            putU(out, static_cast<uvmword>(n.op), 1);
            putI64(out, n.imm);
            putI64(out, n.b);
            putF64(out, n.fimm);
            putI64(out, n.line);
        }
    }

    std::ofstream file(path.c_str(), std::ios::binary);
    if (!file) { error = "cannot open '" + path + "' for writing"; return false; }
    file.write(out.data(), static_cast<std::streamsize>(out.size()));
    if (!file) { error = "failed while writing '" + path + "'"; return false; }
    return true;
}

bool Image::read(const std::string &path, std::string &error) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) { error = "cannot open '" + path + "'"; return false; }
    std::ostringstream ss;
    ss << file.rdbuf();
    const std::string data = ss.str();

    Reader r(data);
    if (r.getU(4) != Magic) { error = "'" + path + "' is not a Compiler++ image"; return false; }
    const uvmword ver = r.getU(4);
    if (ver != Version) {
        std::ostringstream m;
        m << "'" << path << "' is version " << ver << ", this build reads version " << Version;
        error = m.str();
        return false;
    }
    entry = static_cast<int>(r.getI64());
    fini  = static_cast<int>(r.getI64());

    const uvmword dataLen = r.getU(4);
    staticData.clear();
    if (!r.need(dataLen)) { error = "'" + path + "' is truncated"; return false; }
    staticData.reserve(dataLen);
    for (uvmword i = 0; i < dataLen; ++i) {
        staticData.push_back(static_cast<unsigned char>(data[r.at + i]));
    }
    r.at += dataLen;

    const uvmword funcCount = r.getU(4);
    functions.clear();
    for (uvmword f = 0; f < funcCount && r.ok; ++f) {
        FuncImage fi;
        fi.name = r.getStr();
        fi.paramCount = static_cast<int>(r.getI64());
        fi.frameSize = static_cast<int>(r.getI64());
        fi.registerCount = static_cast<int>(r.getI64());

        uvmword n = r.getU(4);
        for (uvmword i = 0; i < n && r.ok; ++i) fi.localOffset.push_back(static_cast<int>(r.getI64()));
        n = r.getU(4);
        for (uvmword i = 0; i < n && r.ok; ++i) fi.localSize.push_back(static_cast<int>(r.getI64()));
        n = r.getU(4);
        for (uvmword i = 0; i < n && r.ok; ++i)
            fi.localFloat.push_back(static_cast<unsigned char>(r.getU(1)));
        n = r.getU(4);
        for (uvmword i = 0; i < n && r.ok; ++i)
            fi.localObject.push_back(static_cast<unsigned char>(r.getU(1)));

        n = r.getU(4);
        for (uvmword i = 0; i < n && r.ok; ++i) {
            Instr in;
            in.op = static_cast<OpCode>(r.getU(1));
            in.imm = r.getI64();
            in.b = r.getI64();
            in.fimm = r.getF64();
            in.line = static_cast<int>(r.getI64());
            fi.code.push_back(in);
        }
        functions.push_back(fi);
    }

    if (!r.ok) { error = "'" + path + "' is truncated or malformed"; return false; }
    return true;
}

void Image::disassemble() const {
    std::cout << "static data: " << staticData.size() << " bytes" << std::endl << std::endl;
    for (std::size_t f = 0; f < functions.size(); ++f) {
        const FuncImage &fi = functions[f];
        std::cout << "function " << f << "  " << fi.name
                  << "  params=" << fi.paramCount
                  << " frame=" << fi.frameSize
                  << " regs=" << fi.registerCount << std::endl;
        for (std::size_t i = 0; i < fi.code.size(); ++i) {
            const Instr &n = fi.code[i];
            std::cout << "  " << i << "\t" << opCodeName(n.op);
            switch (n.op) {
            case OP_PushFConst:
                std::cout << " " << n.fimm;
                break;
            case OP_Load:
            case OP_Store:
                std::cout << " :" << n.imm;
                if (n.b & 2)      std::cout << " float";
                else if (n.b & 1) std::cout << " signed";
                break;
            case OP_IntResize:
                std::cout << " :" << n.imm << (n.b ? " signed" : " unsigned");
                break;
            case OP_Call:
                std::cout << " " << n.imm << " (" << n.b << " args)";
                break;
            case OP_CallIndirect:
                std::cout << " (" << n.b << " args)";
                break;
            case OP_Native:
                std::cout << " " << nativeName(static_cast<NativeId>(n.imm));
                break;
            case OP_ReturnVoid:
            case OP_Pop:
            case OP_Halt:
                break;
            default:
                std::cout << " " << n.imm;
                break;
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }
}

// ---------- CodeGen.cpp ----------
// CodeGen.cpp
//
// C++98 only.


#include <cstddef>

namespace {

// Every value on the operand stack is 8 bytes, so a register slot is too.
const int RegSize = 8;

void put64(std::vector<unsigned char> &mem, std::size_t at, vmword value) {
    for (int i = 0; i < 8; ++i) {
        mem[at + i] = static_cast<unsigned char>((value >> (i * 8)) & 0xFF);
    }
}

Instr make(OpCode op, vmword imm = 0, vmword b = 0, int line = 0) {
    Instr n(op);
    n.imm = imm;
    n.b = b;
    n.line = line;
    return n;
}

} // namespace

CodeGen::CodeGen(Diagnostics &d) : diag(d) {}

// --- symbols ---

void CodeGen::collectSymbols(const IRModule &module, Image &out) {
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        const std::string &name = module.functions[i]->name;
        functionIndex[name] = static_cast<int>(i);
        if (name == "main") out.entry = static_cast<int>(i);
        // Named rather than included: pulling in Lower.h here would tie the
        // back end to the pass in front of it for one string.
        if (name == "__global_fini") out.fini = static_cast<int>(i);
    }
}

// Globals, strings and vtables all become bytes in one image, so that a
// pointer to any of them is an ordinary address.
void CodeGen::layoutStaticData(const IRModule &module, Image &out) {
    // Address 0 is reserved, so a null pointer is distinguishable.
    out.staticData.assign(8, 0);

    for (std::size_t g = 0; g < module.globals.size(); ++g) {
        const IRGlobal &gl = module.globals[g];
        staticAddress[gl.name] = static_cast<long>(out.staticData.size());
        out.staticData.resize(out.staticData.size() + (gl.size > 0 ? gl.size : 1), 0);
    }

    for (std::size_t s = 0; s < module.strings.size(); ++s) {
        const IRString &st = module.strings[s];
        staticAddress[st.name] = static_cast<long>(out.staticData.size());
        for (std::size_t c = 0; c < st.value.size(); ++c) {
            out.staticData.push_back(static_cast<unsigned char>(st.value[c]));
        }
        out.staticData.push_back(0);            // NUL, so print_string can stop
    }

    // A vtable is an array of function indices.  Reading slot n is then a load
    // at vtableAddress + n * 8, which is what OP_VTableLoad does.
    for (std::size_t v = 0; v < module.vtables.size(); ++v) {
        const IRVTable &vt = module.vtables[v];
        // Align, so the 8-byte reads below are on natural boundaries.
        while (out.staticData.size() % 8) out.staticData.push_back(0);
        const std::size_t at = out.staticData.size();
        staticAddress[mangleVTable(vt.className)] = static_cast<long>(at);
        out.staticData.resize(at + vt.slots.size() * 8, 0);
        for (std::size_t s = 0; s < vt.slots.size(); ++s) {
            std::map<std::string, int>::const_iterator it = functionIndex.find(vt.slots[s]);
            const long target = (it == functionIndex.end()) ? -1 : it->second;
            put64(out.staticData, at + s * 8, target);
        }
    }
}

void CodeGen::generate(IRModule &module, Image &out) {
    collectSymbols(module, out);
    layoutStaticData(module, out);
    out.functions.resize(module.functions.size());
    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        IRFunction &fn = *module.functions[i];
        generateFunction(fn, out.functions[i]);
        // Released here rather than when the module dies.  Held to the end,
        // the IR and the finished image are both whole at the same moment and
        // that moment is the compile's high-water mark -- while this half of
        // it has been dead since the line above.  The C++98 way to make a
        // vector give its memory back is to swap it with an empty one;
        // `clear()` keeps the capacity, which is the whole of what is wanted
        // back.
        std::vector<IRInstr>().swap(fn.code);
    }
    if (out.entry < 0) diag.error(0, 0, "no 'main' function to run");
}

// --- one function ---

void CodeGen::generateFunction(const IRFunction &fn, FuncImage &out) {
    out.name = fn.name;
    out.paramCount = fn.paramCount;
    out.registerCount = fn.registerCount();

    // Locals first, at their declared widths; the registers follow, uniformly
    // 8 bytes each.  Both live in one frame so an address into either is an
    // ordinary address.
    int offset = 0;
    for (std::size_t i = 0; i < fn.locals.size(); ++i) {
        const int align = fn.locals[i].size >= 8 ? 8 : (fn.locals[i].size >= 4 ? 4 : 1);
        if (align > 1 && offset % align) offset += align - (offset % align);
        out.localOffset.push_back(offset);
        out.localSize.push_back(fn.locals[i].size);
        out.localFloat.push_back(fn.locals[i].isFloat ? 1 : 0);
        out.localObject.push_back(fn.locals[i].isObject ? 1 : 0);
        offset += fn.locals[i].size > 0 ? fn.locals[i].size : 1;
    }
    if (offset % 8) offset += 8 - (offset % 8);
    const int regBase = offset;
    out.frameSize = regBase + out.registerCount * RegSize;

    // Registers are addressed by index; the VM adds regBase itself, so it is
    // recorded here rather than folded into every instruction.
    out.localOffset.push_back(regBase);         // sentinel: where registers start

    std::map<vmword, int> labelAt;                // IR label id -> instruction index

    for (std::size_t i = 0; i < fn.code.size(); ++i) {
        const IRInstr &in = fn.code[i];
        const int line = in.line;

        switch (in.op) {
        case IR_Label:
            labelAt[in.imm] = static_cast<int>(out.code.size());
            continue;

        case IR_Const:
            out.code.push_back(make(OP_PushConst, in.imm, 0, line));
            break;
        case IR_FConst: {
            Instr n(OP_PushFConst);
            n.fimm = in.fimm;
            n.line = line;
            out.code.push_back(n);
            break;
        }
        case IR_StringAddr:
        case IR_GlobalAddr: {
            std::map<std::string, long>::const_iterator it = staticAddress.find(in.sym);
            const long addr = (it == staticAddress.end()) ? 0 : it->second;
            if (it == staticAddress.end()) {
                diag.error(line, 0, "internal: unknown static symbol '" + in.sym + "'");
            }
            out.code.push_back(make(OP_StaticAddr, addr, 0, line));
            break;
        }
        case IR_FuncAddr: {
            std::map<std::string, int>::const_iterator it = functionIndex.find(in.sym);
            out.code.push_back(make(OP_FuncAddr,
                                    it == functionIndex.end() ? -1 : it->second, 0, line));
            break;
        }
        case IR_LocalAddr:
            out.code.push_back(make(OP_LocalAddr, in.imm, 0, line));
            break;
        case IR_FieldAddr:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_FieldAddr, in.imm, 0, line));
            break;

        // The flag word says how the bits travel: bit 0 sign-extends an
        // integer, bit 1 marks a floating value, whose 4-byte form is a real
        // conversion and not the low half of a double.
        case IR_Load:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_Load, in.imm, in.isFloat ? 2 : 1, line));
            break;
        case IR_Store:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_LoadReg, in.b, 0, line));
            out.code.push_back(make(OP_Store, in.imm, in.isFloat ? 2 : 0, line));
            continue;                            // no destination register

        case IR_MemCopy:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));   // dst
            out.code.push_back(make(OP_LoadReg, in.b, 0, line));   // src
            out.code.push_back(make(OP_MemCopy, in.imm, 0, line));
            continue;

        case IR_Call:
        case IR_CallIndirect: {
            if (in.op == IR_CallIndirect) out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            for (std::size_t k = 0; k < in.args.size(); ++k) {
                out.code.push_back(make(OP_LoadReg, in.args[k], 0, line));
            }
            if (in.op == IR_CallIndirect) {
                out.code.push_back(make(OP_CallIndirect, 0,
                                        static_cast<long>(in.args.size()), line));
            } else {
                const NativeId nat = nativeByName(in.sym);
                std::map<std::string, int>::const_iterator it = functionIndex.find(in.sym);
                if (it != functionIndex.end()) {
                    out.code.push_back(make(OP_Call, it->second,
                                            static_cast<long>(in.args.size()), line));
                } else if (nat != NAT_Count) {
                    // Declared without a body and named like a native: the
                    // declaration IS the binding.
                    out.code.push_back(make(OP_Native, nat,
                                            static_cast<long>(in.args.size()), line));
                } else {
                    diag.error(line, 0, "'" + in.sym + "' is declared but never defined");
                    out.code.push_back(make(OP_PushConst, 0, 0, line));
                }
            }
            if (in.dest == IR_NoReg) out.code.push_back(make(OP_Pop, 0, 0, line));
            break;
        }

        case IR_VCallTarget:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_VTableLoad, in.imm, 0, line));
            break;

        case IR_Alloc:
            if (in.a != IR_NoReg) {
                // The count goes on first, so the size is on top: the machine
                // pops the size it always pops, and the count only when told.
                if (in.b != IR_NoReg) out.code.push_back(make(OP_LoadReg, in.b, 0, line));
                out.code.push_back(make(OP_LoadReg, in.a, 0, line));
                out.code.push_back(make(OP_AllocN, 0, in.b != IR_NoReg ? 1 : 0, line));
            } else {
                out.code.push_back(make(OP_Alloc, in.imm, 0, line));
            }
            break;
        case IR_ArrayCount:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_ArrayCount, 0, 0, line));
            break;
        case IR_Free:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_Free, 0, in.isArray ? 1 : 0, line));
            continue;

        case IR_Jump:
            out.code.push_back(make(OP_Jump, in.imm, 0, line));
            continue;
        case IR_BranchZero:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_BranchZero, in.imm, 0, line));
            continue;
        case IR_BranchNZ:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_BranchNZ, in.imm, 0, line));
            continue;
        case IR_Return:
            if (in.a != IR_NoReg) {
                out.code.push_back(make(OP_LoadReg, in.a, 0, line));
                out.code.push_back(make(OP_Return, 0, 0, line));
            } else {
                out.code.push_back(make(OP_ReturnVoid, 0, 0, line));
            }
            continue;

        case IR_IntToFloat:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_IntToFloat, in.imm, 0, line));
            break;
        case IR_FloatToInt:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_FloatToInt, in.imm, 0, line));
            break;
        case IR_FloatResize:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_FloatResize, in.imm, 0, line));
            break;
        case IR_IntResize:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            out.code.push_back(make(OP_IntResize, in.imm, in.b == 1 ? 1 : 0, line));
            break;

        case IR_Move:
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            break;

        default: {
            // The remaining opcodes are all unary or binary arithmetic, and
            // map one to one.
            OpCode op = OP_Add;
            bool binary = true;
            switch (in.op) {
            case IR_Add: op = OP_Add; break;
            case IR_Sub: op = OP_Sub; break;
            case IR_Mul: op = OP_Mul; break;
            case IR_Div: op = OP_Div; break;
            case IR_Mod: op = OP_Mod; break;
            case IR_UDiv: op = OP_UDiv; break;
            case IR_UMod: op = OP_UMod; break;
            case IR_Shl:  op = OP_Shl;  break;
            case IR_Shr:  op = OP_Shr;  break;
            case IR_UShr: op = OP_UShr; break;
            case IR_FAdd: op = OP_FAdd; break;
            case IR_FSub: op = OP_FSub; break;
            case IR_FMul: op = OP_FMul; break;
            case IR_FDiv: op = OP_FDiv; break;
            case IR_CmpEQ: op = OP_CmpEQ; break;
            case IR_CmpNE: op = OP_CmpNE; break;
            case IR_CmpLT: op = OP_CmpLT; break;
            case IR_CmpGT: op = OP_CmpGT; break;
            case IR_CmpLE: op = OP_CmpLE; break;
            case IR_CmpGE: op = OP_CmpGE; break;
            case IR_UCmpLT: op = OP_UCmpLT; break;
            case IR_UCmpGT: op = OP_UCmpGT; break;
            case IR_UCmpLE: op = OP_UCmpLE; break;
            case IR_UCmpGE: op = OP_UCmpGE; break;
            case IR_FCmpEQ: op = OP_FCmpEQ; break;
            case IR_FCmpNE: op = OP_FCmpNE; break;
            case IR_FCmpLT: op = OP_FCmpLT; break;
            case IR_FCmpGT: op = OP_FCmpGT; break;
            case IR_FCmpLE: op = OP_FCmpLE; break;
            case IR_FCmpGE: op = OP_FCmpGE; break;
            case IR_Neg: op = OP_Neg; binary = false; break;
            case IR_FNeg: op = OP_FNeg; binary = false; break;
            case IR_LogicalNot: op = OP_Not; binary = false; break;
            default:
                diag.error(line, 0, "internal: no bytecode for this IR opcode");
                break;
            }
            out.code.push_back(make(OP_LoadReg, in.a, 0, line));
            if (binary) out.code.push_back(make(OP_LoadReg, in.b, 0, line));
            out.code.push_back(make(op, 0, 0, line));
            break;
        }
        }

        if (in.dest != IR_NoReg) {
            out.code.push_back(make(OP_StoreReg, in.dest, 0, line));
        }
    }

    out.code.push_back(make(OP_ReturnVoid));
    resolveLabels(fn, out, labelAt);
}

// IR labels are ids that mean nothing to the VM; branches need offsets.
void CodeGen::resolveLabels(const IRFunction &, FuncImage &out,
                            const std::map<vmword, int> &labelAt) {
    for (std::size_t i = 0; i < out.code.size(); ++i) {
        Instr &n = out.code[i];
        if (n.op != OP_Jump && n.op != OP_BranchZero && n.op != OP_BranchNZ) continue;
        std::map<vmword, int>::const_iterator it = labelAt.find(n.imm);
        if (it == labelAt.end()) {
            diag.error(n.line, 0, "internal: branch to an unplaced label");
            n.imm = static_cast<long>(out.code.size() - 1);
        } else {
            n.imm = it->second;
        }
    }
}

// ---------- VM.cpp ----------
// VM.cpp
//
// C++98 only.


#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>

namespace {
const int  HeaderSize   = 16;                  // [size][next] before each block

// -MIN and MIN / -1 have no answer in the range, and signed overflow is
// undefined in C++ -- on x86-64 the divide instruction faults and takes the
// whole process with it.  The VM defines them the way the hardware would if
// it were allowed to: wrap, like every other signed operation here.
vmword negate(vmword v) {
    return static_cast<vmword>(~static_cast<uvmword>(v) + 1);
}
}

VM::VM()
    : stepsExhausted(false), steps(0), stackBase(0), stackTop(0), heapBase(0), heapTop(0),
      freeList(0),
      img(0), inputGood(true) {}

void VM::trap(const std::string &msg) {
    if (error.empty()) error = msg;
}

void VM::push(vmword v)    { Value x; x.i = v; stack.push_back(x); }
void VM::pushD(double v) { Value x; x.d = v; stack.push_back(x); }

VM::Value VM::pop() {
    if (stack.empty()) {
        trap("operand stack underflow");
        Value z; z.i = 0; return z;
    }
    Value v = stack.back();
    stack.pop_back();
    return v;
}

// --- memory ---

vmword VM::readInt(vmword addr, int size, bool isSigned) {
    // A width the word cannot hold would shift by 64 or more, which is
    // undefined -- and `size` comes out of the file.
    if (size > 8) { trap("read of a width this machine has no word for"); return 0; }
    if (addr <= 0 || size < 0 || size > static_cast<vmword>(mem.size()) ||
        addr > static_cast<vmword>(mem.size()) - size) {
        std::ostringstream ss;
        ss << "read of " << size << " bytes at invalid address " << addr;
        trap(ss.str());
        return 0;
    }
    uvmword raw = 0;
    for (int i = size - 1; i >= 0; --i) {
        raw = (raw << 8) | mem[addr + i];
    }
    if (isSigned && size < 8) {
        const uvmword signBit = static_cast<uvmword>(1) << (size * 8 - 1);
        if (raw & signBit) raw |= ~((static_cast<uvmword>(1) << (size * 8)) - 1);
    }
    return static_cast<vmword>(raw);
}

void VM::writeInt(vmword addr, int size, vmword value) {
    if (size > 8) { trap("write of a width this machine has no word for"); return; }
    if (addr <= 0 || size < 0 || size > static_cast<vmword>(mem.size()) ||
        addr > static_cast<vmword>(mem.size()) - size) {
        std::ostringstream ss;
        ss << "write of " << size << " bytes at invalid address " << addr;
        trap(ss.str());
        return;
    }
    uvmword raw = static_cast<uvmword>(value);
    for (int i = 0; i < size; ++i) {
        mem[addr + i] = static_cast<unsigned char>((raw >> (i * 8)) & 0xFF);
    }
}

double VM::readFloat(vmword addr, int size) {
    // Only two widths exist.  Any other passed the bounds check on `size` and
    // then moved eight bytes regardless, reading past the end of memory for
    // every size below eight.
    if (size != 4 && size != 8) { trap("floating read of an unsupported width"); return 0.0; }
    if (addr <= 0 || size < 0 || size > static_cast<vmword>(mem.size()) ||
        addr > static_cast<vmword>(mem.size()) - size) {
        trap("floating read at an invalid address");
        return 0.0;
    }
    if (size == 4) {
        float f;
        std::memcpy(&f, &mem[addr], 4);
        return f;
    }
    double d;
    std::memcpy(&d, &mem[addr], 8);
    return d;
}

void VM::writeFloat(vmword addr, int size, double value) {
    if (size != 4 && size != 8) { trap("floating write of an unsupported width"); return; }
    if (addr <= 0 || size < 0 || size > static_cast<vmword>(mem.size()) ||
        addr > static_cast<vmword>(mem.size()) - size) {
        trap("floating write at an invalid address");
        return;
    }
    if (size == 4) {
        float f = static_cast<float>(value);
        std::memcpy(&mem[addr], &f, 4);
        return;
    }
    std::memcpy(&mem[addr], &value, 8);
}

// Every block on the free list needs a header of its own, so the list cannot
// be longer than the heap holds headers.  A walk that goes further is going
// round a cycle rather than along a list, and must stop rather than spin.
vmword VM::freeListLimit() const {
    return (heapTop - heapBase) / HeaderSize + 1;
}

// A block carries its size, so `delete` knows how much it is releasing, and a
// next pointer, so a released block can be reused.  First fit, because the
// simplest allocator that reuses memory is enough to run a loop.
// The block's `next` field is only a free-list link while the block is FREE.
// While it is allocated it is spare, so what made the block lives there: 0 for
// plain new, and the element count plus one for new[].  One word, no cookie in
// front of the payload, and every address the program sees is still the address
// the allocator returned.
//
// The block's SIZE cannot stand in for the count: a block is rounded up to a
// multiple of eight, so five four-byte elements come back as six.
vmword VM::allocate(vmword bytes, vmword arrayCount) {
    const vmword mark = arrayCount < 0 ? 0 : arrayCount + 1;
    if (bytes <= 0) bytes = 1;
    // Round up AFTER the size is known to fit, or the rounding itself wraps.
    if (bytes > static_cast<vmword>(mem.size())) { trap("out of heap memory"); return 0; }
    if (bytes % 8) bytes += 8 - (bytes % 8);

    vmword prev = 0;
    vmword walked = 0;
    const vmword limit = freeListLimit();
    for (vmword b = freeList; b != 0; ) {
        if (++walked > limit) { trap("heap free list is corrupt"); return 0; }
        const vmword size = readInt(b, 8, true);
        const vmword next = readInt(b + 8, 8, true);
        if (size >= bytes) {
            if (prev) writeInt(prev + 8, 8, next);
            else      freeList = next;
            writeInt(b + 8, 8, mark);
            return b + HeaderSize;
        }
        prev = b;
        b = next;
    }

    // As a difference: the sum overflows for a size near the top of the range
    // and wraps back under the limit.
    if (heapTop > static_cast<vmword>(mem.size()) - HeaderSize - bytes) {
        trap("out of heap memory");
        return 0;
    }
    const vmword block = heapTop;
    heapTop += HeaderSize + bytes;
    writeInt(block, 8, bytes);
    writeInt(block + 8, 8, mark);
    return block + HeaderSize;
}

// Walking from the bottom of the heap: every block says how long it is, so the
// starts are exactly the addresses this walk lands on.
bool VM::isBlockStart(vmword block) {
    vmword walk = heapBase;
    vmword steppedOver = 0;
    const vmword guard = freeListLimit();
    while (walk < heapTop) {
        if (++steppedOver > guard) { trap("heap is corrupt"); return false; }
        if (walk == block) return true;
        const vmword size = readInt(walk, 8, true);
        if (size <= 0) { trap("heap is corrupt"); return false; }
        walk += HeaderSize + size;
    }
    return false;
}

bool VM::isOnFreeList(vmword block) {
    vmword walked = 0;
    const vmword limit = freeListLimit();
    for (vmword b = freeList; b != 0; b = readInt(b + 8, 8, true)) {
        if (++walked > limit) { trap("heap free list is corrupt"); return false; }
        if (b == block) return true;
    }
    return false;
}

// The same question arrayCount asks, without the trap: a pointer that is not a
// heap block is a legitimate answer here, not an error.
// The array a `char buf[32]` names has decayed to a pointer by the time it
// reaches a native, and the type went with it.  The SLOT is still described,
// though, by the frame table of whichever function declared it -- so the
// machine looks the address up in the frames it has pushed and answers how
// much room is left from there to the end of that slot.
bool VM::frameCapacity(vmword addr, vmword &cap) {
    if (!img || addr <= 0) return false;
    for (std::size_t f = frames.size(); f-- > 0; ) {
        const Frame &fr = frames[f];
        if (fr.func < 0 || fr.func >= static_cast<int>(img->functions.size())) continue;
        const FuncImage &fi = img->functions[fr.func];
        for (std::size_t k = 0; k < fi.localSize.size(); ++k) {
            const vmword start = fr.base + fi.localOffset[k];
            const vmword end   = start + fi.localSize[k];
            if (addr >= start && addr < end) { cap = end - addr; return cap > 0; }
        }
    }
    return false;
}

bool VM::heapCapacity(vmword addr, vmword &cap) {
    if (addr <= 0) return false;
    const vmword block = addr - HeaderSize;
    if (block < heapBase || block >= heapTop) return false;
    if (!isBlockStart(block) || isOnFreeList(block)) return false;
    cap = readInt(block, 8, true);
    return cap > 0;
}

void VM::writeCString(vmword addr, const std::string &s, vmword cap) {
    if (cap <= 0) return;
    const vmword limit = static_cast<vmword>(mem.size());
    if (addr <= 0 || cap > limit || addr > limit - cap) {
        trap("input written to an invalid address");
        return;
    }
    vmword n = static_cast<vmword>(s.size());
    if (n > cap - 1) n = cap - 1;               // room for the terminator
    for (vmword i = 0; i < n; ++i) {
        mem[addr + i] = static_cast<unsigned char>(s[static_cast<std::size_t>(i)]);
    }
    mem[addr + n] = 0;
}

vmword VM::arrayCount(vmword addr) {
    if (addr == 0) return 0;                    // delete[] of null: nothing to do
    const vmword block = addr - HeaderSize;
    if (block < heapBase || block >= heapTop || !isBlockStart(block)) {
        trap("delete[] of a pointer that did not come from new[]");
        return 0;
    }
    // Asked BEFORE the mark is read: releasing a block overwrites the mark with
    // a free-list link, so a second delete[] would otherwise be reported as a
    // form mismatch rather than as the double delete it is.
    if (isOnFreeList(block)) {
        trap("delete[] of a pointer that was already deleted");
        return 0;
    }
    const vmword mark = readInt(block + 8, 8, true);
    if (mark <= 0) {
        trap("delete[] applied to a pointer from plain 'new'");
        return 0;
    }
    return mark - 1;
}

void VM::release(vmword addr, bool isArray) {
    if (addr == 0) return;                      // deleting null is harmless
    const vmword block = addr - HeaderSize;
    if (block < heapBase || block >= heapTop) {
        trap("delete of a pointer that did not come from new");
        return;
    }
    // Being INSIDE the heap is not the same as being a block.  `delete` of a
    // pointer to a field of an object landed here, and the bytes of that field
    // then became a header: a size and a next of the program's own choosing,
    // which the next allocation followed.  So the blocks are walked from the
    // bottom -- each one says how long it is -- and only a block START counts.
    // Linear in the number of blocks, which is what a first-fit allocator with
    // a single free list already costs.
    if (!isBlockStart(block)) {
        if (!failed()) {
            trap(isArray ? "delete[] of a pointer that did not come from new[]"
                         : "delete of a pointer that did not come from new");
        }
        return;
    }
    // Deleting a block that is already free would link it to ITSELF, and the
    // next allocation too big to satisfy from it would then follow `next`
    // round that loop forever -- inside allocate(), where the interpreter's
    // step limit does not reach.  A double delete is a fault in the program,
    // so it is reported as one instead of hanging the machine.
    //
    // Asked before the form is read, because releasing a block overwrites the
    // form with a free-list link: a second delete[] would otherwise be
    // reported as a mismatch rather than as the double delete it is.
    if (isOnFreeList(block)) {
        if (!failed()) {
            trap(isArray ? "delete[] of a pointer that was already deleted"
                         : "delete of a pointer that was already deleted");
        }
        return;
    }
    // The two forms are not interchangeable: delete[] runs a destructor for
    // every element and delete runs one.  The language leaves the mismatch
    // undefined because a real allocator has nowhere to record which was used.
    // This one does, so it is an error rather than a mystery.
    const vmword wasArray = readInt(block + 8, 8, true);
    if ((wasArray != 0) != isArray) {
        trap(isArray ? "delete[] applied to a pointer from plain 'new'"
                     : "delete applied to a pointer from 'new[]'; use delete[]");
        return;
    }
    writeInt(block + 8, 8, freeList);
    freeList = block;
}

// --- natives ---

void VM::callNative(NativeId id, int argc) {
    // Arguments come off the stack in reverse, so the last one lands last.
    // Every one is popped, whether the native reads it or not: leaving any
    // behind would desync the stack for everything after.
    Value a[NativeMaxArgs];
    for (int i = 0; i < NativeMaxArgs; ++i) a[i].i = 0;
    for (int k = argc - 1; k >= 0; --k) {
        const Value v = pop();
        if (k < NativeMaxArgs) a[k] = v;
    }

    switch (id) {
    case NAT_PrintInt:    std::cout << a[0].i; break;
    case NAT_PrintChar:   std::cout << static_cast<char>(a[0].i); break;
    case NAT_PrintDouble: std::cout << a[0].d; break;
    case NAT_PrintLine:   std::cout << std::endl; break;
    case NAT_PrintString: {
        vmword p = a[0].i;
        while (p > 0 && p < static_cast<vmword>(mem.size()) && mem[p]) {
            std::cout << static_cast<char>(mem[p]);
            ++p;
        }
        break;
    }

    // The same five, on the error stream.
    case NAT_ErrInt:    std::cerr << a[0].i; break;
    case NAT_ErrChar:   std::cerr << static_cast<char>(a[0].i); break;
    case NAT_ErrDouble: std::cerr << a[0].d; break;
    case NAT_ErrLine:   std::cerr << std::endl; break;
    case NAT_ErrString: {
        vmword p = a[0].i;
        while (p > 0 && p < static_cast<vmword>(mem.size()) && mem[p]) {
            std::cerr << static_cast<char>(mem[p]);
            ++p;
        }
        break;
    }

    // Maths.  The operand stack carries a double, which is exactly what the
    // C library wants, so these are one call each.
    case NAT_Sqrt:  pushD(std::sqrt(a[0].d));  return;
    case NAT_Sin:   pushD(std::sin(a[0].d));   return;
    case NAT_Cos:   pushD(std::cos(a[0].d));   return;
    case NAT_Tan:   pushD(std::tan(a[0].d));   return;
    case NAT_Asin:  pushD(std::asin(a[0].d));  return;
    case NAT_Acos:  pushD(std::acos(a[0].d));  return;
    case NAT_Atan:  pushD(std::atan(a[0].d));  return;
    case NAT_Atan2: pushD(std::atan2(a[0].d, a[1].d)); return;
    case NAT_Sinh:  pushD(std::sinh(a[0].d));  return;
    case NAT_Cosh:  pushD(std::cosh(a[0].d));  return;
    case NAT_Tanh:  pushD(std::tanh(a[0].d));  return;
    case NAT_Pow:   pushD(std::pow(a[0].d, a[1].d));   return;
    case NAT_Fabs:  pushD(std::fabs(a[0].d));  return;
    case NAT_Floor: pushD(std::floor(a[0].d)); return;
    case NAT_Ceil:  pushD(std::ceil(a[0].d));  return;
    case NAT_Fmod:  pushD(std::fmod(a[0].d, a[1].d));  return;
    // trunc and round are C99; this is C++98, so they are written in terms of
    // the two roundings C++98 does have.  Both go away from zero the way the
    // C99 versions do: trunc(-2.7) is -2, round(-2.5) is -3.
    case NAT_Trunc:
        pushD(a[0].d < 0.0 ? std::ceil(a[0].d) : std::floor(a[0].d));
        return;
    case NAT_Round:
        pushD(a[0].d < 0.0 ? std::ceil(a[0].d - 0.5) : std::floor(a[0].d + 0.5));
        return;
    case NAT_Log:   pushD(std::log(a[0].d));   return;
    case NAT_Log10: pushD(std::log10(a[0].d)); return;
    case NAT_Exp:   pushD(std::exp(a[0].d));   return;
    case NAT_Abs:   push(a[0].i < 0 ? negate(a[0].i) : a[0].i); return;

    // An address, printed the way C++ prints one: as a number in hex, which
    // is what makes two pointers comparable by eye.  It is this machine's
    // address, not the host's, and that is the useful one -- it is where the
    // object actually lives in the memory this program can see.
    case NAT_PrintPointer:
    case NAT_ErrPointer: {
        std::ostream &out = (id == NAT_PrintPointer) ? std::cout : std::cerr;
        if (a[0].i == 0) { out << "0"; break; }
        std::ostringstream ss;
        ss << "0x" << std::hex << a[0].i;
        out << ss.str();
        break;
    }

    // --- input ---
    // A failed read leaves the destination alone and turns inputGood false.
    // There are no exceptions here and no stream-state object to carry one, so
    // that flag is the whole of the mechanism and cin.good() reads it.
    case NAT_ReadInt: {
        long v = 0;
        inputGood = (std::cin >> v) ? true : false;
        push(inputGood ? static_cast<vmword>(v) : 0);
        return;
    }
    case NAT_ReadDouble: {
        double v = 0.0;
        inputGood = (std::cin >> v) ? true : false;
        pushD(inputGood ? v : 0.0);
        return;
    }
    case NAT_ReadChar: {
        char c = 0;
        inputGood = (std::cin >> c) ? true : false;   // leading space skipped, as >> does
        push(inputGood ? static_cast<vmword>(c) : 0);
        return;
    }
    case NAT_ReadString: {
        std::string w;
        inputGood = (std::cin >> w) ? true : false;
        vmword cap = a[1].i;
        if (cap <= 0) {
            // `cin >> s` gives no width.  If the buffer came from new[] the
            // machine knows how long it is; otherwise nothing does, and a read
            // into a buffer of unknown length is the overflow this VM refuses
            // everywhere else.
            if (!heapCapacity(a[0].i, cap) && !frameCapacity(a[0].i, cap)) {
                trap("reading a word into a buffer of unknown size; "
                     "use cin.getline(buffer, size)");
                push(0);
                return;
            }
        }
        if (inputGood) writeCString(a[0].i, w, cap);
        push(0);
        return;
    }
    case NAT_ReadLine: {
        std::string l;
        inputGood = std::getline(std::cin, l) ? true : false;
        vmword cap = a[1].i;
        if (cap <= 0 && !heapCapacity(a[0].i, cap) && !frameCapacity(a[0].i, cap)) {
            trap("reading a line into a buffer of unknown size; "
                 "give cin.getline a size");
            push(0);
            return;
        }
        if (inputGood) writeCString(a[0].i, l, cap);
        push(0);
        return;
    }
    case NAT_InputGood: push(inputGood ? 1 : 0); return;

    // Not a native: the caller range-checks the id, and this keeps the switch
    // exhaustive so the compiler goes on checking it too.
    case NAT_Count:
        trap("call to a native this machine does not have");
        break;
    }
    push(0);                                    // a print yields a value too
}

// --- the loop ---

vmword VM::run(const Image &image, bool &ok) {
    ok = false;
    error.clear();
    steps = 0;
    stepsExhausted = false;
    // And the machine itself, which a trap leaves standing.  The driver builds
    // a fresh VM per run and exits, so only an embedder reuses one -- and it
    // inherited the previous run's frames, whose `func` is an index into the
    // image they came from.  The dispatch loop reads image.functions[fr.func]
    // before it checks anything, so a stale index into a SMALLER image is a
    // read past the end of the table: ASan calls it a heap-buffer-overflow
    // 9104 bytes past a 480-byte allocation, and it is reachable from nothing
    // more exotic than running one program after another one trapped.
    frames.clear();
    stack.clear();

    if (image.entry < 0) { trap("no entry point"); return 0; }
    // A .cxb may have come from anywhere, so nothing in it is trusted: an
    // out-of-range entry, or a function with no register base, would index
    // straight past the tables it arrived with.
    if (image.entry >= static_cast<int>(image.functions.size())) {
        trap("entry point is not a function in this image");
        return 0;
    }
    for (std::size_t i = 0; i < image.functions.size(); ++i) {
        const FuncImage &fi = image.functions[i];
        if (fi.localOffset.empty()) {
            trap("function '" + fi.name + "' has no frame layout");
            return 0;
        }
        // The four per-local tables describe the same slots and are written
        // that way -- one entry each, plus a sentinel on localOffset saying
        // where the registers start.  They are READ back as four independent
        // length-prefixed arrays, so a file that disagrees with itself would
        // send the argument loop past the end of the short one and use
        // whatever it found there as a frame offset.  They have to agree
        // before any of them is indexed.
        if (fi.localOffset.size() != fi.localSize.size() + 1 ||
            fi.localFloat.size()  != fi.localSize.size() ||
            fi.localObject.size() != fi.localSize.size()) {
            trap("function '" + fi.name + "' has an inconsistent frame layout");
            return 0;
        }
        if (fi.frameSize < 0 || fi.paramCount < 0 || fi.registerCount < 0) {
            trap("function '" + fi.name + "' has a negative frame size");
            return 0;
        }
    }

    // The length is the file's to claim, and the memory is a fixed size: a
    // claim larger than the machine wrote past the end of the vector's buffer
    // and corrupted the host's heap, which is not a fault the program can be
    // blamed for.
    // A machine has to be big enough to be one.  The limits are the host's to
    // choose and a host can choose badly, so they are checked here rather than
    // trusted -- the heap starts after the call stack, and a call stack that
    // does not fit puts every allocation past the end of memory.
    if (limits.memory <= 0 || limits.callStack <= 0 || limits.maxSteps <= 0) {
        trap("this machine has no memory, no call stack, or no step budget");
        return 0;
    }
    if (limits.callStack >= limits.memory) {
        trap("this machine's call stack does not fit in its memory");
        return 0;
    }
    if (static_cast<vmword>(image.staticData.size()) + limits.callStack >= limits.memory) {
        trap("static data and the call stack do not fit in this machine's memory");
        return 0;
    }
    if (static_cast<vmword>(image.staticData.size()) > limits.memory) {
        trap("static data does not fit in this machine's memory");
        return 0;
    }
    img = &image;                   // the frame tables, for frameCapacity
    mem.assign(static_cast<std::size_t>(limits.memory), 0);
    std::copy(image.staticData.begin(), image.staticData.end(), mem.begin());

    stackBase = static_cast<vmword>(image.staticData.size());
    if (stackBase % 8) stackBase += 8 - (stackBase % 8);
    stackTop = stackBase;
    heapBase = stackBase + limits.callStack;
    heapTop = heapBase;
    freeList = 0;

    // The entry frame.
    Frame top;
    top.func = image.entry;
    top.pc = 0;
    top.base = stackTop;
    top.regBase = image.functions[image.entry].localOffset.back();
    top.wantsResult = true;
    // The same bound every other call gets.  The entry frame was pushed
    // without one, so a frame size the file made up slid the whole stack past
    // the heap before a single instruction ran.
    if (stackTop > heapBase - image.functions[image.entry].frameSize) {
        trap("the entry function's frame does not fit on the stack");
        return 0;
    }
    stackTop += image.functions[image.entry].frameSize;
    frames.push_back(top);

    vmword result = 0;
    bool finiDone = false;

    while (!frames.empty() && !failed()) {
        if (++steps > limits.maxSteps) {
            stepsExhausted = true;
            trap("execution did not terminate");
            break;
        }

        Frame &fr = frames.back();
        const FuncImage &fi = image.functions[fr.func];
        if (fr.pc < 0 || fr.pc >= static_cast<int>(fi.code.size())) {
            trap("program counter left the function");
            break;
        }
        const Instr &in = fi.code[fr.pc++];

        switch (in.op) {
        case OP_PushConst:  push(in.imm); break;
        case OP_PushFConst: pushD(in.fimm); break;
        case OP_Pop:        pop(); break;

        case OP_LoadReg:
            // The frame declares how many it has.  Without this the index was
            // multiplied by eight and added to the frame base unchecked, which
            // both overflows and reaches anywhere in memory.
            if (in.imm < 0 || in.imm >= fi.registerCount) {
                trap("register out of range");
                break;
            }
            push(readInt(fr.base + fr.regBase + in.imm * 8, 8, true));
            break;
        case OP_StoreReg:
            if (in.imm < 0 || in.imm >= fi.registerCount) {
                trap("register out of range");
                break;
            }
            writeInt(fr.base + fr.regBase + in.imm * 8, 8, pop().i);
            break;

        case OP_LocalAddr:
            if (in.imm < 0 || in.imm >= static_cast<vmword>(fi.localOffset.size()) - 1) {
                trap("local slot out of range");
                break;
            }
            push(fr.base + fi.localOffset[in.imm]);
            break;
        case OP_StaticAddr: push(in.imm); break;
        case OP_FuncAddr:   push(in.imm); break;
        case OP_FieldAddr:  push(pop().i + in.imm); break;

        case OP_Load: {
            const vmword addr = pop().i;
            if (in.b & 2) pushD(readFloat(addr, static_cast<int>(in.imm)));
            else          push(readInt(addr, static_cast<int>(in.imm), (in.b & 1) != 0));
            break;
        }
        case OP_Store: {
            const Value v = pop();
            const vmword addr = pop().i;
            if (in.b & 2) writeFloat(addr, static_cast<int>(in.imm), v.d);
            else          writeInt(addr, static_cast<int>(in.imm), v.i);
            break;
        }

        case OP_MemCopy: {
            const vmword src = pop().i;
            const vmword dst = pop().i;
            const vmword n   = in.imm;
            // As differences: `src + n` overflows for a count near the top of
            // the range and wraps back under the limit, which is how a copy of
            // nine million million bytes passed this check.  The by-value
            // argument copy below was written this way already; this one was
            // not.
            const vmword limit = static_cast<vmword>(mem.size());
            if (src <= 0 || dst <= 0 || n < 0 || n > limit ||
                src > limit - n || dst > limit - n) {
                trap("object copy at an invalid address");
                break;
            }
            if (src != dst) std::memmove(&mem[dst], &mem[src], static_cast<std::size_t>(n));
            break;
        }

        // A shift by a silly amount is undefined in C++, so the VM defines it:
        // out of range yields 0 rather than whatever the host would do.
        case OP_Shl: {
            vmword b = pop().i, a = pop().i;
            push((b < 0 || b > 63) ? 0 : static_cast<vmword>(
                     static_cast<uvmword>(a) << b));
            break;
        }
        case OP_Shr: {
            vmword b = pop().i, a = pop().i;
            if (b < 0 || b > 63) { push(a < 0 ? -1 : 0); break; }
            push(a >> b);                       // arithmetic: the sign is kept
            break;
        }
        case OP_UShr: {
            vmword b = pop().i, a = pop().i;
            push((b < 0 || b > 63) ? 0 : static_cast<vmword>(
                     static_cast<uvmword>(a) >> b));
            break;
        }

        case OP_Add: { vmword b = pop().i, a = pop().i; push(a + b); break; }
        case OP_Sub: { vmword b = pop().i, a = pop().i; push(a - b); break; }
        case OP_Mul: { vmword b = pop().i, a = pop().i; push(a * b); break; }
        case OP_Div: {
            vmword b = pop().i, a = pop().i;
            if (b == 0) { trap("division by zero"); break; }
            if (b == -1) { push(negate(a)); break; }
            push(a / b);
            break;
        }
        case OP_Mod: {
            vmword b = pop().i, a = pop().i;
            if (b == 0) { trap("remainder by zero"); break; }
            if (b == -1) { push(0); break; }
            push(a % b);
            break;
        }
        case OP_UDiv: {
            uvmword b = static_cast<uvmword>(pop().i);
            uvmword a = static_cast<uvmword>(pop().i);
            if (b == 0) { trap("division by zero"); break; }
            push(static_cast<vmword>(a / b));
            break;
        }
        case OP_UMod: {
            uvmword b = static_cast<uvmword>(pop().i);
            uvmword a = static_cast<uvmword>(pop().i);
            if (b == 0) { trap("remainder by zero"); break; }
            push(static_cast<vmword>(a % b));
            break;
        }
        case OP_Neg: push(negate(pop().i)); break;
        case OP_Not: push(pop().i == 0 ? 1 : 0); break;

        case OP_FAdd: { double b = pop().d, a = pop().d; pushD(a + b); break; }
        case OP_FSub: { double b = pop().d, a = pop().d; pushD(a - b); break; }
        case OP_FMul: { double b = pop().d, a = pop().d; pushD(a * b); break; }
        case OP_FDiv: { double b = pop().d, a = pop().d; pushD(a / b); break; }
        case OP_FNeg: pushD(-pop().d); break;

        case OP_CmpEQ: { vmword b = pop().i, a = pop().i; push(a == b); break; }
        case OP_CmpNE: { vmword b = pop().i, a = pop().i; push(a != b); break; }
        case OP_CmpLT: { vmword b = pop().i, a = pop().i; push(a <  b); break; }
        case OP_CmpGT: { vmword b = pop().i, a = pop().i; push(a >  b); break; }
        case OP_CmpLE: { vmword b = pop().i, a = pop().i; push(a <= b); break; }
        case OP_CmpGE: { vmword b = pop().i, a = pop().i; push(a >= b); break; }
        case OP_UCmpLT: { uvmword b = pop().i, a = pop().i; push(a <  b); break; }
        case OP_UCmpGT: { uvmword b = pop().i, a = pop().i; push(a >  b); break; }
        case OP_UCmpLE: { uvmword b = pop().i, a = pop().i; push(a <= b); break; }
        case OP_UCmpGE: { uvmword b = pop().i, a = pop().i; push(a >= b); break; }
        case OP_FCmpEQ: { double b = pop().d, a = pop().d; push(a == b); break; }
        case OP_FCmpNE: { double b = pop().d, a = pop().d; push(a != b); break; }
        case OP_FCmpLT: { double b = pop().d, a = pop().d; push(a <  b); break; }
        case OP_FCmpGT: { double b = pop().d, a = pop().d; push(a >  b); break; }
        case OP_FCmpLE: { double b = pop().d, a = pop().d; push(a <= b); break; }
        case OP_FCmpGE: { double b = pop().d, a = pop().d; push(a >= b); break; }

        case OP_IntToFloat: {
            const vmword v = pop().i;
            pushD(in.imm ? static_cast<double>(static_cast<uvmword>(v))
                         : static_cast<double>(v));
            break;
        }
        case OP_FloatToInt: push(static_cast<vmword>(pop().d)); break;
        case OP_FloatResize: {
            const double v = pop().d;
            pushD(in.imm == 4 ? static_cast<double>(static_cast<float>(v)) : v);
            break;
        }
        case OP_IntResize: {
            const vmword v = pop().i;
            const int size = static_cast<int>(in.imm);
            if (size <= 0) { trap("integer resize to an impossible width"); break; }
            if (size >= 8) { push(v); break; }
            uvmword masked = static_cast<uvmword>(v) & ((static_cast<uvmword>(1) << (size * 8)) - 1);
            if (in.b) {
                const uvmword signBit = static_cast<uvmword>(1) << (size * 8 - 1);
                if (masked & signBit) masked |= ~((static_cast<uvmword>(1) << (size * 8)) - 1);
            }
            push(static_cast<vmword>(masked));
            break;
        }

        case OP_Jump:       fr.pc = static_cast<int>(in.imm); break;
        case OP_BranchZero: if (pop().i == 0) fr.pc = static_cast<int>(in.imm); break;
        case OP_BranchNZ:   if (pop().i != 0) fr.pc = static_cast<int>(in.imm); break;

        case OP_VTableLoad: {
            const vmword obj = pop().i;
            const vmword vtable = readInt(obj, 8, true);
            push(readInt(vtable + in.imm * 8, 8, true));
            break;
        }

        case OP_Native:
            if (in.imm < 0 || in.imm >= NAT_Count) {
                trap("call to a native this machine does not have");
                break;
            }
            // The count says how many to pop.  Unvalidated it popped for as
            // long as it liked -- setting the trap on the first empty pop and
            // then carrying on for two thousand million more.
            if (in.b < 0 || static_cast<std::size_t>(in.b) > stack.size()) {
                trap("native call wants more arguments than the stack holds");
                break;
            }
            callNative(static_cast<NativeId>(in.imm), static_cast<int>(in.b));
            break;

        case OP_Call:
        case OP_CallIndirect: {
            const int argc = static_cast<int>(in.b);
            // This sizes a vector.  Negative became an enormous size_t and
            // threw length_error; large positive threw bad_alloc.  Nothing
            // catches either, so a single flipped byte in a .cxb aborted the
            // process.  The stack is the true bound: the arguments are on it.
            if (argc < 0 || static_cast<std::size_t>(argc) > stack.size()) {
                trap("call wants more arguments than the stack holds");
                break;
            }
            vmword target = in.imm;
            std::vector<Value> args(argc);
            for (int k = argc - 1; k >= 0; --k) args[k] = pop();
            if (in.op == OP_CallIndirect) target = pop().i;

            if (target < 0 || target >= static_cast<vmword>(image.functions.size())) {
                trap("call to an undefined function");
                break;
            }
            const FuncImage &callee = image.functions[target];
            if (stackTop + callee.frameSize > heapBase) {
                trap("call stack overflow (runaway recursion?)");
                break;
            }
            Frame nf;
            nf.func = static_cast<int>(target);
            nf.pc = 0;
            nf.base = stackTop;
            nf.regBase = callee.localOffset.back();
            nf.wantsResult = true;
            stackTop += callee.frameSize;
            // Arguments land in the first local slots, which is exactly where
            // the parameters were declared.
            for (int k = 0; k < argc && k < static_cast<int>(callee.localSize.size()); ++k) {
                // A float parameter is narrowed here; the operand stack always
                // carries a double, and the slot may be four bytes wide.
                const bool obj = k < static_cast<int>(callee.localObject.size())
                                 && callee.localObject[k] != 0;
                if (obj) {
                    // By value: the argument is the source address, and the
                    // parameter's own slot is the copy the callee owns.
                    const vmword src = args[k].i;
                    const vmword dst = nf.base + callee.localOffset[k];
                    const vmword n = callee.localSize[k];
                    // dst is bounded below as well as above: the offset comes
                    // out of the image, so it can be negative, and a negative
                    // dst passes an upper bound on its own and then writes in
                    // FRONT of memory.  The sums are written as differences
                    // for the same reason -- src + n overflows for an address
                    // near the top of the range and wraps past the check.
                    const vmword limit = static_cast<vmword>(mem.size());
                    if (src <= 0 || dst <= 0 || n < 0 || n > limit ||
                        src > limit - n || dst > limit - n) {
                        trap("object argument at an invalid address");
                        break;
                    }
                    std::memmove(&mem[dst], &mem[src], static_cast<std::size_t>(n));
                    continue;
                }
                const bool flt = k < static_cast<int>(callee.localFloat.size())
                                 && callee.localFloat[k] != 0;
                if (flt) writeFloat(nf.base + callee.localOffset[k], callee.localSize[k], args[k].d);
                else     writeInt(nf.base + callee.localOffset[k], callee.localSize[k], args[k].i);
            }
            frames.push_back(nf);
            break;
        }

        case OP_Return:
        case OP_ReturnVoid: {
            Value rv;
            rv.i = 0;
            if (in.op == OP_Return) rv = pop();
            stackTop = fr.base;
            frames.pop_back();
            if (frames.empty()) {
                result = rv.i;
                // main has returned; global objects are destroyed now, because
                // no scope in the program owns them.
                if (!finiDone && image.fini >= 0 &&
                    image.fini < static_cast<int>(image.functions.size())) {
                    finiDone = true;
                    const FuncImage &ff = image.functions[image.fini];
                    if (stackTop + ff.frameSize <= heapBase && !ff.localOffset.empty()) {
                        Frame nf;
                        nf.func = image.fini;
                        nf.pc = 0;
                        nf.base = stackTop;
                        nf.regBase = ff.localOffset.back();
                        nf.wantsResult = false;
                        stackTop += ff.frameSize;
                        frames.push_back(nf);
                        break;
                    }
                }
                ok = !failed();
                return result;
            }
            stack.push_back(rv);
            break;
        }

        case OP_Alloc:  push(allocate(in.imm, -1)); break;
        case OP_AllocN: {
            const vmword bytes = pop().i;
            vmword count = -1;
            if (in.b != 0) {
                count = pop().i;
                // new T[-1] is not a mistake the language catches for you.
                // Here the size is about to be a negative number of bytes, so
                // it is caught before it becomes one.
                if (count < 0) { trap("negative element count in 'new[]'"); break; }
            }
            push(allocate(bytes, count));
            break;
        }
        case OP_ArrayCount: push(arrayCount(pop().i)); break;
        case OP_Free:   release(pop().i, in.b != 0); break;
        case OP_Halt:  frames.clear(); break;

        // Bytecode.cpp casts a byte straight to OpCode, and 195 of the 256
        // values are not opcodes.  Without this they fell out of the switch as
        // silent no-ops, so a corrupt image RAN, quietly did nothing, and
        // reported success -- the one outcome an untrusted file should never
        // get.  OP_Count is not an instruction; it is here so the switch is
        // exhaustive and the compiler keeps it that way.
        case OP_Count:
        default: {
            std::ostringstream ss;
            ss << "unknown instruction " << static_cast<int>(in.op);
            trap(ss.str());
            break;
        }
        }
    }

    ok = !failed();
    return result;
}

// ---------- main.cpp ----------

#ifndef COMPILERPP_NO_MAIN
//
//  main.cpp
//  Compiler++
//
//  Created by G. R. Akhtar on 29/08/2026.
//
//  Reads a source file and runs it through the front end:
//      Parser -> Semantic -> Layout -> Lower, reporting via Diagnostics.
//
//  `usage()` below is the one statement of what the arguments are; this
//  comment used to be the other, and the two had already drifted apart.
//
//  A .cxb given as the input is loaded and run directly, with no compiling --
//  which is what makes the output a real artifact rather than a print-out.
//
//  There is no default input.  Running an Xcode scheme with no arguments now
//  prints usage instead of compiling one developer's scratch file; pass a path
//  via Product > Scheme > Edit Scheme > Arguments.
//
//  Exit status: 0 clean, 1 errors, 2 the arguments or the file, 3 the VM
//  stopped the program.
//
//  C++98 only.
//

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>


// Windows translates '\n' into "\r\n" on a text-mode stream, so the compiler
// wrote CRLF there while every golden file is LF -- and the whole suite failed
// against a correct compiler, 120 cases out of 120, which reads as a compiler
// fault and is not.  .gitattributes settled this for the bytes going IN; this
// settles it for the bytes coming out, so the compiler writes '\n' on every
// platform it builds on and the suites compare byte for byte wherever they run.
//
// It has to run before anything is written, which is why it is main's first
// statement.  The .cxb path never needed it: Bytecode.cpp opens those streams
// with ios::binary already.
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
static void writeUntranslatedOutput() {
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
}
#else
static void writeUntranslatedOutput() { }
#endif

// The one statement of what the arguments are.  An unrecognised switch used to
// become the input path, so `--help` was reported as a file that could not be
// opened -- and with nothing to report at all, the compiler read an absolute
// path inside one developer's home directory.
static void usage(std::ostream &out) {
    out << "usage: compilerpp [options] <file.cpp | file.cxb>\n"
        << "\n"
        << "  -ast      print the syntax tree\n"
        << "  -layout   print each class's object layout and vtable\n"
        << "  -ir       print the lowered intermediate representation\n"
        << "  -bc       print the generated bytecode\n"
        << "  -run      compile and run, returning the program's own result\n"
        << "  -o FILE   write the compiled image to FILE, a .cxb object file\n"
        << "  -q        diagnostics only, no banner\n"
        << "  -h        print this and stop\n"
        << "\n"
        << "A .cxb given as the input is run directly, with nothing compiled.\n"
        << "\n"
        << "Exit status: 0 clean, 1 errors, 2 the arguments or the file,\n"
        << "3 the VM stopped the program.\n";
}

static bool readFile(const std::string &path, std::string &out) {
    std::ifstream in(path.c_str());
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Diagnostics read better without the path.
static std::string baseName(const std::string &path) {
    const std::string::size_type slash = path.find_last_of('/');
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

int main(int argc, char **argv) {
    writeUntranslatedOutput();

    bool showAst = false;
    bool showLayout = false;
    bool showIR = false;
    bool showBC = false;
    bool doRun = false;
    bool quiet = false;
    std::string path;
    std::string outPath;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if      (arg == "-ast")    showAst = true;
        else if (arg == "-layout") showLayout = true;
        else if (arg == "-ir")     showIR = true;
        else if (arg == "-bc")     showBC = true;
        else if (arg == "-run")    doRun = true;
        else if (arg == "-q")      quiet = true;
        else if (arg == "-h" || arg == "--help") { usage(std::cout); return 0; }
        else if (arg == "-o") {
            // `-o` last on the line used to fall through and become the input
            // path, so the compiler reported "-o" as a file it could not open.
            if (i + 1 >= argc) {
                std::cerr << "-o needs a file to write" << std::endl;
                return 2;
            }
            outPath = argv[++i];
        }
        else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << std::endl;
            usage(std::cerr);
            return 2;
        }
        // A second input silently replaced the first, so a mistyped option
        // took the place of the file it was written beside.
        else if (!path.empty()) {
            std::cerr << "Only one input file, and '" << path << "' is already it: "
                      << arg << std::endl;
            return 2;
        }
        else path = arg;
    }
    if (path.empty()) {
        usage(std::cerr);
        return 2;
    }

    std::string source;
    if (!readFile(path, source)) {
        std::cerr << "Cannot open input file: " << path << std::endl;
        return 2;
    }

    // A .cxb is already compiled: load it and run it.
    if (path.size() > 4 && path.substr(path.size() - 4) == ".cxb") {
        Image image;
        std::string err;
        if (!image.read(path, err)) {
            std::cerr << err << std::endl;
            return 2;
        }
        if (showBC) {
            if (!quiet) std::cout << "=== BYTECODE: " << baseName(path) << " ===" << std::endl;
            image.disassemble();
        }
        if (!doRun) return 0;
        VM vm;
        bool ok = false;
        const vmword result = vm.run(image, ok);
        std::cout.flush();
        if (!ok) {
            std::cerr << baseName(path) << ": runtime error: " << vm.errorMessage() << std::endl;
            return 3;
        }
        if (!quiet) std::cout << "main returned " << result << std::endl;
        return 0;
    }

    Diagnostics diag(baseName(path));

    // parseTranslationUnit() is the C layer's, but every hook it calls is
    // virtual, so a cxx::Parser parses the C++ forms through the same loop.
    // <iostream> is written in this language, so "including" it is prepending
    // it.  The lines it occupies are subtracted from every diagnostic, so the
    // user still sees their own numbering.
    int preludeLines = 0;
    const std::string prelude = preludeFor(source, preludeLines);
    if (!prelude.empty()) {
        source = prelude + source;
        diag.setLineOffset(preludeLines);
    }

    // #define is a textual substitution, so it happens before the first token.
    source = expandDefines(source, diag);

    cxx::Parser parser(source, diag);
    std::vector<cc::Decl*> unit = parser.parseTranslationUnit();

    // Analysis runs even after syntax errors, and BEFORE printing: it writes
    // conclusions back into the tree (virtualness, overrides) that a dump taken
    // earlier would not show.
    {
        SemanticAnalyzer sem(diag);
        sem.analyze(unit);

        if (showAst) {
            if (!quiet) std::cout << "=== SYNTAX TREE: " << baseName(path) << " ===" << std::endl;
            // The prelude is compiler-supplied, so a dump of "the program"
            // means the user's own declarations, not <iostream>'s.
            for (std::size_t i = 0; i < unit.size(); ++i) {
                if (unit[i]->line <= preludeLines) continue;
                unit[i]->print(0);
            }
            std::cout << std::endl;
        }

        // Runs ALWAYS, not only when printing: it enforces constraints of its
        // own, and a diagnostic must not depend on a debug flag.
        Layout layout(diag);
        layout.computeAll(sem.classMap());
        if (showLayout) {
            if (!quiet) std::cout << "=== OBJECT LAYOUT: " << baseName(path) << " ===" << std::endl;
            layout.print();
        }

        // Only over a tree that survived analysis: lowering unresolved names
        // would produce nonsense rather than a better diagnostic.
        if ((showIR || showBC || doRun || !outPath.empty()) && !diag.hadError()) {
            IRModule module;
            {
                cxx::Lowering lower(module, layout, diag, sem.classMap());
                lower.lowerClasses();
                lower.lowerUnit(unit);
            }
            if (showIR) {
                if (!quiet) std::cout << "=== IR: " << baseName(path) << " ===" << std::endl;
                module.print();
            }

            if ((showBC || doRun || !outPath.empty()) && !diag.hadError()) {
                Image image;
                CodeGen gen(diag);
                gen.generate(module, image);

                if (showBC) {
                    if (!quiet) std::cout << "=== BYTECODE: " << baseName(path) << " ===" << std::endl;
                    image.disassemble();
                }
                if (!outPath.empty() && !diag.hadError()) {
                    std::string err;
                    if (!image.write(outPath, err)) {
                        std::cerr << err << std::endl;
                        for (std::size_t i = 0; i < unit.size(); ++i) delete unit[i];
                        return 2;
                    }
                    if (!quiet) std::cout << "wrote " << outPath << std::endl;
                }
                if (doRun && !diag.hadError()) {
                    if (!quiet) std::cout << "=== RUN: " << baseName(path) << " ===" << std::endl;
                    VM vm;
                    bool ok = false;
                    const vmword result = vm.run(image, ok);
                    if (!ok) {
                        std::cout.flush();
                        std::cerr << baseName(path) << ": runtime error: "
                                  << vm.errorMessage() << std::endl;
                        for (std::size_t i = 0; i < unit.size(); ++i) delete unit[i];
                        return 3;
                    }
                    std::cout.flush();
                    if (!quiet) {
                        std::cout << "main returned " << result
                                  << "  (" << vm.stepCount() << " steps)" << std::endl;
                    }
                    for (std::size_t i = 0; i < unit.size(); ++i) delete unit[i];
                    return 0;
                }
            }
        }
    }

    if (!quiet) diag.printSummary();

    for (std::size_t i = 0; i < unit.size(); ++i) delete unit[i];
    return diag.hadError() ? 1 : 0;
}

#endif  // COMPILERPP_NO_MAIN
