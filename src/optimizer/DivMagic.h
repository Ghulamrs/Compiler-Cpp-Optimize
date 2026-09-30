#pragma once

// Hacker's Delight's multipliers for division by a 32-bit constant, shared by
// the x86 pass (OptDivide.cpp) and the C6000 backend (Tms6747.cpp).
namespace opt {
void signedMagic(unsigned d, unsigned long long &M, int &s);
bool unsignedMagic(unsigned d, unsigned long long &M, int &s);
void unsignedMagicAdd(unsigned d, unsigned long long &M, int &s, bool &add);
}
