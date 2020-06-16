#include <stdio.h>
#include <assert.h>

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

int
main (int argc, char const *argv[])
{
  assert (a.x == 6);
  assert (a.b == 6);
  assert (a.z == 6);
  assert (a.q == 6);

  assert (u.a == 6);
  assert (d.z == 6);

  assert (b == 6);
  assert (c == 6);
  assert (w == 6);

  for (int i = 0; i < sizeof (x) / sizeof (*x); i++)
  {
    for (int j = 0; j < sizeof (*x) / sizeof (**x); j++)
    {
      if (x[i][j])
      {
        assert (x[i][j] == 6);
      }
    }
  }

  for (int i = 0; i < sizeof (y) / sizeof (*y); i++)
  {
    if (y[i])
      assert (y[i] == 6);
  }

  for (int i = 0; i < sizeof (q) / sizeof (*q); i++)
  {
    if (q[i])
      assert (q[i] == 6);
  }

  printf("All Good :)\n");

  return 0;
}
