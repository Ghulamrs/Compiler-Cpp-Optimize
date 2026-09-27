// A 32 x 32 double matrix inverted by Gauss-Jordan elimination with partial pivoting,
// written in classes, then multiplied back to check that A times its inverse is I.
#include <iostream>
double fabs(double);

class Matrix {
public:
    int n;
    double* a;
    Matrix(int size) : n(size) {
        a = new double[size * size];
        for (int k = 0; k < size * size; k = k + 1) a[k] = 0.0;
    }
    Matrix(const Matrix& o) : n(o.n) {
        a = new double[o.n * o.n];
        for (int k = 0; k < o.n * o.n; k = k + 1) a[k] = o.a[k];
    }
    ~Matrix() { delete[] a; }
    double get(int i, int j) { return a[i * n + j]; }
    void set(int i, int j, double v) { a[i * n + j] = v; }
    void identity() {
        for (int i = 0; i < n; i = i + 1) {
            for (int j = 0; j < n; j = j + 1) set(i, j, 0.0);
            set(i, i, 1.0);
        }
    }
    void swapRows(int r, int s) {
        for (int j = 0; j < n; j = j + 1) {
            double t = get(r, j);
            set(r, j, get(s, j));
            set(s, j, t);
        }
    }
    void scaleRow(int r, double f) {
        for (int j = 0; j < n; j = j + 1) set(r, j, get(r, j) * f);
    }
    void addRow(int dst, int src, double f) {
        for (int j = 0; j < n; j = j + 1) set(dst, j, get(dst, j) + f * get(src, j));
    }
    void multiply(Matrix& b, Matrix& out) {
        for (int i = 0; i < n; i = i + 1) {
            for (int j = 0; j < n; j = j + 1) {
                double s = 0.0;
                for (int k = 0; k < n; k = k + 1) s = s + get(i, k) * b.get(k, j);
                out.set(i, j, s);
            }
        }
    }
};

class Inverter {
public:
    Matrix work;
    Matrix inv;
    bool singular;
    Inverter(Matrix& m) : work(m), inv(m.n), singular(false) { inv.identity(); }
    int pivotRow(int col) {
        int best = col;
        double big = fabs(work.get(col, col));
        for (int r = col + 1; r < work.n; r = r + 1) {
            double v = fabs(work.get(r, col));
            if (v > big) { big = v; best = r; }
        }
        return best;
    }
    void run() {
        for (int col = 0; col < work.n; col = col + 1) {
            int p = pivotRow(col);
            if (fabs(work.get(p, col)) < 0.000000000001) { singular = true; return; }
            if (p != col) { work.swapRows(p, col); inv.swapRows(p, col); }
            double d = 1.0 / work.get(col, col);
            work.scaleRow(col, d);
            inv.scaleRow(col, d);
            for (int r = 0; r < work.n; r = r + 1) {
                if (r != col) {
                    double f = -work.get(r, col);
                    work.addRow(r, col, f);
                    inv.addRow(r, col, f);
                }
            }
        }
    }
};

int main() {
    int n = 32;
    Matrix A(n);
    int seed = 12345;
    for (int i = 0; i < n; i = i + 1) {
        for (int j = 0; j < n; j = j + 1) {
            seed = (seed * 1103 + 12345) % 65536;
            A.set(i, j, (seed % 1000) / 1000.0 - 0.5);
        }
        A.set(i, i, A.get(i, i) + n);
    }
    Inverter g(A);
    g.run();
    if (g.singular) { cout << "singular" << endl; return 1; }
    Matrix P(n);
    A.multiply(g.inv, P);
    double worst = 0.0;
    double trace = 0.0;
    for (int i = 0; i < n; i = i + 1) {
        trace = trace + g.inv.get(i, i);
        for (int j = 0; j < n; j = j + 1) {
            double want = 0.0;
            if (i == j) want = 1.0;
            double e = fabs(P.get(i, j) - want);
            if (e > worst) worst = e;
        }
    }
    cout << "inverse trace " << trace << endl;
    if (worst < 0.000000001) cout << "A * inverse is I" << endl;
    else cout << "residual too large " << worst << endl;
    return 0;
}
