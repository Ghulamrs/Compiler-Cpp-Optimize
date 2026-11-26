struct S { ~S() = delete; };
struct T { S s; };
int main() { T t; return 0; }
