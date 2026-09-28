// Sorting and searching over arrays: insertion, selection, shell, merge,
// quick and heap sort, each checked against the others, on int and double
// data, with global arrays, heap arrays, bool flags and all three loops.
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
#define COUNT 300

int data[COUNT];
int work[COUNT];
int tmp[COUNT];
double real[COUNT];

int rnd = 12345;
int random(int m) { rnd = (rnd * 1103 + 12345) % 1000003; return rnd % m; }

void load() { for (int i = 0; i < COUNT; i++) { work[i] = data[i]; } }

bool sorted(int *a, int n) { for (int i = 1; i < n; i++) { if (a[i - 1] > a[i]) { return false; } } return true; }

int checksum(int *a, int n) { int s = 0; for (int i = 0; i < n; i++) { s = (s * 31 + a[i]) % 1000003; } return s; }

void insertion(int *a, int n) {
  for (int i = 1; i < n; i++) {
    int x = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > x) { a[j + 1] = a[j]; j--; }
    a[j + 1] = x;
  }
}

void selection(int *a, int n) {
  for (int i = 0; i < n - 1; i++) {
    int m = i;
    for (int j = i + 1; j < n; j++) { if (a[j] < a[m]) { m = j; } }
    int t = a[i]; a[i] = a[m]; a[m] = t;
  }
}

void shell(int *a, int n) {
  int gap = 1;
  while (gap < n / 3) { gap = gap * 3 + 1; }
  while (gap >= 1) {
    for (int i = gap; i < n; i++) {
      int x = a[i];
      int j = i;
      while (j >= gap && a[j - gap] > x) { a[j] = a[j - gap]; j = j - gap; }
      a[j] = x;
    }
    gap = gap / 3;
  }
}

void merge(int *a, int lo, int mid, int hi) {
  int i = lo;
  int j = mid;
  int k = lo;
  while (i < mid && j < hi) { if (a[i] <= a[j]) { tmp[k] = a[i]; i++; } else { tmp[k] = a[j]; j++; } k++; }
  while (i < mid) { tmp[k] = a[i]; i++; k++; }
  while (j < hi) { tmp[k] = a[j]; j++; k++; }
  for (k = lo; k < hi; k++) { a[k] = tmp[k]; }
}

void mergeSort(int *a, int lo, int hi) {
  if (hi - lo < 2) { return; }
  int mid = (lo + hi) / 2;
  mergeSort(a, lo, mid);
  mergeSort(a, mid, hi);
  merge(a, lo, mid, hi);
}

int partition(int *a, int lo, int hi) {
  int p = a[(lo + hi) / 2];
  int i = lo;
  int j = hi;
  do {
    while (a[i] < p) { i++; }
    while (a[j] > p) { j--; }
    if (i <= j) { int t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
  } while (i <= j);
  return i;
}

void quick(int *a, int lo, int hi) {
  if (lo >= hi) { return; }
  int i = partition(a, lo, hi);
  quick(a, lo, i - 1);
  quick(a, i, hi);
}

void sift(int *a, int start, int end) {
  int root = start;
  while (root * 2 + 1 <= end) {
    int child = root * 2 + 1;
    int sw = root;
    if (a[sw] < a[child]) { sw = child; }
    if (child + 1 <= end && a[sw] < a[child + 1]) { sw = child + 1; }
    if (sw == root) { return; }
    int t = a[root]; a[root] = a[sw]; a[sw] = t;
    root = sw;
  }
}

void heapSort(int *a, int n) {
  for (int s = (n - 2) / 2; s >= 0; s--) { sift(a, s, n - 1); }
  for (int e = n - 1; e > 0; e--) { int t = a[e]; a[e] = a[0]; a[0] = t; sift(a, 0, e - 1); }
}

void sortReal(double *a, int n) {
  for (int i = 1; i < n; i++) {
    double x = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > x) { a[j + 1] = a[j]; j--; }
    a[j + 1] = x;
  }
}

int binarySearch(int *a, int n, int key) {
  int lo = 0;
  int hi = n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (a[mid] == key) { return mid; }
    if (a[mid] < key) { lo = mid + 1; } else { hi = mid - 1; }
  }
  return -1;
}

int main() {
  for (int i = 0; i < COUNT; i++) { data[i] = random(10000); real[i] = random(1000) / 7.0; }
  int sums[6];
  bool ok = true;
  for (int k = 0; k < 6; k++) {
    load();
    switch (k) {
      case 0: insertion(work, COUNT); break;
      case 1: selection(work, COUNT); break;
      case 2: shell(work, COUNT); break;
      case 3: mergeSort(work, 0, COUNT); break;
      case 4: quick(work, 0, COUNT - 1); break;
      default: heapSort(work, COUNT); break;
    }
    if (!sorted(work, COUNT)) { ok = false; }
    sums[k] = checksum(work, COUNT);
  }
  for (int k = 1; k < 6; k++) { if (sums[k] != sums[0]) { ok = false; } }
  show("sorted "); show(ok); show(" sum "); show(sums[0]); print_line();
  int *heap = new int[COUNT];
  for (int i = 0; i < COUNT; i++) { heap[i] = work[i]; }
  int hits = 0;
  for (int i = 0; i < 1000; i++) { if (binarySearch(heap, COUNT, i * 10) >= 0) { hits++; } }
  delete[] heap;
  sortReal(real, COUNT);
  show("hits "); show(hits); show(" real "); show(real[0]); show(" "); show(real[COUNT / 2]); show(" "); show(real[COUNT - 1]); print_line();
  return 0;
}
