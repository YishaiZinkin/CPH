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

/* Holds all the trampoline functions generated for functions that are private
   to this translation unit.  */

std::vector<tramp_to_fndecl> priv_tramp_fns;


static bool
is_fn_ptr (const_tree type)
{
  gcc_assert (type != NULL_TREE);

  return TREE_CODE (type) == POINTER_TYPE &&
         TREE_CODE (TREE_TYPE (type)) == FUNCTION_TYPE;
}


static bool
is_fn_addr (const_tree t)
{
  return t != NULL_TREE &&
         TREE_CODE (t) == ADDR_EXPR &&
         is_fn_ptr (TREE_TYPE (t));
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

  tree fndecl =
         build_fn_decl (tramp_name,
                        build_function_type_list (void_type_node, NULL_TREE));

  free (tramp_name);
  return fndecl;
}


/* Build in generic form a trampoline function that calls CALL_FNDECL.  */

static tree
do_build_generic_tramp_fn (tree call_fndecl, bool is_public)
{
  tree fndecl = build_trampoline_decl (call_fndecl);
  gcc_assert (fndecl != NULL);

  DECL_EXTERNAL (fndecl) = 0;
  TREE_STATIC (fndecl) = 1;
  TREE_PUBLIC (fndecl) = is_public;
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
build_generic_tramp_fn (tree call_fndecl, bool is_public)
{
  tree fndecl = do_build_generic_tramp_fn (call_fndecl, is_public);

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
build_ssa_tramp_fn (tree call_fndecl, bool is_public)
{
  tree fndecl = do_build_generic_tramp_fn (call_fndecl, is_public);

  /* Inform the callgraph about the new function  */
  push_struct_function (fndecl);
  cgraph_node::add_new_function (fndecl, true);
  pop_cfun ();

  gimplify_function_tree (fndecl);
  build_cfg (fndecl);
  convert_to_ssa (fndecl);

  return fndecl;
}


/* Get an ADDR_EXPR of a trampoline that calls FN_ADDR_EXPR. If no matching
   trampoline function already exists, create one.
   If GENERIC is true, the function will be created in generic form. Else the
   function will be created in gimple-ssa form.  */

static tree
get_create_tramp_fn_addr_expr (tree fn_addr_expr, bool generic)
{
  gcc_assert (TREE_CODE (fn_addr_expr) == ADDR_EXPR);

  tree fndecl = TREE_OPERAND (fn_addr_expr, 0);

  if (TREE_PUBLIC (fndecl))
  {
    /* For PUBLIC functions the trampoline functions are assumed to be already
       defined  */
    tree tramp_fndecl = build_trampoline_decl (fndecl);
    return build_fold_addr_expr (tramp_fndecl);
  }

  std::vector<tramp_to_fndecl>::iterator it;
  for (it = priv_tramp_fns.begin(); it != priv_tramp_fns.end(); it++)
  {
    if (it->fndecl == fndecl)
    {
      return build_fold_addr_expr (it->tramp_fndecl);
    }
  }

  tree tramp_fndecl = generic ? build_generic_tramp_fn (fndecl, false)
                              : build_ssa_tramp_fn (fndecl, false);

  /* Add the new trampoline function to the priv_tramp_fns vector  */
  struct tramp_to_fndecl tramp_to_fndecl = { tramp_fndecl, fndecl };
  priv_tramp_fns.push_back (tramp_to_fndecl);

  return build_fold_addr_expr (tramp_fndecl);
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
    else if (is_fn_addr (init_val->value))
    {
      /* We don't free the current initial value because it might be shared  */
      init_val->value = get_create_tramp_fn_addr_expr (init_val->value, true);
    }
  }
}


/* The part of the plugin that handles global variables.  */

static void
ipa_callback (void *gcc_data, void *user_data)
{
  /* First we create a trmapoline function for each function defined public in
     this translation unit, for potential use in other translation units - when
     building trampoline function declaration for an EXTERNAL function it's
     defined EXTERNAL as well.  */
  cgraph_node *cnode;
  FOR_EACH_DEFINED_FUNCTION (cnode)
  {
    tree fndecl = cnode->decl;
    if (TREE_PUBLIC (fndecl))
    {
      build_generic_tramp_fn (fndecl, true);
    }
  }

  varpool_node *vnode;
  FOR_EACH_VARIABLE (vnode)
  {
    tree var = vnode->decl;
    tree type = TREE_TYPE (var);

    /* Is it an initialized function pointer?  */
    /* FIXME: What about when initializing integer (for example) with a function pointer?  */
    if (is_fn_ptr (type) &&
        DECL_INITIAL (var))
    {
      /* FIXME: Are you sure this assumption is right?  */
      gcc_assert (TREE_CODE (DECL_INITIAL (var)) == ADDR_EXPR);

      /* We don't free the current DECL_INITIAL because it might be shared  */
      DECL_INITIAL (var) =
          get_create_tramp_fn_addr_expr (DECL_INITIAL (var), true);
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


static void
handle_gimple_assign (gassign *gs)
{
  tree rhs1 = gimple_assign_rhs1 (gs);
  if (is_fn_addr (rhs1))
  {
    gimple_assign_set_rhs1 (gs, get_create_tramp_fn_addr_expr (rhs1, false));
  }

  tree rhs2 = gimple_assign_rhs2 (gs);
  if (is_fn_addr (rhs2))
  {
    gimple_assign_set_rhs2 (gs, get_create_tramp_fn_addr_expr (rhs2, false));
  }

  tree rhs3 = gimple_assign_rhs3 (gs);
  if (is_fn_addr (rhs3))
  {
    gimple_assign_set_rhs3 (gs, get_create_tramp_fn_addr_expr (rhs3, false));
  }
}


static void
handle_gimple_call (gcall *gs)
{
  unsigned num_args = gimple_call_num_args (gs);
  for (unsigned i = 0; i < num_args; i++)
  {
    tree arg = gimple_call_arg (gs, i);
    if (is_fn_addr (arg))
    {
      gimple_call_set_arg (gs, i, get_create_tramp_fn_addr_expr (arg, false));
    }
  }
}


static void
handle_gimple_cond (gcond *gs)
{
  tree lhs = gimple_cond_lhs (gs);
  if (is_fn_addr (lhs))
  {
    gimple_cond_set_lhs (gs, get_create_tramp_fn_addr_expr (lhs, false));
  }

  tree rhs = gimple_cond_rhs (gs);
  if (is_fn_addr (rhs))
  {
    gimple_cond_set_rhs (gs, get_create_tramp_fn_addr_expr (rhs, false));
  }
}


static void
handle_gimple_return (greturn *gs)
{
  tree retval = gimple_return_retval (gs);

  /* Actually not sure that this case is even possible in ssa-form, but why
     not to be on the safe side  */
  if (is_fn_addr (retval))
  {
    gimple_return_set_retval (gs,
                              get_create_tramp_fn_addr_expr (retval, false));
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
  cph_pass (gcc::context * ctx) :
    gimple_opt_pass (cph_pass_data, ctx)
  {
  }

  virtual unsigned int
  execute (function * func) override
  {
    /* Notice that this part of the plugin is executed on trampoline functions
       defined in the previous part, but there is nothing to handle in them and
       the plugin won't touch them so we don't really care  */

    basic_block bb;
    FOR_EACH_BB_FN (bb, func)
    {
      gimple_stmt_iterator gsi;
      for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
      {
        gimple *gs = gsi_stmt (gsi);
        switch (gimple_code (gs))
          {
          case GIMPLE_ASSIGN:
            handle_gimple_assign (GIMPLE_CHECK2<gassign *> (gs));
            break;

          case GIMPLE_CALL:
            handle_gimple_call (GIMPLE_CHECK2<gcall *> (gs));
            break;

          case GIMPLE_RETURN:
            handle_gimple_return (GIMPLE_CHECK2<greturn *> (gs));
            break;

          case GIMPLE_COND:
            handle_gimple_cond (GIMPLE_CHECK2<gcond *> (gs));
            break;

          default:
            break;
          }
      }
    }

    return 0;
  }

  virtual cph_pass *
  clone() override
  {
    /* We do not clone ourselves  */
    return this;
  }
};
}


int
plugin_init (struct plugin_name_args *plugin_info,
                struct plugin_gcc_version *version)
{
  /* The plugin consists of two passes:

     The first pass runs after the IPA passes and
     1. Generates trampoline functions for all the public functions defined in
        this translation unit (trmapoline functions for private functions (i.e.
        static) are generated as needed).
     2. Handles initializations of global variables.

     The second pass runs after the code is lowered to ssa-form and replaces
     function address expressions in the code itself.  */

  register_callback(plugin_info->base_name, PLUGIN_INFO, NULL,
                    &my_gcc_plugin_info);

  register_callback (plugin_info->base_name, PLUGIN_ALL_IPA_PASSES_END,
                     ipa_callback, NULL);

  struct register_pass_info pass_info;

  pass_info.pass = new cph_pass(g);
  pass_info.reference_pass_name = "ssa";
  pass_info.ref_pass_instance_number = 1;
  pass_info.pos_op = PASS_POS_INSERT_AFTER;

  register_callback(plugin_info->base_name, PLUGIN_PASS_MANAGER_SETUP, NULL,
                    &pass_info);

  return 0;
}
