// main without a return statement returns 0; exit codes; return of an expression with side effects; argc is 1, argv[0] non-null, argv[argc] null.
#include <cstdio>
int side = 0;
int bump() { return ++side; }
int main(int argc, char **argv) {
    std::printf("%d %d %d\n", argc, argv[0] != 0, argv[argc] == 0);
    bump();
    std::printf("%d\n", side);
    if (argc > 100) return 3;
}
