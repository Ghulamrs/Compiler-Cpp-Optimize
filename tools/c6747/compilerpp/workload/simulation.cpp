// A particle system: floating-point chains in float and double, member objects
// inside members, arrays of objects, objects returned by value and copied,
// const methods, and an integrator stepped many times.
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
double sqrt(double);  double sin(double);  double cos(double);

class Vec {
public:
  double x;
  double y;
  Vec() { x = 0.0; y = 0.0; }
  Vec(double a, double b) { x = a; y = b; }
  Vec operator+(Vec o) { Vec r(x + o.x, y + o.y); return r; }
  Vec operator-(Vec o) { Vec r(x - o.x, y - o.y); return r; }
  Vec operator*(double k) { Vec r(x * k, y * k); return r; }
  double len() const { return sqrt(x * x + y * y); }
};

class Body {
public:
  Vec pos;
  Vec vel;
  Vec force;
  double mass;
  float charge;
  Body() { mass = 1.0; charge = 0.0f; }
  void reset() { Vec z; force = z; }
  void push(Vec f) { force = force + f; }
  void step(double dt) {
    Vec acc = force * (1.0 / mass);
    vel = vel + acc * dt;
    pos = pos + vel * dt;
  }
  double energy() const { return 0.5 * mass * (vel.x * vel.x + vel.y * vel.y); }
};

class Box {
public:
  Vec low;
  Vec high;
  int bounces;
  Box() { bounces = 0; }
  void contain(Body &b) {
    if (b.pos.x < low.x) { b.pos.x = low.x; b.vel.x = -b.vel.x * 0.9; bounces++; }
    if (b.pos.x > high.x) { b.pos.x = high.x; b.vel.x = -b.vel.x * 0.9; bounces++; }
    if (b.pos.y < low.y) { b.pos.y = low.y; b.vel.y = -b.vel.y * 0.9; bounces++; }
    if (b.pos.y > high.y) { b.pos.y = high.y; b.vel.y = -b.vel.y * 0.9; bounces++; }
  }
};

class World {
public:
  Body bodies[16];
  Box box;
  int n;
  double time;
  World() {
    n = 16;
    time = 0.0;
    Vec lo(-50.0, -50.0);
    Vec hi(50.0, 50.0);
    box.low = lo;
    box.high = hi;
    for (int i = 0; i < n; i++) {
      double a = i * 0.3926990816;
      Vec p(cos(a) * 20.0, sin(a) * 20.0);
      Vec v(-sin(a) * 3.0, cos(a) * 3.0);
      bodies[i].pos = p;
      bodies[i].vel = v;
      bodies[i].mass = 1.0 + i % 4;
      bodies[i].charge = (i % 2) * 2.0f - 1.0f;
    }
  }
  void forces() {
    for (int i = 0; i < n; i++) { bodies[i].reset(); }
    for (int i = 0; i < n; i++) {
      for (int j = i + 1; j < n; j++) {
        Vec d = bodies[j].pos - bodies[i].pos;
        double r = d.len() + 0.5;
        double k = 5.0 * bodies[i].mass * bodies[j].mass / (r * r * r);
        float q = bodies[i].charge * bodies[j].charge;
        k = k - q * 2.0 / (r * r * r);
        Vec f = d * k;
        bodies[i].push(f);
        Vec g = f * -1.0;
        bodies[j].push(g);
      }
      Vec gravity(0.0, -0.3 * bodies[i].mass);
      bodies[i].push(gravity);
    }
  }
  void step(double dt) {
    forces();
    for (int i = 0; i < n; i++) { bodies[i].step(dt); box.contain(bodies[i]); }
    time = time + dt;
  }
  double energy() const { double e = 0.0; for (int i = 0; i < n; i++) { e = e + bodies[i].energy(); } return e; }
  Vec centre() const {
    Vec c;
    double m = 0.0;
    for (int i = 0; i < n; i++) {
      c.x = c.x + bodies[i].pos.x * bodies[i].mass;
      c.y = c.y + bodies[i].pos.y * bodies[i].mass;
      m = m + bodies[i].mass;
    }
    Vec r(c.x / m, c.y / m);
    return r;
  }
};

int main() {
  World w;
  double e0 = w.energy();
  for (int s = 0; s < 40; s++) { w.step(0.05); }
  Vec c = w.centre();
  World copy = w;
  copy.step(0.05);
  show("e0 "); show(e0); show(" e "); show(w.energy()); show(" t "); show(w.time); print_line();
  show("centre "); show(c.x); show(" "); show(c.y); show(" bounces "); show(w.box.bounces); show(" copy "); show(copy.time); print_line();
  return 0;
}
