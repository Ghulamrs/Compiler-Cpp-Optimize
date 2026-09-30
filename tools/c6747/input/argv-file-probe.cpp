/* iop.c - does the C6747 simulator give a program argv and a host file?
   argv[1] names a file; "-" reads nothing. Prints argc, each argv, the byte
   count and a checksum of the file read whole by fread, then again by fgetc. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    int i;
    printf("argc %d\n", argc);
    for (i = 0; i < argc; i++) printf("argv[%d] %s\n", i, argv[i]);
    if (argc < 2 || strcmp(argv[1], "-") == 0) { printf("no file\n"); return 0; }
    {
        FILE *f = (argc > 2 && argv[2][0] == '2') ? 0 : fopen(argv[1], "rb");
        static char buf[65536];
        size_t n, total = 0;
        unsigned h = 2166136261u;
        int c, count = 0;
        if (f) while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
            size_t k;
            for (k = 0; k < n; k++) h = (h ^ (unsigned char)buf[k]) * 16777619u;
            total += n;
        }
        if (f) fclose(f);
        if (f) printf("fread bytes %u fnv %u\n", (unsigned)total, h);
        if (argc > 2 && argv[2][0] == '1') return 0;
        f = fopen(argv[1], "r");
        if (!f) { printf("fopen 2 failed\n"); return 1; }
        h = 2166136261u;
        while ((c = fgetc(f)) != EOF) { h = (h ^ (unsigned char)c) * 16777619u; count++; }
        fclose(f);
        printf("fgetc bytes %d fnv %u\n", count, h);
    }
    return 0;
}
