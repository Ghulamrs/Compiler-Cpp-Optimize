#include <ratio>
#include <cstdio>
typedef std::ratio<6, -4> r1;
static_assert(r1::num == -3 && r1::den == 2, "reduce");
typedef std::ratio_add<std::ratio<1, 3>, std::ratio<1, 6> > s;
static_assert(s::num == 1 && s::den == 2, "add");
typedef std::ratio_subtract<std::ratio<1, 3>, std::ratio<1, 2> > d;
static_assert(d::num == -1 && d::den == 6, "sub");
typedef std::ratio_multiply<std::ratio<2, 3>, std::ratio<9, 4> > m;
static_assert(m::num == 3 && m::den == 2, "mul");
typedef std::ratio_divide<std::milli, std::micro> q;
static_assert(q::num == 1000 && q::den == 1, "div");
static_assert(std::ratio_less<std::ratio<1, 3>, std::ratio<1, 2> >::value, "less");
static_assert(!std::ratio_less<std::ratio<1, 2>, std::ratio<1, 3> >::value, "less2");
static_assert(std::ratio_less<std::ratio<-1, 2>, std::ratio<1, 3> >::value, "less3");
static_assert(std::ratio_less<std::ratio<-1, 2>, std::ratio<-1, 3> >::value, "less4");
static_assert(std::ratio_less<std::ratio<9223372036854775805LL, 9223372036854775806LL>, std::ratio<9223372036854775806LL, 9223372036854775807LL> >::value, "cf");
static_assert(std::ratio_equal<std::ratio<2, 4>, std::ratio<1, 2> >::value, "eq");
static_assert(std::ratio_greater_equal<std::kilo, std::kilo>::value, "ge");
static_assert(std::is_same<std::ratio<4, 8>::type, std::ratio<1, 2> >::value, "type");
int main() {
    const intmax_t *p = &std::ratio<3, 9>::num;
    std::printf("%lld %lld %lld\n", (long long)*p, (long long)std::ratio<3, 9>::den, (long long)std::exa::num);
    return 0;
}
