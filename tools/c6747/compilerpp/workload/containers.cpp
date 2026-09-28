// Linked structures on the heap: a doubly linked list, a stack and a queue on
// top of it, and a binary search tree with recursive insert, search, traversal,
// height and removal - pointers to pointers, recursion, new and delete.
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

class Node {
public:
  int value;
  Node *prev;
  Node *next;
  Node(int v) { value = v; prev = 0; next = 0; }
};

class List {
protected:
  Node *head;
  Node *tail;
  int n;
public:
  List() { head = 0; tail = 0; n = 0; }
  ~List() { clear(); }
  void clear() { while (head != 0) { Node *d = head; head = head->next; delete d; } tail = 0; n = 0; }
  void pushBack(int v) {
    Node *x = new Node(v);
    x->prev = tail;
    if (tail != 0) { tail->next = x; } else { head = x; }
    tail = x;
    n++;
  }
  void pushFront(int v) {
    Node *x = new Node(v);
    x->next = head;
    if (head != 0) { head->prev = x; } else { tail = x; }
    head = x;
    n++;
  }
  int popFront() {
    Node *x = head;
    int v = x->value;
    head = x->next;
    if (head != 0) { head->prev = 0; } else { tail = 0; }
    delete x;
    n--;
    return v;
  }
  int popBack() {
    Node *x = tail;
    int v = x->value;
    tail = x->prev;
    if (tail != 0) { tail->next = 0; } else { head = 0; }
    delete x;
    n--;
    return v;
  }
  int size() const { return n; }
  bool empty() const { return n == 0; }
  int sum() const { int s = 0; Node *p = head; while (p != 0) { s = s + p->value; p = p->next; } return s; }
  void reverse() {
    Node *p = head;
    while (p != 0) { Node *t = p->next; p->next = p->prev; p->prev = t; p = t; }
    Node *t2 = head; head = tail; tail = t2;
  }
  int at(int i) const { Node *p = head; while (i > 0) { p = p->next; i--; } return p->value; }
  void removeIf(int m) {
    Node *p = head;
    while (p != 0) {
      Node *nx = p->next;
      if (p->value % m == 0) {
        if (p->prev != 0) { p->prev->next = p->next; } else { head = p->next; }
        if (p->next != 0) { p->next->prev = p->prev; } else { tail = p->prev; }
        delete p;
        n--;
      }
      p = nx;
    }
  }
};

class Stack : public List {
public:
  void push(int v) { pushBack(v); }
  int pop() { return popBack(); }
  int top() const { return tail->value; }
};

class Queue : public List {
public:
  void put(int v) { pushBack(v); }
  int take() { return popFront(); }
};

class Tree {
public:
  int key;
  Tree *left;
  Tree *right;
  Tree(int k) { key = k; left = 0; right = 0; }
};

Tree *insert(Tree *t, int k) {
  if (t == 0) { return new Tree(k); }
  if (k < t->key) { t->left = insert(t->left, k); }
  else if (k > t->key) { t->right = insert(t->right, k); }
  return t;
}

bool contains(Tree *t, int k) {
  while (t != 0) {
    if (k == t->key) { return true; }
    if (k < t->key) { t = t->left; } else { t = t->right; }
  }
  return false;
}

int height(Tree *t) {
  if (t == 0) { return 0; }
  int a = height(t->left);
  int b = height(t->right);
  if (a > b) { return a + 1; }
  return b + 1;
}

int inorder(Tree *t, int *out, int n) {
  if (t == 0) { return n; }
  n = inorder(t->left, out, n);
  out[n] = t->key;
  n++;
  return inorder(t->right, out, n);
}

Tree *minNode(Tree *t) { while (t->left != 0) { t = t->left; } return t; }

Tree *removeKey(Tree *t, int k) {
  if (t == 0) { return 0; }
  if (k < t->key) { t->left = removeKey(t->left, k); return t; }
  if (k > t->key) { t->right = removeKey(t->right, k); return t; }
  if (t->left == 0) { Tree *r = t->right; delete t; return r; }
  if (t->right == 0) { Tree *l = t->left; delete t; return l; }
  Tree *m = minNode(t->right);
  t->key = m->key;
  t->right = removeKey(t->right, m->key);
  return t;
}

void destroy(Tree *t) { if (t == 0) { return; } destroy(t->left); destroy(t->right); delete t; }

int main() {
  List l;
  for (int i = 0; i < 50; i++) { if (i % 2 == 0) { l.pushBack(i); } else { l.pushFront(i); } }
  l.reverse();
  l.removeIf(3);
  show("list "); show(l.size()); show(" "); show(l.sum()); show(" "); show(l.at(5)); print_line();
  Stack s;
  Queue q;
  for (int i = 1; i <= 20; i++) { s.push(i * i); q.put(i * 3); }
  int ss = 0;
  int qs = 0;
  while (!s.empty()) { ss = ss * 3 + s.pop() % 7; ss = ss % 100003; }
  while (!q.empty()) { qs = qs * 5 + q.take() % 11; qs = qs % 100003; }
  show("stack "); show(ss); show(" queue "); show(qs); print_line();
  Tree *root = 0;
  int seed = 7;
  for (int i = 0; i < 200; i++) { seed = (seed * 75 + 74) % 65537; root = insert(root, seed % 1000); }
  int keys[256];
  int n = inorder(root, keys, 0);
  show("tree "); show(n); show(" height "); show(height(root)); show(" min "); show(keys[0]); show(" max "); show(keys[n - 1]); print_line();
  for (int i = 0; i < n; i = i + 3) { root = removeKey(root, keys[i]); }
  int found = 0;
  for (int i = 0; i < n; i++) { if (contains(root, keys[i])) { found++; } }
  show("after "); show(found); show(" height "); show(height(root)); print_line();
  destroy(root);
  return 0;
}
