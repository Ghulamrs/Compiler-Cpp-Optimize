// Access control and object lifetime: private and protected members, overloaded
// constructors, member objects built in order, a copy constructor, global
// objects constructed before main, friend functions, const methods.
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

int created = 0;
int destroyed = 0;

class Money {
  long cents;
public:
  Money() { cents = 0; }
  Money(long c) { cents = c; }
  Money(int units, int c) { cents = units * 100 + c; }
  long value() const { return cents; }
  Money operator+(Money o) { Money r(cents + o.cents); return r; }
  Money operator-(Money o) { Money r(cents - o.cents); return r; }
  Money operator*(int k) { Money r(cents * k); return r; }
  bool operator<(Money o) { return cents < o.cents; }
  bool operator>(Money o) { return cents > o.cents; }
  bool operator==(Money o) { return cents == o.cents; }
  friend void show(Money m);
};

void show(Money m) {
  long c = m.cents;
  if (c < 0) { show("-"); c = -c; }
  long frac = c % 100;
  show(c / 100); show(".");
  if (frac < 10) { show("0"); }
  show(frac);
}

class Stamp {
  int day;
  int seq;
public:
  Stamp() { day = 0; seq = 0; }
  Stamp(int d, int s) { day = d; seq = s; }
  int key() const { return day * 1000 + seq; }
};

class Account {
protected:
  int number;
  Money balance;
  Stamp opened;
  int ops;
public:
  Account(int n, Money start, int day) { number = n; balance = start; Stamp s(day, n); opened = s; ops = 0; created++; }
  Account(const Account &o) { number = o.number + 1000; balance = o.balance; opened = o.opened; ops = 0; created++; }
  virtual ~Account() { destroyed++; }
  virtual bool withdraw(Money m) {
    if (balance < m) { return false; }
    balance = balance - m;
    ops++;
    return true;
  }
  virtual void deposit(Money m) { balance = balance + m; ops++; }
  virtual Money monthEnd() { Money none; return none; }
  Money funds() const { return balance; }
  int id() const { return number; }
  int count() const { return ops; }
  int since() const { return opened.key(); }
  friend bool transfer(Account &from, Account &to, Money m);
};

class Savings : public Account {
  int rate;
public:
  Savings(int n, Money start, int day, int permille) : Account(n, start, day) { rate = permille; }
  Money monthEnd() {
    Money interest(balance.value() * rate / 12000);
    balance = balance + interest;
    return interest;
  }
};

class Checking : public Account {
  Money overdraft;
  Money fee;
public:
  Checking(int n, Money start, int day, Money limit) : Account(n, start, day) {
    overdraft = limit;
    Money f(150);
    fee = f;
  }
  bool withdraw(Money m) {
    Money floor = balance + overdraft;
    if (floor < m) { return false; }
    balance = balance - m;
    ops++;
    return true;
  }
  Money monthEnd() {
    Money zero;
    if (balance < zero) { balance = balance - fee; return fee; }
    return zero;
  }
};

bool transfer(Account &from, Account &to, Money m) {
  if (!from.withdraw(m)) { return false; }
  to.deposit(m);
  return true;
}

Money ledgerFloor;
Savings *reserve;

int main() {
  Money startMoney(100000);
  reserve = new Savings(1, startMoney, 1, 25);
  Account *book[10];
  for (int i = 0; i < 10; i++) {
    Money s(i * 5000 + 1000);
    if (i % 2 == 0) { book[i] = new Savings(100 + i, s, 3, 20 + i); }
    else { Money lim(20000); book[i] = new Checking(100 + i, s, 4, lim); }
  }
  int failed = 0;
  for (int round = 0; round < 12; round++) {
    for (int i = 0; i < 10; i++) {
      int j = (i * 7 + round * 3) % 10;
      Money m(round * 173 + i * 91 + 250);
      if (i != j) { if (!transfer(*book[i], *book[j], m)) { failed++; } }
      Money w(round * 60 + 40);
      if (!book[i]->withdraw(w)) { failed++; }
    }
    for (int i = 0; i < 10; i++) { book[i]->monthEnd(); }
    reserve->monthEnd();
  }
  Money total;
  int ops = 0;
  for (int i = 0; i < 10; i++) { total = total + book[i]->funds(); ops = ops + book[i]->count(); }
  show("total ");
  show(total);
  show(" ops "); show(ops); show(" failed "); show(failed); print_line();
  show("reserve ");
  show(reserve->funds());
  print_line();
  Account copy(*book[3]);
  show("copy "); show(copy.id()); show(" since "); show(copy.since()); print_line();
  for (int i = 0; i < 10; i++) { delete book[i]; }
  delete reserve;
  show("created "); show(created); show(" destroyed "); show(destroyed); print_line();
  return 0;
}
