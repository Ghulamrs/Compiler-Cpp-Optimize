#pragma once

// **The table of measured facts about how this target passes and returns things.**
struct Abi {
    // The registers arguments travel in, and how many there are of each.
    const char *const *intRegs = nullptr;
    int intCount = 0;
    const char *const *sseRegs = nullptr;
    int sseCount = 0;

    // **An argument takes a slot from both counters where this is set**.
    bool positional = false;

    // Room the caller leaves for the callee to spill its register arguments.
    int shadowBytes = 0;

    // The largest class that comes back in registers rather than through a hidden pointer.
    int structReturnLimit = 0;

    // **Microsoft's rule, and only Microsoft's**.
    bool aggregatesByReference = false;

    // A variadic call puts the number of SSE registers it used in al.
    bool variadicSseCountInAl = false;

    // The scratch register this target's code generator borrows, in its 64-bit
    // and 32-bit spellings.
    const char *scratch = nullptr;
    const char *scratch32 = nullptr;
    // The registers a callee must give back as it found them; a call may
    // change every other one, which is what the optimizer needs to know.
    const char *const *preservedRegs = nullptr;
    int preservedCount = 0;

    // **AAPCS64 returns one to four floats or doubles in the float registers.**
    bool homogeneousFloatAggregates = false;

    // `.type` and `.size` beside a symbol: ELF has them, Mach-O and COFF do not
    bool elfSymbolAttributes = false;
};
