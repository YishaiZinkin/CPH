#include <stdio.h>
#include <assert.h>

void
foo ()
{
  printf("%s? %d!\n", "Why", 42);
}

struct A
{
  int x;
  struct
  {
    int b;

    struct
    {
      const int z;
      int y;
    };
    union
    {
      int q;
      char g;
    };
  };
};

union B
{
  int a;
  char c;
  struct
  {
    int z;
  };
};

union C
{
  struct
  {
    char q;
    int z;
  };
  int c;
  int g;
};

struct A a = { 5, { 2, { 1 }, { 3 } } };
union B u = { 5 };
union C d = { 0, 8 };

short b = 9;
int c = 42;
static long w = 7;

int x[4][2] = { { 1, 2 }, { }, { 3 } };
const short y[] = { 1, 3, 3 };
long q[] = { 1 };

struct D
{
  int (*baz)(const char *, ...);
};

void baz (void (*p)())
{
  void (*q)() = p;

  if (p == foo)
    printf ("WOW\n");
  else
    printf("Cry\n");
}

int
main (int argc, char const *argv[])
{
  void (*bar)(void (*)()) = baz;
  bar (foo);

  return 0;
}
