// The other way is still refused: `const char (*)[3]` to `char (*)[3]`
// would let the elements be written through.
int main() { char g[2][3] = {"ab", "cd"}; const char (*p)[3] = g; char (*q)[3] = p; return q == 0; }
