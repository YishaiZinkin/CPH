#include <stdio.h>

const short x[4];

int
main (int argc, char const *argv[])
{
  for (int i = 0; i < sizeof (x) / sizeof (*x); i++)
  {
    printf ("x[%d] = %d\n", i, x[i]);
  }

  return 0;
}
