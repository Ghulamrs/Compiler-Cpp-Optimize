#include <iostream>
#include <sstream>
#include <string>
int main() { std::istringstream in("42abc -7 3.5e2 0x10 true"); int a; std::string s; int b; double d; int h; std::string t; in >> a >> s >> b >> d >> h >> t; std::cout << a << ' ' << s << ' ' << b << ' ' << d << ' ' << h << ' ' << t << ' ' << in.eof() << ' ' << in.fail() << '\n'; std::istringstream in2("999999999999"); int big; in2 >> big; std::cout << big << ' ' << in2.fail() << '\n'; std::istringstream in3(""); int e = 5; in3 >> e; std::cout << e << ' ' << in3.fail() << ' ' << in3.eof() << '\n'; return 0; }
