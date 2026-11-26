#pragma once

#include <cstddef>
#include <string>

// The overloadable operators, and what each ABI calls them. Every code here
// was measured with clang for both ABIs and confirmed with cl, and CLAUDE.md
// keeps why Itanium needs the arity (`ml` against `de`) where Microsoft does not.
struct OperatorCode {
    const char *spelling;      // as written after `operator`
    const char *itanium;       // the binary form, or the only form
    const char *itaniumUnary;  // the unary form where the token has both
    const char *microsoft;     // written after "??"; arity plays no part
};

// The row for `+`, `[]`, `<<=` and so on, or null when the spelling names no overloadable operator.
const OperatorCode *findOperator(const std::string &spelling);

// "operator+" -> "+", and empty for a name that is not an operator's.
std::string operatorSpelling(const std::string &name);

// `unary` decides between the two Itanium codes and is ignored by Microsoft.
const char *itaniumOperatorCode(const OperatorCode &op, bool unary);

// **[over.literal]: a literal operator is filed as `operator"" _km`**; its suffix, else empty.
std::string literalSuffix(const std::string &name);
