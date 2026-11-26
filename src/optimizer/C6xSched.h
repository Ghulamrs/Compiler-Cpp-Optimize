#pragma once

// The C6000 optimizer over one function's emitted text: the -O0 code is
// serial and padded to every latency, so with its NOPs removed it reads as a
// sequential program, and the pass re-pads it to the hazards that remain.

#include <string>

// The text with every padding NOP replaced by the wait the pending writes require.
// A volatile access is marked in the text (C6xModel.h, kVolatileMark); the marks are taken off whatever the level.
// `volatileLost` is set when fewer marked accesses leave the passes than entered them - a pass dropped or merged one,
// which none may: the caller then writes the function as at -O0.
std::string c6xSchedule(const std::string &text, int level, bool *volatileLost = nullptr);

// The backend's side of the mark: every load and store written from byte `from` of the text on is marked, but those
// through B15 - the expression stack and the outgoing arguments, which hold no object of the program's.
std::string c6xMarkVolatile(const std::string &text, std::size_t from);
