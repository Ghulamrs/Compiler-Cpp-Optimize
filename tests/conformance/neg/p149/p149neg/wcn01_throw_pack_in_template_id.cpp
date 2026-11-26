// [temp.variadic]/5: `throw(Box<Ts>)` names the pack inside a template-id and never expands it.
template <class T> struct Box {};
template <class... Ts> void f() throw(Box<Ts>);
int main() { return 0; }
