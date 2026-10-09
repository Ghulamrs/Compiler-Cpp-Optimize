// A range-based `for` over a class with no `begin` and `end`, refused.
//
// [stmt.ranged]/1 looks the two up as members first, and where neither is
// found, as free `begin(r)` and `end(r)` by argument-dependent lookup. This
// class has neither, so the loop has nothing to walk - clang refuses it too:
// "invalid range expression of type 'Plain'; no viable 'begin' function
// available" at 16:16.

struct Plain { int x; };

int main(void) {
    Plain p;
    p.x = 1;
    for (int n : p) return n;
    return 0;
}
