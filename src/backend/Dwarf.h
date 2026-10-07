#pragma once

#include "../Ast.h"
#include "../Type.h"

#include <string>
#include <vector>

struct DwarfBlock {
    int parent;
    std::string begin;
    std::string end;
};

struct DwarfFunction {
    std::string name;
    std::string begin;
    std::string end;
    int file;
    int line;
    bool external;
    const Type *returns;
    const std::vector<Local> *locals;

    std::vector<DwarfBlock> blocks;

    // For CodeView (CodeView.h), and spelled as a table names them: the symbol,
    // the labels after the prologue and after the last instruction, whether the
    // code is a COMDAT, and the frame slot's displacement from RBP (rbp + bias - offset).
    std::string symbol;
    std::string prologEnd;
    std::string codeEnd;
    bool mergeable = false;
    int frameBias = 0;
};

struct DwarfGlobal {
    std::string name;
    std::string symbol;
    const Type *type;
    bool external;
};

struct DwarfSpelling {
    const char *abbrev;
    const char *info;

    const char *line;

    bool offsetsAreLabels;
    int frameBaseReg;
    const char *symbolPrefix;
};

extern const DwarfSpelling kElfDwarf;
extern const DwarfSpelling kMachODwarf;

void writeDwarf(std::string &out, const DwarfSpelling &sp, const Target &target,
                const std::string &file, const std::string &compDir,
                const std::vector<DwarfFunction> &fns,
                const std::vector<DwarfGlobal> &globals);
