#include <iostream>
#include <vector>

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
#include "stringpool.h"

#include "debug-utils.h"

int plugin_is_GPL_compatible;

static struct plugin_info my_gcc_plugin_info =
{ "0.0", "Forward-Edge Code Pointer Hiding" };


namespace {

/* Used to relate function declarations with the matching trampoline
   function.  */

struct tramp_to_fndecl
{
  tree tramp_fndecl;
  tree fndecl;
};

/* Holds all the trampoline functions generated.  */

std::vector<tramp_to_fndecl> tramp_fns;


static bool
is_fn_ptr (const_tree type)
{
  return TREE_CODE (type) == POINTER_TYPE &&
         TREE_CODE (TREE_TYPE (type)) == FUNCTION_TYPE;
}


static int
alloc_build_trampoline_name (char **tramp_name, const char *fn_name)
{
  return asprintf (tramp_name, "__cph_tramp_%s", fn_name);
}


/* Build a FUNCTION_DECL (without body) for a trampoline function that calls
   CALL_FNDECL.  */

static tree
build_trampoline_decl (tree call_fndecl)
{
  const char *call_fn_name = fndecl_name (call_fndecl);

  char *tramp_name;
  if (alloc_build_trampoline_name (&tramp_name, call_fn_name) < 0)
  {
    /* String allocation failed  */
    gcc_unreachable ();
  }

  return
    build_decl (input_location, FUNCTION_DECL, get_identifier (tramp_name),
                  build_function_type_list (void_type_node, NULL_TREE));
}


/* Build a trampoline function that calls CALL_FNDECL in generic form.  */

static tree
do_build_generic_tramp_fn (tree call_fndecl)
{
  tree fndecl = build_trampoline_decl (call_fndecl);
  gcc_assert (fndecl != NULL);

  TREE_STATIC (fndecl) = 1;
  TREE_USED (fndecl) = 1;
  DECL_ARTIFICIAL (fndecl) = 1;
  DECL_IGNORED_P (fndecl) = 1;
  DECL_NO_INSTRUMENT_FUNCTION_ENTRY_EXIT (fndecl) = 1;
  DECL_UNINLINABLE (fndecl) = 1;

  /* Create a return value  */
  tree retval = build_decl (UNKNOWN_LOCATION, RESULT_DECL,
          NULL_TREE, void_type_node);
  DECL_ARTIFICIAL (retval) = 1;
  DECL_IGNORED_P (retval) = 1;
  DECL_RESULT (fndecl) = retval;
  DECL_CONTEXT (retval) = fndecl;

  /* Populate the function  */

  tree stmt_list = NULL_TREE;

  tree call_stmt = build_call_expr (call_fndecl, 0);
  append_to_statement_list (call_stmt, &stmt_list);

  /* Add a print to indicate a trampoline was called  */
  char message[128];
  sprintf (message, "Trampoline %s was called!", fndecl_name (fndecl));
  call_stmt = build_call_expr (builtin_decl_implicit (BUILT_IN_PUTS), 1,
                               build_string_literal (sizeof (message),
                                                     message));
  append_to_statement_list (call_stmt, &stmt_list);

  tree block = make_node (BLOCK);
  tree bind_expr
    = build3 (BIND_EXPR, void_type_node, NULL, stmt_list, block);

  DECL_INITIAL (fndecl) = block;
  BLOCK_SUPERCONTEXT (block) = fndecl;
  BLOCK_VARS (block) = BIND_EXPR_VARS (bind_expr);
  TREE_USED (block) = 1;

  DECL_SAVED_TREE (fndecl) = bind_expr;

  return fndecl;
}


/* Like do_build_generic_tramp_fn, but also add the new function to the
   callgraph.  */

static tree
build_generic_tramp_fn (tree call_fndecl)
{
  tree fndecl = do_build_generic_tramp_fn (call_fndecl);

  /* Inform the callgraph about the new function  */
  push_struct_function (fndecl);
  cgraph_node::add_new_function (fndecl, false);
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


/* Build a trampoline function that calls CALL_FNDECL in gimple-ssa form.  */

static tree
build_ssa_tramp_fn (tree call_fndecl)
{
  tree fndecl = do_build_generic_tramp_fn (call_fndecl);

  /* Inform the callgraph about the new function  */
  push_struct_function (fndecl);
  cgraph_node::add_new_function (fndecl, true);
  pop_cfun ();

  gimplify_function_tree (fndecl);
  build_cfg (fndecl);
  convert_to_ssa (fndecl);

  return fndecl;
}


/* Get a trampoline fndecl that calls FNDECL. If no matching trampoline
   function already exists, create one.
   If GENERIC is true, the function will be created in generic form. Else the
   function will be created in gimple-ssa form.  */

static tree
get_create_tramp_fn (tree fndecl, bool generic)
{
  std::vector<tramp_to_fndecl>::iterator it;
  for (it = tramp_fns.begin(); it != tramp_fns.end(); it++)
  {
    if (it->fndecl == fndecl)
    {
      return it->tramp_fndecl;
    }
  }

  tree tramp_fndecl = generic ? build_generic_tramp_fn (fndecl)
                              : build_ssa_tramp_fn (fndecl);

  /* Add the new trampoline function to the tramp_fns vector  */
  struct tramp_to_fndecl tramp_to_fndecl = { tramp_fndecl, fndecl };
  tramp_fns.push_back (tramp_to_fndecl);

  return tramp_fndecl;
}


/* Replaces function pointers in CTOR with matching trampoline functions.  */

static void
handle_constructor (tree ctor)
{
  gcc_assert (TREE_CODE (ctor) == CONSTRUCTOR);

  unsigned i;
  constructor_elt *init_val;

  /* Is it an empty constructor?  */
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
    else if (TREE_CODE (init_val->value) == ADDR_EXPR &&
             is_fn_ptr (TREE_TYPE (init_val->value)))
    {
      tree pointed_fndecl = TREE_OPERAND (init_val->value, 0);

      tree tramp_fndecl = get_create_tramp_fn (pointed_fndecl, true);

      /* We don't free the current initial value because it might be shared  */
      init_val->value = build_fold_addr_expr (tramp_fndecl);
    }
  }
}


/* The part of the plugin that handles global variables.  */

static void
ipa_callback (void *gcc_data, void *user_data)
{
  varpool_node *node;

  FOR_EACH_VARIABLE (node)
  {
    tree var = node->decl;
    tree type = TREE_TYPE (var);

    /* Is it an initialized function pointer?  */
    if (is_fn_ptr (type) &&
        DECL_INITIAL (var))
    {
      gcc_assert (TREE_CODE (DECL_INITIAL (var)) == ADDR_EXPR);

      /* DECL_INITIAL (var) is ADDR_EXPR, so we need TREE_OPERAND to get
         the FUNCTION_DECL  */
      tree pointed_fndecl = TREE_OPERAND (DECL_INITIAL (var), 0);

      tree tramp_fndecl = get_create_tramp_fn (pointed_fndecl, true);

      /* We don't free the current DECL_INITIAL because it might be shared  */
      DECL_INITIAL (var) = build_fold_addr_expr (tramp_fndecl);
    }
    else if (TREE_CODE (type) == ARRAY_TYPE ||
             TREE_CODE (type) == RECORD_TYPE ||
             TREE_CODE (type) == UNION_TYPE)
    {
      tree ctor = DECL_INITIAL (var);
      if (ctor && TREE_CODE (ctor) == CONSTRUCTOR)
      {
        handle_constructor (ctor);
      }
    }
  }
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
    // if (strcmp (function_name (func), "foo") == 0)
    // {
    //   /* Replace the address of the call at the beginning of foo() with the
    //      address of test_fn, but don't modify the arguments or the return
    //      type  */

    //   gimple_stmt_iterator gsi =
    //         gsi_start_bb (ENTRY_BLOCK_PTR_FOR_FN (func)->next_bb);
    //   gimple * stmt = gsi_stmt (gsi);

    //    Make sure the statment we're dealing with is actually a call  
    //   gcc_assert (gimple_code (stmt) == GIMPLE_CALL);

    //   /* gimple_call_fn() returns ADDR_EXPR, so we need TREE_OPERAND to get
    //      the FUNCTION_DECL  */
    //   tree called_fn_decl = TREE_OPERAND (gimple_call_fn (stmt), 0);

    //   tree fndecl = build_ssa_tramp_fn (called_fn_decl);
    //   DECL_STRUCT_FUNCTION (fndecl)->curr_properties = cfun->curr_properties;

    //   gimple_call_set_fndecl (stmt, fndecl);
    // }

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
                     ipa_callback, NULL);

  struct register_pass_info pass_info;

  pass_info.pass = new cph_pass(g);
  pass_info.reference_pass_name = "*all_optimizations";
  pass_info.ref_pass_instance_number = 1;
  pass_info.pos_op = PASS_POS_INSERT_AFTER;

  register_callback(plugin_info->base_name, PLUGIN_PASS_MANAGER_SETUP, NULL,
                    &pass_info);

  return 0;
}
