#include <chrono>
#include <cstdio>
using namespace std::chrono;
int main() {
    milliseconds ms(1500);
    seconds s = duration_cast<seconds>(ms);
    microseconds us = ms;
    duration<double> ds = ms;
    std::printf("%lld %lld %.3f\n", (long long)s.count(), (long long)us.count(), ds.count());
    auto sum = seconds(1) + milliseconds(250);
    std::printf("%lld %d\n", (long long)sum.count(), (int)std::is_same<decltype(sum), milliseconds>::value);
    std::printf("%d %d %d\n", (int)(seconds(1) == milliseconds(1000)), (int)(minutes(1) > seconds(59)), (int)(hours(1) / minutes(1)));
    minutes mm = duration_cast<minutes>(seconds(-90));
    std::printf("%lld %lld\n", (long long)mm.count(), (long long)(seconds(100) % seconds(30)).count());
    auto t0 = steady_clock::now();
    volatile long x = 0;
    for (long i = 0; i < 1000000; ++i) x += i;
    auto t1 = steady_clock::now();
    std::printf("steady %d\n", (int)(t1 >= t0));
    system_clock::time_point now = system_clock::now();
    std::time_t tt = system_clock::to_time_t(now);
    std::time_t ct = std::time(0);
    std::printf("sys %d\n", (int)(tt - ct <= 1 && ct - tt <= 1));
    auto tp = time_point_cast<seconds>(system_clock::from_time_t(1000));
    std::printf("%lld\n", (long long)tp.time_since_epoch().count());
    std::printf("%d %d\n", (int)steady_clock::is_steady, (int)system_clock::is_steady);
    duration<int, std::ratio<1, 3> > third(1);
    duration<int, std::ratio<1, 2> > half(1);
    auto c = third + half;
    std::printf("%d %lld\n", (int)c.count(), (long long)decltype(c)::period::den);
    milliseconds m2 = ms * 2;
    m2 /= 3;
    std::printf("%lld %lld\n", (long long)m2.count(), (long long)(3 * ms).count());
    return 0;
}
