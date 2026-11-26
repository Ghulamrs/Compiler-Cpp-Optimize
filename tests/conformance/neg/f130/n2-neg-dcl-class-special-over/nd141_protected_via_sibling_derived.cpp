class B { protected: int p; }; class D1 : public B {}; class D2 : public B { public: int f(D1 &d) { return d.p; } }; int main() { return 0; }
