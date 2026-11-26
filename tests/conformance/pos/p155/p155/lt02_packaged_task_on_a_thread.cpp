#include <future>
#include <thread>
#include <cstdio>
static int square(int x) { return x * x; }
int main() {
    std::packaged_task<int(int)> task(square);
    std::future<int> tf = task.get_future();
    std::thread runner(std::move(task), 9);
    runner.join();
    std::printf("task %d\n", tf.get());
    return 0;
}
