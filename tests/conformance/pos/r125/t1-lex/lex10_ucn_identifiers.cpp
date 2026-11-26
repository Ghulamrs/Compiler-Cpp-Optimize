// Universal character names in strings; long identifiers; identifiers with $ are extensions (not used); keywords as member names refused (not used).
#include <cstdio>
int a_very_long_identifier_name_that_goes_on_and_on_and_on_for_quite_a_while_indeed_1234567890 = 5;
int main() {
    const char *s = "\u00c9";
    std::printf("%d %d\n", (unsigned char)s[0], (unsigned char)s[1]);
    std::printf("%d\n", a_very_long_identifier_name_that_goes_on_and_on_and_on_for_quite_a_while_indeed_1234567890);
    return 0;
}
