#include "Operator.h"

#include <cstring>

namespace {

// Measured, one operator per row. The parameter type beside each is the one the
// measurement gave it, kept because it is what makes the row checkable.
//   spelling  itanium  unary   microsoft   measured with
const OperatorCode table[] = {
    { "+",    "pl",  "ps",    "H"  },   // char   / no parameter
    { "-",    "mi",  "ng",    "G"  },   // short  / no parameter
    { "*",    "ml",  "de",    "D"  },   // int    / no parameter
    { "&",    "an",  "ad",    "I"  },   // double / no parameter
    { "/",    "dv",  nullptr, "K"  },   // long
    { "%",    "rm",  nullptr, "L"  },   // float
    { "|",    "or",  nullptr, "U"  },   // unsigned char
    { "^",    "eo",  nullptr, "T"  },   // unsigned short
    { "<<",   "ls",  nullptr, "6"  },   // unsigned int
    { ">>",   "rs",  nullptr, "5"  },   // unsigned long
    { "==",   "eq",  nullptr, "8"  },   // char
    { "!=",   "ne",  nullptr, "9"  },   // short
    { "<",    "lt",  nullptr, "M"  },   // int
    { "<=",   "le",  nullptr, "N"  },   // float
    { ">",    "gt",  nullptr, "O"  },   // long
    { ">=",   "ge",  nullptr, "P"  },   // double
    { "=",    "aS",  nullptr, "4"  },
    { "+=",   "pL",  nullptr, "Y"  },   // char
    { "-=",   "mI",  nullptr, "Z"  },   // short
    { "*=",   "mL",  nullptr, "X"  },   // int
    { "/=",   "dV",  nullptr, "_0" },   // long
    { "%=",   "rM",  nullptr, "_1" },   // float
    { "&=",   "aN",  nullptr, "_4" },   // double
    { "|=",   "oR",  nullptr, "_5" },   // unsigned char
    { "^=",   "eO",  nullptr, "_6" },   // unsigned short
    { "<<=",  "lS",  nullptr, "_3" },   // unsigned int
    { ">>=",  "rS",  nullptr, "_2" },   // unsigned long
    { "[]",   "ix",  nullptr, "A"  },   // char
    { "()",   "cl",  nullptr, "R"  },   // short
    { "&&",   "aa",  nullptr, "V"  },   // int
    { "||",   "oo",  nullptr, "W"  },   // long
    { ",",    "cm",  nullptr, "Q"  },   // float
    { "!",    "nt",  nullptr, "7"  },
    { "~",    "co",  nullptr, "S"  },
    { "++",   "pp",  nullptr, "E"  },
    { "--",   "mm",  nullptr, "F"  },
    { "->",   "pt",  nullptr, "C"  },
    // The allocation functions, named as operators: `_Znwm` and `_ZdlPv` are
    // exactly these codes with their parameters. cl: ??2 and ??3.
    { "new",    "nw",  nullptr, "2"  },
    { "delete", "dl",  nullptr, "3"  },
    // The array forms: `_Znam` and `_ZdaPv`; cl ??_U and ??_V.
    { "new[]",    "na",  nullptr, "_U" },
    { "delete[]", "da",  nullptr, "_V" },
};

const std::size_t count = sizeof table / sizeof table[0];

}  // namespace

const OperatorCode *findOperator(const std::string &spelling) {
    for (std::size_t i = 0; i < count; i++)
        if (spelling == table[i].spelling) return &table[i];
    return nullptr;
}

std::string operatorSpelling(const std::string &name) {
    const std::size_t prefix = 8;             // "operator"
    if (name.compare(0, prefix, "operator") != 0) return std::string();
    if (name.size() <= prefix) return std::string();
    return name.substr(prefix);
}

const char *itaniumOperatorCode(const OperatorCode &op, bool unary) {
    if (unary && op.itaniumUnary != nullptr) return op.itaniumUnary;
    return op.itanium;
}
