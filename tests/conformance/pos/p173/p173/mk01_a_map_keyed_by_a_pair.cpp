// [map.access]/5 with [tuple.cnstr]: operator[] with an rvalue key makes the element from
// forward_as_tuple(std::move(key)), a tuple of one element. When the key is itself a pair, that tuple is built
// from the pair as its one element - the constructors that take a pair apart belong to a tuple of two elements
// ([tuple.cnstr]/17-22) and are not candidates here.
#include <cstdio>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

typedef std::pair<int, std::vector<long> > Key;

static Key key(int owner, long a, long b) {
    std::vector<long> stack;
    stack.push_back(a);
    stack.push_back(b);
    return Key(owner, stack);
}

int main() {
    std::map<Key, std::vector<long> > openings;
    openings[key(2, 10, 20)].push_back(7);               // an rvalue key, the element made in place
    openings[key(1, 30, 40)].push_back(8);
    openings[key(2, 10, 20)].push_back(9);               // found again
    Key named = key(1, 5, 6);
    openings[named].push_back(1);                         // an lvalue key
    openings[std::move(named)].push_back(2);              // the same key, moved from only if it were inserted
    for (std::map<Key, std::vector<long> >::const_iterator i = openings.begin(); i != openings.end(); ++i) {
        std::printf("%d [%ld %ld]:", i->first.first, i->first.second[0], i->first.second[1]);
        for (std::size_t k = 0; k < i->second.size(); ++k) std::printf(" %ld", i->second[k]);
        std::printf("\n");
    }
    std::printf("named still holds %d element(s)\n", (int)named.second.size());

    // (a key whose member is a std::string is EXCLUSIONS.md's: its program does not link)
    std::map<std::pair<long, int>, int> counts;
    counts[std::make_pair(7L, 1)] += 3;
    counts[std::pair<long, int>(7, 1)] += 4;
    counts[std::make_pair(8L, 0)] = 5;
    std::printf("%d entries, 7,1=%d 8,0=%d\n", (int)counts.size(), counts[std::make_pair(7L, 1)],
                counts[std::make_pair(8L, 0)]);

    // the tuple itself: one element that is a pair, and two elements from a pair
    std::pair<int, long> p(3, 4L);
    std::tuple<std::pair<int, long> > one(p);
    std::tuple<std::pair<int, long> &&> ref = std::forward_as_tuple(std::move(p));
    std::tuple<long, long> two(p);
    std::tuple<long, double> moved(std::make_pair(5, 6.5f));
    std::printf("%d %ld | %d %ld | %ld %ld | %ld %.1f\n", std::get<0>(one).first, std::get<0>(one).second,
                std::get<0>(ref).first, std::get<0>(ref).second, std::get<0>(two), std::get<1>(two),
                std::get<0>(moved), std::get<1>(moved));
    std::printf("%d %d\n", (int)std::tuple_size<std::tuple<std::pair<int, long> > >::value,
                (int)std::tuple_size<std::tuple<long, long> >::value);
    return 0;
}
