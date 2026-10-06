// A program whose second translation unit, data-only-translation-unit.part.cpp, defines no
// function. `extern "C" int counter;` below only declares - [dcl.link]/7 again - so the part's
// definition is the one object; and each file's own `hidden` is its own.
extern "C" int printf(const char *, ...);
extern "C" const char version[];
extern "C" const int primes[5];
extern "C" int counter;
extern double scale;
extern const char *names[2];
extern "C" { const int hidden = 1; }
extern const int *partHidden;
const int *mainHidden = &hidden;

int main()
{
    counter += 2;
    printf("%s %d %d %d %g %s%s %d %d\n", version, primes[0], primes[4], counter, scale, names[0], names[1],
           *mainHidden, *partHidden);
    return 0;
}
