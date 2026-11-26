// Comments: // inside /* */, /* inside //, line splice ending a // comment, splice in the middle of a token, splice before a comment.
#include <cstdio>
int ma\
in() {
    int x = 1; /* // not a line comment */ x += 1;
    // this comment continues \
    x += 100;
    int y = 2; // /* not a block
    x += y;
    std::printf("%d\n", x);
    std::printf("%d\n", 3 /
    1);
    return 0;
}
