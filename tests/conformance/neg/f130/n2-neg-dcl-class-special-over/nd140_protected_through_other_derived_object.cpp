class B { protected: int p; }; class D : public B { public: int f(B &b) { return b.p; } }; int main() { return 0; }
