#include <cstdio>
struct S {}; S gs;
int main() { try { throw &gs; } catch (const S *p) { std::printf("%d ", (int)(p == &gs)); } try { throw nullptr; } catch (int *p) { std::printf("%d ", (int)(p == nullptr)); } catch (...) { std::printf("dots "); } try { const int ci = 3; throw &ci; } catch (const int *p) { std::printf("%d ", *p); } try { throw "lit"; } catch (const char *s) { std::printf("%s", s); } std::printf("\n"); return 0; }
