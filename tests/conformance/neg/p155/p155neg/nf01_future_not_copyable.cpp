#include <future>
int main() { std::future<int> f; std::future<int> g = f; return 0; }
