#include <future>
int main() { std::future<int> f = std::async(std::launch::deferred, [] { return 5; }); return f.get() - 5; }
