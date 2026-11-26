// [temp.dep.candidate]: a dependent call sees what was declared before the template and what ADL finds - for int, nothing.
template <class T> int f(T t) { return after(t); }
int after(int v) { return v; }
int main() { return f(1); }
