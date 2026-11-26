#include <cstdio>
struct T { const char *n; T(const char *n) : n(n) {} ~T() { std::printf("~%s ", n); } };
int main() { try { throw 1; } catch (int) { T t("h"); try { throw 2; } catch (int i) { std::printf("in%d ", i); } std::printf("after "); } std::printf("\n"); return 0; }
