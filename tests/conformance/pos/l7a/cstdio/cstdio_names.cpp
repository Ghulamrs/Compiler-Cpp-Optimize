#include <cstdio>
#include <cstring>
int main() {
    std::FILE *f = std::tmpfile();
    static char buf[BUFSIZ];
    std::printf("%d %d\n", std::setvbuf(f, buf, _IOFBF, sizeof buf), BUFSIZ >= 256);
    std::fputs("alpha beta 99", f);
    std::rewind(f);
    char word[16];
    std::fpos_t mark;
    std::fscanf(f, "%15s", word);
    std::fgetpos(f, &mark);
    std::fscanf(f, "%15s", word);
    std::fsetpos(f, &mark);
    char again[16];
    int c = 0;
    int read = std::fscanf(f, "%15s %d", again, &c);
    std::printf("%s %d %s %d\n", word, read, again, c);
    std::fclose(f);
    std::FILE *g = std::tmpfile();
    std::setbuf(g, 0);
    std::fputs("x", g);
    std::printf("%ld\n", std::ftell(g));
    std::fclose(g);
    char name[L_tmpnam];
    std::printf("%d %d %d %d\n", std::tmpnam(name) != 0, (int)sizeof(std::fpos_t), _IONBF != _IOFBF && _IOLBF != _IOFBF, FOPEN_MAX >= 8 && FILENAME_MAX > 0 && TMP_MAX >= 25);
    return 0;
}
