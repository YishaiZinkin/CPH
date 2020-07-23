#include <iostream>

#include "gcc-plugin.h"

#include "tree-pass.h"
#include "context.h"
#include "basic-block.h"
#include "gimple-pretty-print.h"
#include "tree.h"
#include "gimple.h"
#include "gimplify.h"
#include "tree-iterator.h"
#include "gimple-iterator.h"
#include "gimple-ssa.h"
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

namespace {

static tree
build_trivial_generic_function (const char * name)
{
  tree fndecl = build_fn_decl (name, build_function_type_list (void_type_node,
                                                                NULL_TREE));
  gcc_assert (fndecl != NULL);

  TREE_STATIC (fndecl) = 1;
  TREE_USED (fndecl) = 1;
  DECL_ARTIFICIAL (fndecl) = 1;
  DECL_IGNORED_P (fndecl) = 1;
  DECL_NO_INSTRUMENT_FUNCTION_ENTRY_EXIT (fndecl) = 1;
  DECL_UNINLINABLE (fndecl) = 1;

  // Create a return value
  tree retval = build_decl (UNKNOWN_LOCATION, RESULT_DECL,
          NULL_TREE, void_type_node);
  DECL_ARTIFICIAL (retval) = 1;
  DECL_IGNORED_P (retval) = 1;
  DECL_RESULT (fndecl) = retval;
  DECL_CONTEXT (retval) = fndecl;

  // Populate the function

  const char greeting[] = "Hello world!";
  tree call_stmt = build_call_expr (builtin_decl_implicit (BUILT_IN_PUTS), 1,
                                    build_string_literal (sizeof (greeting),
                                                          greeting));

  tree stmt_list = NULL_TREE;
  append_to_statement_list (call_stmt, &stmt_list);

  tree block = make_node (BLOCK);
  tree bind_expr
    = build3 (BIND_EXPR, void_type_node, NULL, stmt_list, block);

  DECL_INITIAL (fndecl) = block;
  BLOCK_SUPERCONTEXT (block) = fndecl;
  BLOCK_VARS (block) = BIND_EXPR_VARS (bind_expr);
  TREE_USED (block) = 1;

  DECL_SAVED_TREE (fndecl) = bind_expr;

  // Inform the callgraph about the new function
  push_struct_function (fndecl);
  cgraph_node::add_new_function (fndecl, true);
  pop_cfun ();

  return fndecl;
}

/* Build a CFG for a function in gimple form.  */

static void
build_cfg (tree fndecl)
{
  function *fun = DECL_STRUCT_FUNCTION (fndecl);
  gcc_assert (fun != NULL);
  gcc_assert (fndecl == fun->decl);

  gimple_opt_pass *lower_cf_pass = make_pass_lower_cf (g);
  push_cfun (fun);
  lower_cf_pass->execute (fun);
  pop_cfun ();
  delete lower_cf_pass;

  gimple_opt_pass *build_cfg_pass = make_pass_build_cfg (g);
  push_cfun (fun);
  build_cfg_pass->execute (fun);
  pop_cfun ();
  delete build_cfg_pass;
}

/* Convert a gimple+CFG function to SSA form.  */

static void
convert_to_ssa (tree fndecl)
{
  function *fun = DECL_STRUCT_FUNCTION (fndecl);
  gcc_assert (fun != NULL);
  gcc_assert (fndecl == fun->decl);

  gimple_opt_pass *build_ssa_pass = make_pass_build_ssa (g);
  push_cfun (fun);
  build_ssa_pass->execute (fun);
  pop_cfun ();
  delete build_ssa_pass;
}


const pass_data cph_pass_data =
{
  GIMPLE_PASS,
  "cph_pass",     /* name */
  OPTGROUP_NONE,       /* optinfo_flags */
  TV_NONE,         /* tv_id */
  PROP_gimple_any,     /* properties_required */
  0,             /* properties_provided */
  0,             /* properties_destroyed */
  0,             /* todo_flags_start  */
  0            /* todo_flags_finish */
};

struct cph_pass : gimple_opt_pass
{
  cph_pass(gcc::context * ctx) :
    gimple_opt_pass(cph_pass_data, ctx)
  {
  }

  virtual unsigned int execute(function * func) override
  {
    if (strcmp (function_name (func), "main") == 0)
    {
      tree fndecl = build_trivial_generic_function ("test_fn");
      gimplify_function_tree (fndecl);
      build_cfg (fndecl);
      convert_to_ssa (fndecl);
      DECL_STRUCT_FUNCTION (fndecl)->curr_properties = cfun->curr_properties;

      // Insert a call to this function at the beginning of the first BB of main
      gcall * fncall = gimple_build_call (fndecl, 0);
      gimple_stmt_iterator gsi = gsi_start_bb (ENTRY_BLOCK_PTR_FOR_FN (func)->next_bb);

      gsi_insert_before (&gsi, fncall, GSI_NEW_STMT);
      update_stmt (fncall);
    }

    return 0;
  }

  virtual cph_pass *clone() override
  {
    // We do not clone ourselves
    return this;
  }
};
}

int
plugin_init (struct plugin_name_args *plugin_info,
                struct plugin_gcc_version *version)
{
  register_callback(plugin_info->base_name, PLUGIN_INFO, NULL,
                    &my_gcc_plugin_info);

  register_callback (plugin_info->base_name, PLUGIN_ALL_IPA_PASSES_START,
                     callback, NULL);

  struct register_pass_info pass_info;

  pass_info.pass = new cph_pass(g);
  pass_info.reference_pass_name = "*all_optimizations";
  pass_info.ref_pass_instance_number = 1;
  pass_info.pos_op = PASS_POS_INSERT_AFTER;

  register_callback(plugin_info->base_name, PLUGIN_PASS_MANAGER_SETUP, NULL,
                    &pass_info);

  return 0;
}
