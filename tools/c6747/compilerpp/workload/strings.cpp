// A string class that owns its buffer: constructors of every kind, a copy
// constructor and assignment that deep-copy, a destructor that frees, new[]
// and delete[], operators that compare, index and append, and a stream
// operator of its own.
// Output through Compiler++'s natives rather than its <iostream>, which is compiled into
// every program that includes it and was 40% of this file's compile time.
void print_int(int n); void print_double(double d); void print_string(char *s);
void print_char(char c); void print_line();
void show(int n) { print_int(n); }
void show(double d) { print_double(d); }
void show(char *s) { print_string(s); }
void show(char c) { print_char(c); }
void show(bool b) { if (b) { print_int(1); } else { print_int(0); } }
void show(long n) { print_int((int)n); }
void show(unsigned int n) { print_int((int)n); }
void show(unsigned short n) { print_int((int)n); }
void show(float f) { print_double(f); }

int length(char *s) { int n = 0; while (s[n] != 0) { n++; } return n; }

class Str {
  char *buf;
  int len;
  int cap;
  void grow(int need) {
    if (need < cap) { return; }
    int c = cap * 2 + 8;
    while (c <= need) { c = c * 2; }
    char *nb = new char[c];
    for (int i = 0; i < len; i++) { nb[i] = buf[i]; }
    nb[len] = 0;
    delete[] buf;
    buf = nb;
    cap = c;
  }
public:
  Str() { cap = 8; len = 0; buf = new char[cap]; buf[0] = 0; }
  Str(char *s) {
    len = length(s);
    cap = len + 1;
    buf = new char[cap];
    for (int i = 0; i <= len; i++) { buf[i] = s[i]; }
  }
  Str(char c, int n) {
    len = n;
    cap = n + 1;
    buf = new char[cap];
    for (int i = 0; i < n; i++) { buf[i] = c; }
    buf[n] = 0;
  }
  Str(const Str &o) {
    len = o.len;
    cap = o.len + 1;
    buf = new char[cap];
    for (int i = 0; i <= len; i++) { buf[i] = o.buf[i]; }
  }
  ~Str() { delete[] buf; }
  Str &operator=(const Str &o) {
    if (this == &o) { return *this; }
    delete[] buf;
    len = o.len;
    cap = o.len + 1;
    buf = new char[cap];
    for (int i = 0; i <= len; i++) { buf[i] = o.buf[i]; }
    return *this;
  }
  Str &operator+=(Str o) {
    grow(len + o.len + 1);
    for (int i = 0; i <= o.len; i++) { buf[len + i] = o.buf[i]; }
    len = len + o.len;
    return *this;
  }
  Str &append(char c) {
    grow(len + 2);
    buf[len] = c;
    len++;
    buf[len] = 0;
    return *this;
  }
  Str operator+(Str o) { Str r(*this); r += o; return r; }
  char &operator[](int i) { return buf[i]; }
  char at(int i) const { return buf[i]; }
  int size() const { return len; }
  void set(char *s) { Str t(s); *this = t; }
  char *text() { return buf; }
  bool operator==(Str o) {
    if (len != o.len) { return false; }
    for (int i = 0; i < len; i++) { if (buf[i] != o.buf[i]) { return false; } }
    return true;
  }
  bool operator<(Str o) {
    int i = 0;
    while (i < len && i < o.len) {
      if (buf[i] != o.buf[i]) { return buf[i] < o.buf[i]; }
      i++;
    }
    return len < o.len;
  }
  int find(char c) const { for (int i = 0; i < len; i++) { if (buf[i] == c) { return i; } } return -1; }
  Str upper() const {
    Str r(*this);
    for (int i = 0; i < len; i++) {
      char c = r.buf[i];
      if (c >= 'a' && c <= 'z') { r.buf[i] = (char)(c - 'a' + 'A'); }
    }
    return r;
  }
  Str reversed() const {
    Str r(*this);
    for (int i = 0; i < len; i++) { r.buf[i] = buf[len - 1 - i]; }
    return r;
  }
  Str slice(int from, int count) const {
    Str r;
    for (int i = from; i < from + count && i < len; i++) { r.append(buf[i]); }
    return r;
  }
  int words() const {
    int n = 0;
    bool in = false;
    for (int i = 0; i < len; i++) {
      if (buf[i] == ' ') { in = false; }
      else { if (!in) { n++; } in = true; }
    }
    return n;
  }
  friend void show(Str s);
};

void show(Str s) { print_string(s.buf); }

Str number(int n) {
  Str r;
  if (n == 0) { r.append('0'); return r; }
  bool neg = n < 0;
  if (neg) { n = -n; }
  Str digits;
  while (n > 0) { digits.append((char)('0' + n % 10)); n = n / 10; }
  if (neg) { r.append('-'); }
  Str back = digits.reversed();
  r += back;
  return r;
}

void sortStrs(Str *a, int n) {
  for (int i = 1; i < n; i++) {
    Str x = a[i];
    int j = i - 1;
    while (j >= 0 && x < a[j]) { a[j + 1] = a[j]; j--; }
    a[j + 1] = x;
  }
}

int main() {
  Str hello("hello");
  Str space(' ', 1);
  Str world("world");
  Str line = hello + space + world;
  line += space;
  Str n = number(-12345);
  line += n;
  show(line); show(" ["); show(line.size()); show("] words "); show(line.words()); print_line();
  Str up = line.upper();
  show(up); show(" "); show(up.find('W')); show(" "); show(line.slice(6, 5)); print_line();
  Str names[6];
  names[0].set("delta");
  names[1].set("alpha");
  names[2].set("echo");
  names[3].set("charlie");
  names[4].set("bravo");
  names[5].set("alpha");
  sortStrs(names, 6);
  for (int i = 0; i < 6; i++) { show(names[i]); show(" "); }
  print_line();
  show("equal "); show((names[0] == names[1])); show(" less "); show((names[1] < names[2])); print_line();
  Str big;
  for (int i = 0; i < 40; i++) { Str k = number(i * 7); big += k; big.append(','); }
  show(big.size()); show(" "); show(big.at(10)); print_line();
  return 0;
}
