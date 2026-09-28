// A recursive-descent evaluator for arithmetic over named variables: a
// tokenizer driven by switch over characters, mutual recursion, do-while,
// char arithmetic, and a symbol table held in parallel arrays.
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

char src[256];
int pos = 0;
int tok = 0;
int tokVal = 0;
char tokName[16];
char names[32][16];
int values[32];
int nvars = 0;
int errors = 0;

void copyText(char *to, char *from) { int i = 0; do { to[i] = from[i]; i++; } while (from[i - 1] != 0); }

bool same(char *a, char *b) {
  int i = 0;
  while (a[i] != 0 && a[i] == b[i]) { i++; }
  return a[i] == b[i];
}

int lookup(char *name) {
  for (int i = 0; i < nvars; i++) { if (same(names[i], name)) { return i; } }
  copyText(names[nvars], name);
  values[nvars] = 0;
  nvars++;
  return nvars - 1;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

void next() {
  while (src[pos] == ' ') { pos++; }
  char c = src[pos];
  if (c == 0) { tok = 0; return; }
  if (isDigit(c)) {
    tokVal = 0;
    while (isDigit(src[pos])) { tokVal = tokVal * 10 + (src[pos] - '0'); pos++; }
    tok = 'n';
    return;
  }
  if (isAlpha(c)) {
    int n = 0;
    while (isAlpha(src[pos]) || isDigit(src[pos])) { if (n < 15) { tokName[n] = src[pos]; n++; } pos++; }
    tokName[n] = 0;
    tok = 'v';
    return;
  }
  pos++;
  switch (c) {
    case '+': case '-': case '*': case '/': case '%':
    case '(': case ')': case '=': case ';':
      tok = c;
      break;
    case '<':
      if (src[pos] == '=') { pos++; tok = 'l'; } else { tok = '<'; }
      break;
    case '>':
      if (src[pos] == '=') { pos++; tok = 'g'; } else { tok = '>'; }
      break;
    default:
      errors++;
      tok = '?';
      break;
  }
}

int expression();

int primary() {
  if (tok == 'n') { int v = tokVal; next(); return v; }
  if (tok == 'v') { int i = lookup(tokName); next(); return values[i]; }
  if (tok == '(') {
    next();
    int v = expression();
    if (tok == ')') { next(); } else { errors++; }
    return v;
  }
  if (tok == '-') { next(); return -primary(); }
  errors++;
  next();
  return 0;
}

int term() {
  int v = primary();
  while (tok == '*' || tok == '/' || tok == '%') {
    int op = tok;
    next();
    int r = primary();
    if (op == '*') { v = v * r; }
    else if (r == 0) { errors++; }
    else if (op == '/') { v = v / r; }
    else { v = v % r; }
  }
  return v;
}

int sum() {
  int v = term();
  while (tok == '+' || tok == '-') {
    int op = tok;
    next();
    int r = term();
    if (op == '+') { v = v + r; } else { v = v - r; }
  }
  return v;
}

int expression() {
  int v = sum();
  while (tok == '<' || tok == '>' || tok == 'l' || tok == 'g') {
    int op = tok;
    next();
    int r = sum();
    switch (op) {
      case '<': v = v < r; break;
      case '>': v = v > r; break;
      case 'l': v = v <= r; break;
      default: v = v >= r; break;
    }
  }
  return v;
}

int statement() {
  if (tok == 'v') {
    int save = pos;
    char name[16];
    copyText(name, tokName);
    next();
    if (tok == '=') {
      next();
      int v = expression();
      values[lookup(name)] = v;
      return v;
    }
    pos = save;
    copyText(tokName, name);
    tok = 'v';
  }
  return expression();
}

int run(char *program) {
  copyText(src, program);
  pos = 0;
  next();
  int last = 0;
  while (tok != 0) {
    last = statement();
    if (tok == ';') { next(); }
    else if (tok != 0) { errors++; next(); }
  }
  return last;
}

int main() {
  int r = run("a = 7; b = a * 6 - (a + 3) / 2; c = b % 5 + -a; a < b; b >= c");
  show("r "); show(r); show(" a "); show(values[lookup("a")]); show(" b "); show(values[lookup("b")]); show(" c "); show(values[lookup("c")]); print_line();
  int total = 0;
  for (int i = 0; i < 20; i++) {
    values[lookup("i")] = i;
    total = total + run("x = i * i - 3 * i + 2; y = (x + i) % 7; x + y * 2");
  }
  show("total "); show(total); show(" vars "); show(nvars); show(" errors "); show(errors); print_line();
  run("q = 5 $ 3");
  show("errors "); show(errors); print_line();
  return 0;
}
