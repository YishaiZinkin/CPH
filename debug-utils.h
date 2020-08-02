/* Some useful functions for debugging  */

#include <iostream>
#include "c-family/c-pretty-print.h"


void
print_tree_code (tree t)
{
#define DEFTREECODE(SYM, STRING, TYPE, NARGS) \
  case SYM: \
    std::cout << STRING << " (" #SYM ")"; \
    break;

  switch (TREE_CODE (t))
    {
#include "tree.def"

      default:
        std::cout << "UNKNOWN CODE";
    }

#undef DEFTREECODE
}

void
print_gimple_code (const gimple *g)
{
#define DEFGSCODE(SYM, STRING, STRUCT)  \
  case SYM: \
    std::cout << STRING << " (" #SYM ")"; \
    break;

  switch (gimple_code (g))
    {
#include "gimple.def"

      default:
        std::cout << "UNKNOWN CODE";
    }

  #undef DEFGSCODE
}

void
print_c_tree_and_code (tree t)
{
  // Based on print_c_tree()

  c_pretty_printer pp;

  pp_needs_newline (&pp) = true;
  pp.buffer->stream = stdout;
  std::cout << std::endl;
  print_tree_code (t);
  std::cout  << ":";
  pp.statement (t);
  pp_newline_and_flush (&pp);
}

tree
cb_walk_tree_print (tree * tp, int * walk_subtrees,
     void * data ATTRIBUTE_UNUSED)
{
  print_c_tree_and_code (*tp);
  return NULL_TREE;
}
