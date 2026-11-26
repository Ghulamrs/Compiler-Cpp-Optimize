int main() { int i = 1; double *d = const_cast<double*>(&i); return (int)*d; }
