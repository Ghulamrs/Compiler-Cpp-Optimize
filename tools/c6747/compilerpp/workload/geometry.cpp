// Value classes and the operators Compiler++ overloads: members and friends,
// references, const methods, by-value parameters and return by value.
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
#include <cmath>
double sqrt(double);  double sin(double);  double cos(double);  double fabs(double);

class Vec2 {
public:
  double x;
  double y;
  Vec2() { x = 0.0; y = 0.0; }
  Vec2(double a, double b) { x = a; y = b; }
  Vec2(const Vec2 &o) { x = o.x; y = o.y; }
  Vec2 &operator=(const Vec2 &o) { x = o.x; y = o.y; return *this; }
  Vec2 operator+(Vec2 o) { Vec2 r; r.x = x + o.x; r.y = y + o.y; return r; }
  Vec2 operator-(Vec2 o) { Vec2 r; r.x = x - o.x; r.y = y - o.y; return r; }
  Vec2 operator*(double k) { Vec2 r; r.x = x * k; r.y = y * k; return r; }
  Vec2 operator-() { Vec2 r; r.x = -x; r.y = -y; return r; }
  Vec2 &operator+=(Vec2 o) { x = x + o.x; y = y + o.y; return *this; }
  Vec2 &operator-=(Vec2 o) { x = x - o.x; y = y - o.y; return *this; }
  bool operator==(Vec2 o) { return x == o.x && y == o.y; }
  bool operator!=(Vec2 o) { return !(x == o.x && y == o.y); }
  double dot(Vec2 o) const { return x * o.x + y * o.y; }
  double cross(Vec2 o) const { return x * o.y - y * o.x; }
  double length() const { return sqrt(x * x + y * y); }
  friend Vec2 operator*(double k, Vec2 v);
  friend bool nearly(Vec2 a, Vec2 b);
};

Vec2 operator*(double k, Vec2 v) { Vec2 r; r.x = v.x * k; r.y = v.y * k; return r; }
bool nearly(Vec2 a, Vec2 b) { return fabs(a.x - b.x) < 0.000001 && fabs(a.y - b.y) < 0.000001; }

class Vec3 {
public:
  double v[3];
  Vec3() { v[0] = 0.0; v[1] = 0.0; v[2] = 0.0; }
  Vec3(double a, double b, double c) { v[0] = a; v[1] = b; v[2] = c; }
  double &operator[](int i) { return v[i]; }
  double get(int i) const { return v[i]; }
  Vec3 operator+(Vec3 o) { Vec3 r; for (int i = 0; i < 3; i++) { r.v[i] = v[i] + o.v[i]; } return r; }
  Vec3 operator-(Vec3 o) { Vec3 r; for (int i = 0; i < 3; i++) { r.v[i] = v[i] - o.v[i]; } return r; }
  Vec3 operator*(double k) { Vec3 r; for (int i = 0; i < 3; i++) { r.v[i] = v[i] * k; } return r; }
  double dot(Vec3 o) const { return v[0] * o.v[0] + v[1] * o.v[1] + v[2] * o.v[2]; }
  Vec3 cross(Vec3 o) const {
    Vec3 r;
    r.v[0] = v[1] * o.v[2] - v[2] * o.v[1];
    r.v[1] = v[2] * o.v[0] - v[0] * o.v[2];
    r.v[2] = v[0] * o.v[1] - v[1] * o.v[0];
    return r;
  }
  double norm() const { return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
  Vec3 unit() const {
    Vec3 r;
    double n = norm();
    if (n == 0.0) { return r; }
    for (int i = 0; i < 3; i++) { r.v[i] = v[i] / n; }
    return r;
  }
};

class Mat3 {
public:
  double m[3][3];
  Mat3() {
    for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) { m[i][j] = 0.0; } }
  }
  void identity() {
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        if (i == j) { m[i][j] = 1.0; } else { m[i][j] = 0.0; }
      }
    }
  }
  double operator()(int r, int c) { return m[r][c]; }
  void set(int r, int c, double value) { m[r][c] = value; }
  Mat3 operator*(Mat3 o) {
    Mat3 r;
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        double s = 0.0;
        for (int k = 0; k < 3; k++) { s = s + m[i][k] * o.m[k][j]; }
        r.m[i][j] = s;
      }
    }
    return r;
  }
  Vec3 apply(Vec3 v) {
    Vec3 r;
    for (int i = 0; i < 3; i++) { r[i] = m[i][0] * v.get(0) + m[i][1] * v.get(1) + m[i][2] * v.get(2); }
    return r;
  }
  double det() const {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
         - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
         + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  }
  Mat3 transpose() const {
    Mat3 r;
    for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) { r.m[j][i] = m[i][j]; } }
    return r;
  }
};

Mat3 rotationZ(double a) {
  Mat3 r;
  r.identity();
  r.set(0, 0, cos(a));
  r.set(0, 1, -sin(a));
  r.set(1, 0, sin(a));
  r.set(1, 1, cos(a));
  return r;
}

double polygonArea(Vec2 *pts, int n) {
  double s = 0.0;
  for (int i = 0; i < n; i++) {
    int j = i + 1;
    if (j == n) { j = 0; }
    s = s + pts[i].cross(pts[j]);
  }
  if (s < 0.0) { s = -s; }
  return s / 2.0;
}

int convexHullCount(Vec2 *pts, int n) {
  int count = 0;
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      if (i != j) {
        bool edge = true;
        Vec2 d = pts[j] - pts[i];
        for (int k = 0; k < n; k++) {
          if (k != i && k != j) {
            Vec2 e = pts[k] - pts[i];
            if (d.cross(e) < 0.0) { edge = false; }
          }
        }
        if (edge) { count++; }
      }
    }
  }
  return count;
}

int main() {
  Vec2 pts[8];
  for (int i = 0; i < 8; i++) {
    double a = i * 0.785398163;
    pts[i].x = cos(a) * 10.0;
    pts[i].y = sin(a) * 10.0;
  }
  show("area "); show(polygonArea(pts, 8)); print_line();
  show("hull "); show(convexHullCount(pts, 8)); print_line();
  Vec2 a(1.0, 2.0);
  Vec2 b(3.0, -4.0);
  Vec2 c = a + b * 2.0 - (-a);
  c += 0.5 * b;
  Vec2 one(1.0, 1.0);
  c -= one;
  Vec2 same(1.0, 2.0);
  show("vec2 "); show(c.x); show(" "); show(c.y); show(" "); show(a.dot(b)); show(" "); show(nearly(a, same)); print_line();
  Vec3 u(1.0, 0.0, 0.0);
  Vec3 w(0.0, 1.0, 0.0);
  Vec3 z = u.cross(w);
  Mat3 r1 = rotationZ(0.5);
  Mat3 r2 = rotationZ(0.25);
  Mat3 r = r1 * r2;
  Vec3 ru = r.apply(u);
  Mat3 rt = r.transpose();
  show("vec3 "); show(z.get(2)); show(" "); show(ru.norm()); show(" "); show(r.det()); show(" "); show(rt(0, 1)); print_line();
  return 0;
}
