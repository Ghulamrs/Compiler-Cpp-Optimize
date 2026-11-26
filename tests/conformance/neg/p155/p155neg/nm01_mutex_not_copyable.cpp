#include <mutex>
int main() { std::mutex a; std::mutex b = a; (void)b; return 0; }
