#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

static std::mutex m;
static long counter = 0;
static std::atomic<int> hits(0);
static std::once_flag once;
static int onceRuns = 0;

static void work(int n) {
    for (int i = 0; i < n; i++) {
        std::lock_guard<std::mutex> g(m);
        ++counter;
    }
    hits.fetch_add(n);
    std::call_once(once, [] { ++onceRuns; });
}

static int square(int x) { return x * x; }

int main() {
    std::vector<std::thread> pool;
    for (int t = 0; t < 4; t++) pool.push_back(std::thread(work, 1000));
    for (std::thread &t : pool) t.join();
    std::printf("counter %ld hits %d once %d\n", counter, hits.load(), onceRuns);

    std::atomic<long long> a(5);
    long long expected = 5;
    bool swapped = a.compare_exchange_strong(expected, 9);
    long long expected2 = 5;
    bool swapped2 = a.compare_exchange_strong(expected2, 11);
    std::printf("cas %d %lld %d %lld\n", (int)swapped, a.load(), (int)swapped2, expected2);
    std::atomic<int *> p(nullptr);
    int arr[4] = {1, 2, 3, 4};
    p = arr;
    p += 2;
    std::printf("ptr %d\n", *p.load());
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
    bool first = flag.test_and_set(), second = flag.test_and_set();
    flag.clear();
    std::printf("flag %d %d %d\n", (int)first, (int)second, (int)flag.test_and_set());

    std::condition_variable cv;
    std::mutex cm;
    bool ready = false;
    int value = 0;
    std::thread producer([&] {
        std::lock_guard<std::mutex> g(cm);
        value = 42;
        ready = true;
        cv.notify_one();
    });
    {
        std::unique_lock<std::mutex> lk(cm);
        cv.wait(lk, [&] { return ready; });
    }
    producer.join();
    std::printf("cv %d\n", value);
    {
        std::unique_lock<std::mutex> lk(cm);
        bool got = cv.wait_for(lk, std::chrono::milliseconds(20), [] { return false; });
        std::printf("timeout %d\n", (int)got);
    }

    std::promise<int> pr;
    std::future<int> fu = pr.get_future();
    std::thread setter([&pr] { pr.set_value(7); });
    std::printf("promise %d\n", fu.get());
    setter.join();

    std::future<int> as = std::async(std::launch::async, square, 12);
    std::future<int> df = std::async(std::launch::deferred, square, 5);
    std::printf("async %d deferred %d\n", as.get(), df.get());

    std::packaged_task<int(int)> task(square);
    std::future<int> tf = task.get_future();
    std::thread runner(std::move(task), 9);
    runner.join();
    std::printf("task %d\n", tf.get());

    std::promise<void> broken;
    std::future<void> bf = broken.get_future();
    std::promise<void>().swap(broken);
    try {
        bf.get();
        std::printf("no throw\n");
    } catch (const std::future_error &e) {
        std::printf("future_error %d\n", (int)(e.code() == std::future_errc::broken_promise));
    }

    std::future<int> thrower = std::async(std::launch::async, [] () -> int { throw std::runtime_error("boom"); });
    try {
        thrower.get();
    } catch (const std::runtime_error &e) {
        std::printf("caught %s\n", e.what());
    }
    std::shared_future<int> sf = std::async(std::launch::async, square, 3).share();
    std::printf("shared %d %d\n", sf.get(), sf.get());

    std::recursive_mutex rm;
    rm.lock();
    bool again = rm.try_lock();
    rm.unlock();
    rm.unlock();
    std::timed_mutex tm;
    tm.lock();
    bool timed = tm.try_lock_for(std::chrono::milliseconds(10));
    tm.unlock();
    std::printf("recursive %d timed %d\n", (int)again, (int)timed);
    std::mutex m1, m2;
    std::lock(m1, m2);
    m1.unlock();
    m2.unlock();
    std::thread::id me = std::this_thread::get_id();
    std::printf("ids %d %d\n", (int)(me == std::this_thread::get_id()), (int)(std::thread::id() != me));
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::printf("cores %d\n", (int)(std::thread::hardware_concurrency() > 0));
    return 0;
}
