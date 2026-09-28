// A class hierarchy three levels deep: virtual functions and a virtual
// destructor, protected members, base-class initialisers, upcasts through
// pointers and references, and objects made and destroyed with new and delete.
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
double sqrt(double);
#define PI 3.14159265358979

class Shape {
protected:
  int id;
  double ox;
  double oy;
public:
  Shape(int i, double x, double y) { id = i; ox = x; oy = y; }
  virtual ~Shape() { }
  virtual double area() const { return 0.0; }
  virtual double perimeter() const { return 0.0; }
  virtual int sides() const { return 0; }
  virtual void scale(double k) { scaleOrigin(k); }
  void scaleOrigin(double k) { ox = ox * k; oy = oy * k; }
  int ident() const { return id; }
  double distance() const { return sqrt(ox * ox + oy * oy); }
  void moveBy(double dx, double dy) { ox = ox + dx; oy = oy + dy; }
};

class Circle : public Shape {
protected:
  double r;
public:
  Circle(int i, double x, double y, double radius) : Shape(i, x, y) { r = radius; }
  ~Circle() { }
  double area() const { return discArea(); }
  double perimeter() const { return rim(); }
  void scale(double k) { scaleDisc(k); }
  double discArea() const { return PI * r * r; }
  double rim() const { return 2.0 * PI * r; }
  void scaleDisc(double k) { scaleOrigin(k); r = r * k; }
};

class Ring : public Circle {
  double inner;
public:
  Ring(int i, double x, double y, double outer, double in) : Circle(i, x, y, outer) { inner = in; }
  double area() const { return discArea() - PI * inner * inner; }
  double perimeter() const { return rim() + 2.0 * PI * inner; }
  void scale(double k) { scaleDisc(k); inner = inner * k; }
};

class Rect : public Shape {
protected:
  double w;
  double h;
public:
  Rect(int i, double x, double y, double width, double height) : Shape(i, x, y) { w = width; h = height; }
  double area() const { return w * h; }
  double perimeter() const { return 2.0 * (w + h); }
  int sides() const { return 4; }
  void scale(double k) { scaleOrigin(k); w = w * k; h = h * k; }
  bool square() const { return w == h; }
};

class Square : public Rect {
public:
  Square(int i, double x, double y, double s) : Rect(i, x, y, s, s) { }
  double perimeter() const { return 4.0 * w; }
};

class Triangle : public Shape {
  double a;
  double b;
  double c;
public:
  Triangle(int i, double x, double y, double p, double q, double s) : Shape(i, x, y) { a = p; b = q; c = s; }
  double area() const {
    double s = (a + b + c) / 2.0;
    return sqrt(s * (s - a) * (s - b) * (s - c));
  }
  double perimeter() const { return a + b + c; }
  int sides() const { return 3; }
  void scale(double k) { scaleOrigin(k); a = a * k; b = b * k; c = c * k; }
};

double totalArea(Shape **all, int n) {
  double s = 0.0;
  for (int i = 0; i < n; i++) { s = s + all[i]->area(); }
  return s;
}

int countSides(Shape **all, int n) {
  int s = 0;
  for (int i = 0; i < n; i++) { s = s + all[i]->sides(); }
  return s;
}

void grow(Shape &s, double k) { s.scale(k); }

Shape *largest(Shape **all, int n) {
  Shape *best = all[0];
  for (int i = 1; i < n; i++) {
    if (all[i]->area() > best->area()) { best = all[i]; }
  }
  return best;
}

void sortByArea(Shape **all, int n) {
  for (int i = 1; i < n; i++) {
    Shape *x = all[i];
    int j = i - 1;
    while (j >= 0 && all[j]->area() > x->area()) {
      all[j + 1] = all[j];
      j--;
    }
    all[j + 1] = x;
  }
}

int main() {
  Shape **all = new Shape*[12];
  for (int i = 0; i < 12; i++) {
    double k = i + 1;
    switch (i % 6) {
      case 0: all[i] = new Circle(i, k, k, k); break;
      case 1: all[i] = new Ring(i, k, -k, k + 2.0, k); break;
      case 2: all[i] = new Rect(i, -k, k, k, k * 2.0); break;
      case 3: all[i] = new Square(i, 0.0, k, k); break;
      case 4: all[i] = new Triangle(i, k, 0.0, 3.0 * k, 4.0 * k, 5.0 * k); break;
      default: all[i] = new Shape(i, 1.0, 1.0); break;
    }
  }
  show("area "); show(totalArea(all, 12)); show(" sides "); show(countSides(all, 12)); print_line();
  for (int i = 0; i < 12; i++) { grow(*all[i], 1.5); all[i]->moveBy(1.0, -1.0); }
  sortByArea(all, 12);
  Shape *big = largest(all, 12);
  show("largest "); show(big->ident()); show(" "); show(big->area()); show(" "); show(big->distance()); print_line();
  for (int i = 0; i < 12; i++) { show(all[i]->ident()); show(" "); }
  print_line();
  Rect r(99, 0.0, 0.0, 2.0, 2.0);
  Shape &sr = r;
  show("square "); show(r.square()); show(" "); show(sr.perimeter()); print_line();
  for (int i = 0; i < 12; i++) { delete all[i]; }
  delete[] all;
  return 0;
}
