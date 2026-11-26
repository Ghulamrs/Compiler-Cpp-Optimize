// [cpp.stringize]: backslashes and quotes inside stringized string literals are escaped; leading/trailing white space dropped; internal runs become one space.
#include <cstdio>
#include <cstring>
#define S(x) #x
int main() {
    std::printf("%s\n", S("a\"b\\c"));
    std::printf("%s\n", S('\''));
    std::printf("[%s]\n", S(  x   +   y  ));
    std::printf("[%s]\n", S(/* c */ z /* d */));
    std::printf("[%s]\n", S(a
    b));
    return 0;
}
