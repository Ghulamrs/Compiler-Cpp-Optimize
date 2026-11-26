// [temp.explicit]/4: the declaration must match a template's specialization; twice takes one parameter.
template <class T> T twice(T t) { return t + t; }
template int twice<int>(int, int);
int main() { return twice(1); }
