// `-> auto` asks for the return type to be deduced, which is C++14 - in C++11 the
// type after the arrow has to be written out. clang -std=c++11: "'auto' not
// allowed in function return type" at 1:20 of the one-line form. The C++11 form
// beside it is trailing-return-type.cpp.
auto f(int a) -> auto { return a; }

int main(void) { return f(0); }
