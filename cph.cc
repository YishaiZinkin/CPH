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
handle_array (tree var)
{
  tree type = TREE_TYPE (var);
  unsigned arr_nelem = int_cst_value (TYPE_MAX_VALUE (TYPE_DOMAIN (type))) + 1;

  gcc_assert (TREE_CODE (type) == ARRAY_TYPE);
  gcc_assert (TREE_CODE (TREE_TYPE (type)) == INTEGER_TYPE);

  tree constructor = DECL_INITIAL (var);
  if (constructor)
  {
    unsigned i;
    constructor_elt *init_val;
    FOR_EACH_VEC_ELT (*CONSTRUCTOR_ELTS (constructor), i, init_val)
    {
      init_val->value = build_int_cst (TREE_TYPE (type), 6);
    }
  }
  else
  {
    // Build an empty initialization list
    vec<constructor_elt, va_gc> *v;
    vec_alloc (v, arr_nelem);
    constructor = build_constructor (type, v);
    DECL_INITIAL (var) = constructor;
  }

  // Append values to fill the rest of the intialization list
  unsigned i = CONSTRUCTOR_ELTS (constructor)->length ();
  for (; i < arr_nelem; i++)
  {
    CONSTRUCTOR_APPEND_ELT (CONSTRUCTOR_ELTS (constructor), NULL_TREE,
                            build_int_cst (TREE_TYPE (type), 6));
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
      /* Just override the current initial value, whether it exists or not.
         If it does exist it might be shared, so we can't free it. It also
         worth mentioning that if the variable is constant, references in the
         current translation unit won't be effected because they are replaced
         with the variable's value it an earlier stage of the compilation  */
      DECL_INITIAL (var) = build_int_cst (type, 6);
    }
    /* Handle the case of array of integers.
       Currently the case of arrays of more than one dimension isn't
       supported  */
    else if (TREE_CODE (type) == ARRAY_TYPE &&
             TREE_CODE (TREE_TYPE (type)) == INTEGER_TYPE &&
             !is_char (TREE_TYPE (type)))
    {
      handle_array (var);
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
