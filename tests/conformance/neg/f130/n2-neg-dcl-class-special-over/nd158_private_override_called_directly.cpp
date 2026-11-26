class B { public: virtual void f(); }; class D : public B { void f() override; }; int main() { D d; d.f(); return 0; }
