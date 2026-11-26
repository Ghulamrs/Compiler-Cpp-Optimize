#include <cstddef>
#include <cstdio>
#include <type_traits>
struct Empty {};
struct Data { int a; double b; };
struct Ctor { Ctor(int) {} };
struct Poly { virtual ~Poly() {} };
struct Abs { virtual void f() = 0; };
struct Der : Data {};
struct NoCopy { NoCopy() {} NoCopy(const NoCopy &) = delete; };
struct Throws { Throws() {} Throws(const Throws &) {} Throws &operator=(const Throws &) { return *this; } };
union U { int i; float f; };
enum E { e0 };
enum class EC : short { a };
int f(int);
#define P(...) std::printf("%d", (int)(__VA_ARGS__))
#define CATS(T)                                                                                    \
    P(std::is_void<T>::value); P(std::is_integral<T>::value); P(std::is_array<T>::value);          \
    P(std::is_pointer<T>::value); P(std::is_member_object_pointer<T>::value);                      \
    P(std::is_member_function_pointer<T>::value); P(std::is_enum<T>::value);                        \
    P(std::is_union<T>::value); P(std::is_class<T>::value); P(std::is_function<T>::value);         \
    P(std::is_reference<T>::value); P(std::is_object<T>::value); P(std::is_scalar<T>::value);      \
    P(std::is_compound<T>::value); P(std::is_member_pointer<T>::value); std::printf(" %s\n", #T)
#define PROPS(T)                                                                                   \
    P(std::is_trivial<T>::value); P(std::is_trivially_copyable<T>::value);                         \
    P(std::is_standard_layout<T>::value); P(std::is_pod<T>::value); P(std::is_literal_type<T>::value); \
    P(std::is_empty<T>::value); P(std::is_polymorphic<T>::value); P(std::is_abstract<T>::value);   \
    P(std::has_virtual_destructor<T>::value); std::printf(" ");                                    \
    P(std::is_default_constructible<T>::value); P(std::is_copy_constructible<T>::value);           \
    P(std::is_move_constructible<T>::value); P(std::is_copy_assignable<T>::value);                 \
    P(std::is_move_assignable<T>::value); P(std::is_destructible<T>::value); std::printf(" ");     \
    P(std::is_trivially_default_constructible<T>::value); P(std::is_trivially_copy_constructible<T>::value); \
    P(std::is_trivially_move_constructible<T>::value); P(std::is_trivially_copy_assignable<T>::value); \
    P(std::is_trivially_move_assignable<T>::value); P(std::is_trivially_destructible<T>::value);   \
    std::printf(" ");                                                                              \
    P(std::is_nothrow_default_constructible<T>::value); P(std::is_nothrow_copy_constructible<T>::value); \
    P(std::is_nothrow_move_constructible<T>::value); P(std::is_nothrow_copy_assignable<T>::value); \
    P(std::is_nothrow_move_assignable<T>::value); P(std::is_nothrow_destructible<T>::value);       \
    std::printf(" %s\n", #T)
#define PROPS_NOT_NOTHROW(T)                                                                       \
    P(std::is_trivial<T>::value); P(std::is_trivially_copyable<T>::value);                         \
    P(std::is_standard_layout<T>::value); P(std::is_pod<T>::value); P(std::is_literal_type<T>::value); \
    P(std::is_empty<T>::value); P(std::is_polymorphic<T>::value); P(std::is_abstract<T>::value);   \
    P(std::has_virtual_destructor<T>::value); std::printf(" ");                                    \
    P(std::is_default_constructible<T>::value); P(std::is_copy_constructible<T>::value);           \
    P(std::is_move_constructible<T>::value); P(std::is_copy_assignable<T>::value);                 \
    P(std::is_move_assignable<T>::value); P(std::is_destructible<T>::value);                       \
    std::printf(" %s\n", #T)
int main() {
    CATS(void); CATS(int); CATS(const int); CATS(int[3]); CATS(int[]); CATS(int *); CATS(int Data::*);
    CATS(void (Data::*)()); CATS(int (Poly::*)(int) const); CATS(E); CATS(EC); CATS(U); CATS(Data);
    CATS(int &); CATS(int &&); CATS(std::nullptr_t); CATS(double); CATS(const volatile Data);
    PROPS(int); PROPS(Empty); PROPS(Data); PROPS(Ctor); PROPS_NOT_NOTHROW(Poly); PROPS_NOT_NOTHROW(Abs); PROPS(Der); PROPS(NoCopy);
    PROPS(Throws); PROPS(U); PROPS(E); PROPS(int *); PROPS(int &);
    P(std::is_constructible<Ctor, int>::value); P(std::is_constructible<Ctor>::value);
    P(std::is_constructible<int, double>::value); P(std::is_constructible<Data, int, double>::value);
    P(std::is_trivially_constructible<int, int>::value); P(std::is_nothrow_constructible<Throws, const Throws &>::value);
    P(std::is_assignable<int &, int>::value); P(std::is_assignable<int, int>::value);
    P(std::is_trivially_assignable<Data &, const Data &>::value); P(std::is_nothrow_assignable<Throws &, Throws>::value);
    std::printf("\n");
    P(std::is_base_of<Data, Der>::value); P(std::is_base_of<Der, Data>::value); P(std::is_base_of<Data, Data>::value);
    P(std::is_base_of<int, int>::value); P(std::is_convertible<Der *, Data *>::value);
    P(std::is_convertible<Data *, Der *>::value); P(std::is_convertible<int, double>::value);
    P(std::is_convertible<int, Ctor>::value); P(std::is_convertible<Ctor, int>::value);
    std::printf("\n");
    std::printf("%d %d %d %d %d %d\n", (int)std::rank<int>::value, (int)std::rank<int[2][3]>::value, (int)std::rank<int[][4]>::value,
        (int)std::extent<int[2][3]>::value, (int)std::extent<int[2][3], 1>::value, (int)std::extent<int[][4]>::value);
    std::printf("%d %d %d\n", (int)std::extent<int[2][3], 2>::value, (int)std::extent<int>::value, (int)std::extent<int[][4], 1>::value);
    std::printf("%d %d %d\n", (int)std::is_same<std::remove_extent<int[2][3]>::type, int[3]>::value,
        (int)std::is_same<std::remove_all_extents<const int[2][3]>::type, const int>::value,
        (int)std::is_same<std::remove_extent<int[]>::type, int>::value);
    std::printf("%d %d %d %d\n", (int)std::is_same<std::add_lvalue_reference<int>::type, int &>::value,
        (int)std::is_same<std::add_lvalue_reference<int &&>::type, int &>::value,
        (int)std::is_same<std::add_rvalue_reference<int>::type, int &&>::value,
        (int)std::is_same<std::add_rvalue_reference<void>::type, void>::value);
    std::printf("%d %d %d %d %d\n", (int)std::is_same<std::make_signed<unsigned int>::type, int>::value,
        (int)std::is_same<std::make_signed<const unsigned char>::type, const signed char>::value,
        (int)std::is_same<std::make_unsigned<long>::type, unsigned long>::value,
        (int)std::is_same<std::make_unsigned<char>::type, unsigned char>::value,
        (int)std::is_same<std::make_signed<EC>::type, short>::value);
    std::printf("%d %d %d %d\n", (int)std::is_same<std::decay<const int &>::type, int>::value,
        (int)std::is_same<std::decay<int[3]>::type, int *>::value,
        (int)std::is_same<std::decay<const char (&)[4]>::type, const char *>::value,
        (int)std::is_same<std::decay<volatile Data>::type, Data>::value);
    std::printf("%d %d %d\n", (int)std::is_same<std::common_type<int, long>::type, long>::value,
        (int)std::is_same<std::common_type<char, short, int>::type, int>::value,
        (int)std::is_same<std::common_type<Der *, Data *>::type, Data *>::value);
    std::printf("%d %d\n", (int)std::is_same<std::underlying_type<EC>::type, short>::value,
        (int)std::is_integral<std::underlying_type<E>::type>::value);
    std::printf("%d %d %d\n", (int)std::alignment_of<double>::value, (int)std::alignment_of<char>::value, (int)std::alignment_of<Data>::value);
    std::printf("%d %d %d %d\n", (int)sizeof(std::aligned_storage<10, 8>::type), (int)alignof(std::aligned_storage<10, 8>::type),
        (int)sizeof(std::aligned_storage<8>::type), (int)alignof(std::aligned_storage<8>::type));
    std::printf("%d %d %d\n", (int)sizeof(std::aligned_union<2, int, double>::type), (int)alignof(std::aligned_union<2, int, double>::type),
        (int)std::aligned_union<2, int, double>::alignment_value);
    return 0;
}
