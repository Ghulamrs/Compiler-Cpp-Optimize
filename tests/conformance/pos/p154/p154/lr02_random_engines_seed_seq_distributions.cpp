#include <random>
#include <cstdio>
#include <sstream>
#include <vector>
#include <cmath>

template <class E> unsigned long long tenThousandth() {
    E e;
    e.discard(9999);
    return e();
}

int main() {
    std::printf("minstd_rand0 %llu\n", tenThousandth<std::minstd_rand0>());
    std::printf("minstd_rand %llu\n", tenThousandth<std::minstd_rand>());
    std::printf("mt19937 %llu\n", tenThousandth<std::mt19937>());
    std::printf("mt19937_64 %llu\n", tenThousandth<std::mt19937_64>());
    std::printf("ranlux24_base %llu\n", tenThousandth<std::ranlux24_base>());
    std::printf("ranlux48_base %llu\n", tenThousandth<std::ranlux48_base>());
    std::printf("ranlux24 %llu\n", tenThousandth<std::ranlux24>());
    std::printf("ranlux48 %llu\n", tenThousandth<std::ranlux48>());
    std::printf("knuth_b %llu\n", tenThousandth<std::knuth_b>());

    std::seed_seq seq{1, 2, 3};
    std::vector<unsigned> out(6);
    seq.generate(out.begin(), out.end());
    for (unsigned v : out) std::printf("%u ", v);
    std::printf("\n");
    std::seed_seq seq2{5, 7};
    std::mt19937 ms(seq2);
    std::printf("mt19937(seq) %u\n", (unsigned)ms());
    std::seed_seq seq3{5, 7};
    std::minstd_rand ls(seq3);
    std::printf("minstd_rand(seq) %u\n", (unsigned)ls());
    std::seed_seq seq4{5, 7};
    std::ranlux24_base rs(seq4);
    std::printf("ranlux24_base(seq) %u\n", (unsigned)rs());

    std::mt19937 a(42), b;
    a.discard(5);
    std::ostringstream os1;
    os1 << a;
    std::istringstream is1(os1.str());
    is1 >> b;
    std::printf("round trip %d %d\n", (int)(a == b), (int)(a() == b()));

    std::independent_bits_engine<std::mt19937, 64, unsigned long long> ib;
    std::printf("ibits %llu\n", ib());
    std::independent_bits_engine<std::minstd_rand, 12, unsigned> ib2;
    std::printf("ibits12 %u\n", ib2());

    std::mt19937 g(1);
    std::uniform_int_distribution<int> die(1, 6);
    int counts[7] = {0};
    for (int i = 0; i < 60000; i++) counts[die(g)]++;
    bool inRange = true;
    for (int k = 1; k <= 6; k++) inRange = inRange && counts[k] > 9000 && counts[k] < 11000;
    std::printf("dice %d %d\n", (int)inRange, counts[0]);

    std::uniform_real_distribution<double> ur(2.0, 3.0);
    double lo = 10, hi = -10, sum = 0;
    for (int i = 0; i < 10000; i++) { double x = ur(g); lo = x < lo ? x : lo; hi = x > hi ? x : hi; sum += x; }
    std::printf("uniform_real %d %d\n", (int)(lo >= 2.0 && hi < 3.0), (int)(std::fabs(sum / 10000 - 2.5) < 0.02));

    std::normal_distribution<double> nd(5.0, 2.0);
    double s1 = 0, s2 = 0;
    for (int i = 0; i < 20000; i++) { double x = nd(g); s1 += x; s2 += x * x; }
    double mean = s1 / 20000, var = s2 / 20000 - mean * mean;
    std::printf("normal %d %d\n", (int)(std::fabs(mean - 5.0) < 0.1), (int)(std::fabs(var - 4.0) < 0.2));

    std::poisson_distribution<int> small(3.5), big(40.0);
    double ps = 0, pb = 0;
    for (int i = 0; i < 20000; i++) { ps += small(g); pb += big(g); }
    std::printf("poisson %d %d\n", (int)(std::fabs(ps / 20000 - 3.5) < 0.1), (int)(std::fabs(pb / 20000 - 40.0) < 0.3));

    std::bernoulli_distribution coin(0.25);
    int heads = 0;
    for (int i = 0; i < 20000; i++) heads += coin(g);
    std::printf("bernoulli %d\n", (int)(heads > 4600 && heads < 5400));

    std::binomial_distribution<int> bin(20, 0.3);
    double bs = 0;
    for (int i = 0; i < 20000; i++) bs += bin(g);
    std::printf("binomial %d\n", (int)(std::fabs(bs / 20000 - 6.0) < 0.1));

    std::geometric_distribution<int> geo(0.2);
    std::negative_binomial_distribution<int> nb(3, 0.5);
    double gs = 0, ns = 0;
    for (int i = 0; i < 20000; i++) { gs += geo(g); ns += nb(g); }
    std::printf("geometric %d negbin %d\n", (int)(std::fabs(gs / 20000 - 4.0) < 0.15), (int)(std::fabs(ns / 20000 - 3.0) < 0.15));

    std::exponential_distribution<double> ex(2.0);
    std::gamma_distribution<double> ga(2.0, 3.0), gsmall(0.5, 1.0);
    std::chi_squared_distribution<double> chi(4.0);
    double es = 0, gas = 0, gss = 0, cs = 0;
    for (int i = 0; i < 20000; i++) { es += ex(g); gas += ga(g); gss += gsmall(g); cs += chi(g); }
    std::printf("exponential %d gamma %d %d chi %d\n", (int)(std::fabs(es / 20000 - 0.5) < 0.02), (int)(std::fabs(gas / 20000 - 6.0) < 0.15),
                (int)(std::fabs(gss / 20000 - 0.5) < 0.03), (int)(std::fabs(cs / 20000 - 4.0) < 0.1));

    std::weibull_distribution<double> wb(2.0, 1.0);
    std::lognormal_distribution<double> ln(0.0, 0.5);
    std::extreme_value_distribution<double> ev(0.0, 1.0);
    std::student_t_distribution<double> st(10.0);
    std::fisher_f_distribution<double> ff(5.0, 10.0);
    std::cauchy_distribution<double> cy(0.0, 1.0);
    double ws = 0, ls2 = 0, evs = 0, sts = 0, ffs = 0;
    int cyAbove = 0;
    for (int i = 0; i < 20000; i++) { ws += wb(g); ls2 += ln(g); evs += ev(g); sts += st(g); ffs += ff(g); cyAbove += cy(g) > 0; }
    std::printf("weibull %d lognormal %d extreme %d student %d fisher %d cauchy %d\n", (int)(std::fabs(ws / 20000 - 0.8862) < 0.02),
                (int)(std::fabs(ls2 / 20000 - 1.1331) < 0.02), (int)(std::fabs(evs / 20000 - 0.5772) < 0.03), (int)(std::fabs(sts / 20000) < 0.05),
                (int)(std::fabs(ffs / 20000 - 1.25) < 0.08), (int)(cyAbove > 9600 && cyAbove < 10400));

    std::discrete_distribution<int> dd{1, 3};
    int ones = 0;
    for (int i = 0; i < 20000; i++) ones += dd(g);
    std::printf("discrete %d %g %g\n", (int)(ones > 14600 && ones < 15400), dd.probabilities()[0], dd.probabilities()[1]);

    std::vector<double> bounds = {0.0, 1.0, 3.0}, dens = {1.0, 1.0};
    std::piecewise_constant_distribution<double> pc(bounds.begin(), bounds.end(), dens.begin());
    double pcs = 0;
    for (int i = 0; i < 20000; i++) pcs += pc(g);
    std::printf("piecewise_constant %d %g\n", (int)(std::fabs(pcs / 20000 - 1.25) < 0.05), pc.densities()[0]);

    std::vector<double> lb = {0.0, 1.0}, lw = {0.0, 1.0};
    std::piecewise_linear_distribution<double> pl(lb.begin(), lb.end(), lw.begin());
    double pls = 0;
    for (int i = 0; i < 20000; i++) pls += pl(g);
    std::printf("piecewise_linear %d\n", (int)(std::fabs(pls / 20000 - 2.0 / 3.0) < 0.02));

    std::normal_distribution<double> n2(1.0, 3.0), n3;
    std::ostringstream os2;
    os2 << n2;
    std::istringstream is2(os2.str());
    is2 >> n3;
    std::printf("dist round trip %d %g %g\n", (int)(n2 == n3), n3.mean(), n3.stddev());

    std::random_device rd;
    unsigned r1 = rd(), r2 = rd();
    std::printf("random_device %d %d\n", (int)(r1 != r2 || r1 != 0), (int)(rd.entropy() >= 0));
    double c = std::generate_canonical<double, 53>(g);
    std::printf("canonical %d\n", (int)(c >= 0.0 && c < 1.0));
    return 0;
}
