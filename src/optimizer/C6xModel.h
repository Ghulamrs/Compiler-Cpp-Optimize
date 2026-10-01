#pragma once

// The C6000 machine model the text passes share: a parsed line, the scheduler's view of an instruction, an execute
// packet being filled, and the readers of the ISA - delay slots, units, cross paths - written once in C6xSched.cpp
// and read by the software pipeliner in C6xPipe.cpp.

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace c6x {

enum { UL = 1, US = 2, UD = 4, UM = 8 };   // the units, as a bitmask

struct Line {
    std::string raw;
    bool instr = false;
    bool verbatim = false;              // scheduled already: printed as it stands, never rescheduled
    std::string pred;                   // "A1" or "!A1", read as A1
    std::string mnem;
    std::vector<std::string> ops;
    std::uint64_t liveOut = ~0ull;      // the registers live at the end of this line's block
    std::uint64_t liveIn = ~0ull;       // and at its start
};

// One instruction as the scheduler sees it: what it reads and writes, its
// latency, the units it has a form on, its side and cross path, its memory
// access, and the edges from the earlier instructions it must follow.
struct Node {
    Line line;
    std::uint64_t reads = 0, writes = 0;
    std::uint64_t stepped = 0;          // the address register a `*R++` or `*R--` access writes, in E1 rather than at its latency
    int lat = 1;
    int late = 0;                       // the cycles after issue its sources are still read, and its unit held
    unsigned units = 0;                 // a bitmask of UL, US, UD, UM; 0 for a form the table does not know
    int side = -1;                      // 0 A, 1 B, -1 either (a branch to a label)
    int cross = -1;                     // the cross path used, by the side that reads: 0 1X, 1 2X, -1 none
    bool mem = false, store = false;
    int tpath = -1;                     // a load's or store's data path: the data register's side
    std::string memBase; long memOff = 0; int memSize = 0; bool memFrame = false;
    bool branch = false, call = false;
    std::vector<std::pair<int, int> > preds;   // (earlier node, the least distance in cycles)
    int issue = -1, height = 0;
};

// The packet being filled at one cycle: the units taken on each side, the
// cross paths, the data paths, the registers written and the branch.
struct Packet {
    bool unit[2][4] = { { false, false, false, false }, { false, false, false, false } };
    bool held[2][4] = { { false, false, false, false }, { false, false, false, false } };   // by an earlier cycle's DP instruction
    bool cross[2] = { false, false }, tpath[2] = { false, false };
    std::uint64_t writes = 0;
    bool branch = false, alone = false;
    int count = 0;
    std::vector<const Node *> members;
};

bool startsWith(const std::string &s, const char *p);
bool endsWith(const std::string &s, const char *p);
int delaySlots(const std::string &m);
int lateReads(const std::string &m);
Line parse(const std::string &raw);
Line make(const std::string &mnem, const std::string &a, const std::string &b = "", const std::string &c = "");
Line rebuilt(const std::string &mnem, const std::vector<std::string> &ops, const std::string &pred = "");
void registersIn(const std::string &op, std::vector<std::string> &out);
bool isStore(const std::string &m);
bool isLoad(const std::string &m);
char sideOf(const std::string &r);
int sideIndex(const std::string &r);
std::uint64_t bitOf(const std::string &r);
std::uint64_t maskOf(const std::vector<std::string> &regs);
void readsAndWrites(const Line &l, std::vector<std::string> &reads, std::vector<std::string> &writes);
bool has(const std::vector<std::string> &v, const std::string &r);
bool isNumber(const std::string &s);
bool isLabel(const Line &l);
std::string labelName(const Line &l);
bool isBranch(const std::string &m);
bool blockEnd(const Line &l);
bool isCall(const Line &l, const std::set<std::string> &labels);
void computeLiveness(std::vector<Line> &v);
int accessSize(const std::string &m);
std::string renamed(const std::string &op, const std::string &from, const std::string &to);
int crossings(const Line &l);
unsigned unitsFor(const Line &l);
Node makeNode(const Line &l, const std::set<std::string> &labels);
bool fits(const Packet &p, const Node &n);
void add(Packet &p, const Node &n);
bool passThrough(const Line &l, const std::set<std::string> &named);
std::string steppedRegister(const Line &l, long &by);

// The software pipeliner (C6xPipe.cpp): the innermost counted loops of the text rewritten as verbatim lines.
void pipelineLoops(std::vector<Line> &v);

}   // namespace c6x
