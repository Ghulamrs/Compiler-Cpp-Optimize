// [temp.explicit]/3: an explicit instantiation names a template's specialization; f is no template.
int f(int v) { return v; }
template int f(int);
int main() { return f(0); }
