int main() { int i = 1; double *d = static_cast<double*>(&i); return (int)*d; }
