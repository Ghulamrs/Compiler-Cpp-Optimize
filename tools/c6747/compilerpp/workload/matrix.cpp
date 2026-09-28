// Numerics over two-dimensional arrays of double: fill, multiply, LU
// decomposition with partial pivoting, a solve, a determinant, an inverse,
// and a power iteration for the largest eigenvalue.
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
double fabs(double);  double sqrt(double);
#define N 8

double A[N][N];
double B[N][N];
double LU[N][N];
double INV[N][N];
int perm[N];

void fill(int seed) {
  int s = seed;
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      s = (s * 1103 + 12345) % 65536;
      A[i][j] = (s % 200) / 10.0 - 10.0;
      if (i == j) { A[i][j] = A[i][j] + 25.0; }
    }
  }
}

void multiply() {
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      double s = 0.0;
      for (int k = 0; k < N; k++) { s = s + A[i][k] * A[k][j]; }
      B[i][j] = s;
    }
  }
}

bool decompose() {
  for (int i = 0; i < N; i++) {
    perm[i] = i;
    for (int j = 0; j < N; j++) { LU[i][j] = A[i][j]; }
  }
  for (int k = 0; k < N; k++) {
    int p = k;
    double best = fabs(LU[k][k]);
    for (int i = k + 1; i < N; i++) {
      if (fabs(LU[i][k]) > best) { best = fabs(LU[i][k]); p = i; }
    }
    if (best < 0.000000001) { return false; }
    if (p != k) {
      for (int j = 0; j < N; j++) { double t = LU[k][j]; LU[k][j] = LU[p][j]; LU[p][j] = t; }
      int t2 = perm[k]; perm[k] = perm[p]; perm[p] = t2;
    }
    for (int i = k + 1; i < N; i++) {
      LU[i][k] = LU[i][k] / LU[k][k];
      for (int j = k + 1; j < N; j++) { LU[i][j] = LU[i][j] - LU[i][k] * LU[k][j]; }
    }
  }
  return true;
}

void solve(double *b, double *x) {
  double y[N];
  for (int i = 0; i < N; i++) {
    double s = b[perm[i]];
    for (int j = 0; j < i; j++) { s = s - LU[i][j] * y[j]; }
    y[i] = s;
  }
  for (int i = N - 1; i >= 0; i--) {
    double s = y[i];
    for (int j = i + 1; j < N; j++) { s = s - LU[i][j] * x[j]; }
    x[i] = s / LU[i][i];
  }
}

double determinant() {
  double d = 1.0;
  int swaps = 0;
  for (int i = 0; i < N; i++) {
    d = d * LU[i][i];
    if (perm[i] != i) { swaps++; }
  }
  return d;
}

void invert() {
  double e[N];
  double x[N];
  for (int c = 0; c < N; c++) {
    for (int i = 0; i < N; i++) { if (i == c) { e[i] = 1.0; } else { e[i] = 0.0; } }
    solve(e, x);
    for (int i = 0; i < N; i++) { INV[i][c] = x[i]; }
  }
}

double checkInverse() {
  double worst = 0.0;
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      double s = 0.0;
      for (int k = 0; k < N; k++) { s = s + A[i][k] * INV[k][j]; }
      if (i == j) { s = s - 1.0; }
      if (fabs(s) > worst) { worst = fabs(s); }
    }
  }
  return worst;
}

double powerIteration(int steps) {
  double v[N];
  double w[N];
  for (int i = 0; i < N; i++) { v[i] = 1.0; }
  double lambda = 0.0;
  for (int s = 0; s < steps; s++) {
    for (int i = 0; i < N; i++) {
      double t = 0.0;
      for (int j = 0; j < N; j++) { t = t + A[i][j] * v[j]; }
      w[i] = t;
    }
    double nrm = 0.0;
    for (int i = 0; i < N; i++) { nrm = nrm + w[i] * w[i]; }
    nrm = sqrt(nrm);
    for (int i = 0; i < N; i++) { v[i] = w[i] / nrm; }
    lambda = nrm;
  }
  return lambda;
}

int main() {
  double total = 0.0;
  for (int seed = 1; seed <= 3; seed++) {
    fill(seed);
    multiply();
    if (!decompose()) { show("singular"); print_line(); return 1; }
    double b[N];
    double x[N];
    for (int i = 0; i < N; i++) { b[i] = i + 1.0; }
    solve(b, x);
    invert();
    double err = checkInverse();
    double det = determinant();
    double lam = powerIteration(40);
    total = total + x[0] + B[1][2] / 100.0;
    show("seed "); show(seed); show(" x0 "); show(x[0]); show(" det "); show(det); show(" eig "); show(lam); show(" ok "); show((err < 0.000001)); print_line();
  }
  show("total "); show(total); print_line();
  return 0;
}
