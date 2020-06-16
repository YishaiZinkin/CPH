#include <iostream>

#include "gcc-plugin.h"

#include "tree-pass.h"
#include "context.h"
#include "basic-block.h"
#include "gimple-pretty-print.h"
#include "tree.h"
#include "gimple.h"
#include "tree-iterator.h"
#include "gimple-iterator.h"
#include "cgraph.h"
#include "vec.h"
#include "varasm.h"

#include "debug-utils.h"

int plugin_is_GPL_compatible;

static struct plugin_info my_gcc_plugin_info =
{ "0.0", "Forward-Edge Code Pointer Hiding" };


static bool
is_char (const_tree type)
{
  const_tree main_variant = TYPE_MAIN_VARIANT (type);
  return main_variant == char_type_node ||
         main_variant == signed_char_type_node ||
         main_variant == unsigned_char_type_node;
}

static void
handle_constructor (tree ctor)
{
  gcc_assert (TREE_CODE (ctor) == CONSTRUCTOR);

  unsigned i;
  constructor_elt *init_val;

  // Is the constructor empty?
  if (CONSTRUCTOR_ELTS (ctor) == NULL)
  {
    return;
  }

  FOR_EACH_VEC_ELT (*CONSTRUCTOR_ELTS (ctor), i, init_val)
  {
    if (TREE_CODE (init_val->value) == CONSTRUCTOR)
    {
      handle_constructor (init_val->value);
    }
    else if (TREE_CODE (init_val->value) == INTEGER_CST &&
             !is_char (TREE_TYPE (init_val->value)))
    {
      init_val->value = build_int_cst (TREE_TYPE (init_val->value), 6);
    }
  }
}

static void
callback (void *gcc_data, void *user_data)
{
  varpool_node *node;

  FOR_EACH_VARIABLE (node)
  {
    tree var = node->decl;
    tree type = TREE_TYPE (var);

    if (TREE_CODE (type) == INTEGER_TYPE &&
        !is_char (type))
    {
      if (DECL_INITIAL (var))
      {
        /* The initial value might be shared, so we can't free it. It also
           worth mentioning that if the variable is constant, references in the
           current translation unit won't be affected because they are replaced
           with the variable's value in an earlier stage of the compilation  */
        DECL_INITIAL (var) = build_int_cst (type, 6);
      }
    }
    else if (TREE_CODE (type) == ARRAY_TYPE ||
             TREE_CODE (type) == RECORD_TYPE ||
             TREE_CODE (type) == UNION_TYPE)
    {
      tree ctor = DECL_INITIAL (var);
      if (ctor && TREE_CODE (ctor) != STRING_CST)
      {
        handle_constructor (ctor);
      }
    }
  }
}

// namespace {
// const pass_data cph_pass_data =
// {
//   GIMPLE_PASS,
//   "cph_pass",     /* name */
//   OPTGROUP_NONE,       /* optinfo_flags */
//   TV_NONE,         /* tv_id */
//   PROP_gimple_any,     /* properties_required */
//   0,             /* properties_provided */
//   0,             /* properties_destroyed */
//   0,              todo_flags_start 
//   0            /* todo_flags_finish */
// };

// struct cph_pass : gimple_opt_pass
// {
//   cph_pass(gcc::context * ctx) :
//     gimple_opt_pass(cph_pass_data, ctx)
//   {
//   }

//   virtual unsigned int execute(function * fun) override
//   {
//   }

//   virtual cph_pass *clone() override
//   {
//     // We do not clone ourselves
//     return this;
//   }
// };
// }

int
plugin_init (struct plugin_name_args *plugin_info,
                struct plugin_gcc_version *version)
{
  register_callback(plugin_info->base_name, PLUGIN_INFO, NULL,
                    &my_gcc_plugin_info);

  register_callback (plugin_info->base_name, PLUGIN_ALL_IPA_PASSES_START,
                     callback, NULL);

  // struct register_pass_info pass_info;

  // pass_info.pass = new cph_pass(g);
  // pass_info.reference_pass_name = "ssa";
  // pass_info.ref_pass_instance_number = 1;
  // pass_info.pos_op = PASS_POS_INSERT_BEFORE;

  // register_callback(plugin_info->base_name, PLUGIN_PASS_MANAGER_SETUP, NULL,
  //                   &pass_info);

  return 0;
}
