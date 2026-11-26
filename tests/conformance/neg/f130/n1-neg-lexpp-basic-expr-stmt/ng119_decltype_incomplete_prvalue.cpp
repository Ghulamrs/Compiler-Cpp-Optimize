struct S; S make(); int main() { decltype(make()) *p = nullptr; return sizeof(decltype(make())); }
