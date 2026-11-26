// [conv.array] in C++11 removed the deprecated conversion of a string literal to char*.
int main() { char *p = "literal"; return p[0]; }
