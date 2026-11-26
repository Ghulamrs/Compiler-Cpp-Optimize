#include <cstdio>
struct V { virtual ~V() = 0; }; V::~V() { std::printf("~V "); } struct I : V { ~I() override { std::printf("~I "); } };
int main() { { I i; } std::printf("\n"); return 0; }
