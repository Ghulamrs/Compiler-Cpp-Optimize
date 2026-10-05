#!/usr/bin/env python3
# Normalise the TEXT section of a dis6x listing to the multiset of instructions
# it encodes - each as its mnemonic base and register/immediate operands - so
# that ASM6x and TI's assembler compare at the level of what the code DOES, not
# how the assembler laid it out. A real encoding fault (the C2 report's DPSP
# A5:A4 written as A1:A0 - a wrong source register) changes an operand and is
# caught; what is deliberately dropped is every legal assembler choice that is
# not a correctness difference:
#   - the functional unit (.L1 / .S2X / .D1): either assembler may place an
#     instruction on any unit that can run it;
#   - packet grouping and order: the caller sorts, so order within and across
#     packets does not matter; || and NOP padding go here;
#   - the address, the raw hex word, the [pred] guard's form, and a branch's
#     resolved (PC... = 0x...) target, a symbol-table artifact.
# Only the TEXT section is read: .const and the .c6xabi.exidx unwind table are
# data, not code, and differ by relocation rather than by encoding.
import re
import sys

BRANCHES = ("B", "BNOP", "BDEC", "BPOS", "CALL", "CALLP", "ADDKPC")


def norm(path):
    out = []
    in_text = False
    for line in open(path):
        line = line.rstrip("\n")
        m = re.match(r'\s*(TEXT|DATA) Section', line)
        if m:
            in_text = (m.group(1) == "TEXT")
            continue
        if not in_text:
            continue
        m = re.match(r'[0-9a-f]{8}\s+[0-9a-f]{4,8}\s+(.*)$', line)
        if not m:
            continue
        rest = m.group(1).strip().replace("||", " ").strip()
        rest = re.sub(r'\[\s*!?[AB]\d+\s*\]', '', rest).strip()
        if not rest:
            continue
        parts = rest.split(None, 1)
        mnem = parts[0].split('.')[0]      # drop the unit suffix
        if mnem == "NOP":
            continue
        ops = parts[1] if len(parts) > 1 else ""
        ops = re.sub(r'\([^)]*\)', '', ops)
        ops = re.sub(r'\s+', '', ops)
        if mnem in BRANCHES:
            head, sep, tail = ops.partition(",")
            if not re.fullmatch(r'[AB]\d+', head):
                head = "TGT"
            ops = head + sep + tail
        out.append(mnem + " " + ops)
    return out


if __name__ == "__main__":
    for l in sorted(norm(sys.argv[1])):
        print(l)
