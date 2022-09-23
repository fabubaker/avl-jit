#include <glib.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <jit/jit.h>
#include <assert.h>

// Parameters
// Number of Nodes
#define TREE_SIZE 10000
// Number of Lookups
#define LOOKUPS 1000
// Random Seed (for GRand)
#define SEED 54783

/** Expose some Glib interfaces **/
typedef struct _GTreeNode  GTreeNode;
typedef int (*FF) (int);

struct _GTree
{
    GTreeNode        *root;
    GCompareDataFunc  key_compare;
    GDestroyNotify    key_destroy_func;
    GDestroyNotify    value_destroy_func;
    gpointer          key_compare_data;
    guint             nnodes;
    gint              ref_count;
};

struct _GTreeNode
{
    gpointer   key;         /* key for this node */
    gpointer   value;       /* value stored at this node */
    GTreeNode *left;        /* left subtree */
    GTreeNode *right;       /* right subtree */
    gint8      balance;     /* height (left) - height (right) */
    guint8     left_child;
    guint8     right_child;
};

/** End Glib interfaces **/

int compare_int(const void *a, const void *b)
{
    return GPOINTER_TO_INT(a)-GPOINTER_TO_INT(b);
}

jit_function_t translate_avl_tree(GTree *avl_tree) {
    GList *stack           = NULL;
    GList *pre_order_stack = NULL;
    GList *list_item       = NULL;

    GTreeNode *node       = NULL;
    GTreeNode *node_left  = NULL;
    GTreeNode *node_right = NULL;

    // Build a hash table to keep track of label str -> label mappings
    GHashTable *label_mappings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    int node_left_key, node_right_key;
    int pop_key   = 0;
    int pop_value = 0;

    jit_value_t ifx;

    jit_context_t context;
    jit_function_t function;

    jit_type_t params[1];
    jit_type_t signature;

    context = jit_context_create();
    jit_context_build_start(context);

    params[0] = jit_type_int;
    signature = jit_type_create_signature(jit_abi_cdecl, jit_type_int, params, 1, 1);

    function = jit_function_create(context, signature);
    jit_type_free(signature);

    // Start building the function body...
    jit_value_t lookup_arg = jit_value_get_param(function, 0);
    jit_label_t *block_label;
    char *block_label_str = NULL;
    jit_value_t node_value;

    // These vars are used to lookup the label hash table.
    char *block_label_str_lookup;
    jit_label_t *block_label_lookup;
    jit_label_t *basic_block_true;
    jit_label_t *basic_block_false;

    // BRNULL block - couldn't find key
    block_label = malloc(sizeof(jit_label_t));
    *block_label = jit_label_undefined;
    block_label_str = g_strdup("BRNULL");
    jit_insn_label(function, block_label);
    jit_value_t negative_one = jit_value_create_nint_constant(function, jit_type_int, -1); // return -1
    jit_insn_return(function, negative_one);
    g_hash_table_insert(label_mappings, block_label_str, block_label);

    // Build the pre-order stack.
    stack = g_list_append(stack, avl_tree->root);
    while (g_list_length(stack) > 0) {
        node = (GTreeNode*) g_list_last(stack)->data;
        stack = g_list_remove(stack, node);
        pre_order_stack = g_list_append(pre_order_stack, node);

        if (node->right_child) stack = g_list_append(stack, node->right);
        if (node->left_child) stack = g_list_append(stack, node->left);
    }

    // Traverse the pre-order stack and emit instructions.
    while((list_item = g_list_last(pre_order_stack))) {
        node = (GTreeNode*) list_item->data;
        pre_order_stack = g_list_remove(pre_order_stack, node);
        pop_key = GPOINTER_TO_INT(node->key);
        pop_value = GPOINTER_TO_INT(node->value);

        // Create the EQX label ---------------------------------
        block_label = malloc(sizeof(jit_label_t));
        *block_label = jit_label_undefined;
        block_label_str = g_strdup_printf("EQ%d", pop_key);
        jit_insn_label(function, block_label);
        node_value = jit_value_create_nint_constant(function, jit_type_int, pop_value);
        jit_insn_return(function, node_value);
        g_hash_table_insert(label_mappings, block_label_str, block_label);

        // Create the DIFX label ----------------------------------
        block_label = malloc(sizeof(jit_label_t));
        *block_label = jit_label_undefined;
        block_label_str = g_strdup_printf("DIF%d", pop_key);
        jit_insn_label(function, block_label);
        g_hash_table_insert(label_mappings, block_label_str, block_label);

        // It's a leaf node
        if((!node->right_child) && (!node->left_child))
        {
            // Get the BRNULL
            block_label_str_lookup = g_strdup("BRNULL");
            block_label_lookup = (jit_label_t *) g_hash_table_lookup(label_mappings, block_label_str_lookup);
            assert(block_label_lookup != NULL);
            g_free(block_label_str_lookup);
            jit_insn_branch(function, block_label_lookup);
        }
        else { // It's an internal node
            if (node->right_child) { // We have a "greater than" node
                node_right = node->right;
                node_right_key = GPOINTER_TO_INT(node_right->key);
                block_label_str_lookup = g_strdup_printf("BR%d", node_right_key);
                basic_block_true = (jit_label_t *) g_hash_table_lookup(label_mappings, block_label_str_lookup);
                assert(basic_block_true != NULL);
                g_free(block_label_str_lookup);
            } else { // We do not have a "greater than" node
                // Get the BRNULL
                block_label_str_lookup = g_strdup("BRNULL");
                basic_block_true = (jit_label_t *) g_hash_table_lookup(label_mappings, block_label_str_lookup);
                assert(basic_block_true != NULL);
                g_free(block_label_str_lookup);
            }

            if (node->left_child) { // We have a "less than" node
                node_left = node->left;
                node_left_key = GPOINTER_TO_INT(node_left->key);
                block_label_str_lookup = g_strdup_printf("BR%d", node_left_key);
                basic_block_false = (jit_label_t *) g_hash_table_lookup(label_mappings, block_label_str_lookup);
                assert(basic_block_false != NULL);
                g_free(block_label_str_lookup);
            } else { // We do not have a "less than" node
                // Get the BRNULL
                block_label_str_lookup = g_strdup("BRNULL");
                basic_block_false = (jit_label_t *) g_hash_table_lookup(label_mappings, block_label_str_lookup);
                assert(basic_block_false != NULL);
                g_free(block_label_str_lookup);
            }

            // Compare with lookup_arg and branch accordingly
            ifx = jit_insn_gt(function, lookup_arg, jit_value_create_nint_constant(function, jit_type_int, pop_key));
            jit_insn_branch_if(function, ifx, basic_block_true);
            jit_insn_branch_if_not(function, ifx, basic_block_false);
        }

        // Create the BRX label -----------------------------------------
        if (g_list_length(pre_order_stack) == 0) {
            // It's the root node
            // ENTRY block
            block_label = malloc(sizeof(jit_label_t));
            *block_label = jit_label_undefined;
            block_label_str = g_strdup("ENTRY");
            jit_insn_label(function, block_label);
            g_hash_table_insert(label_mappings, block_label_str, block_label);
        } else {
            block_label = malloc(sizeof(jit_label_t));
            *block_label = jit_label_undefined;
            block_label_str = g_strdup_printf("BR%d", pop_key);
            jit_insn_label(function, block_label);
            g_hash_table_insert(label_mappings, block_label_str, block_label);
        }

        block_label_str = g_strdup_printf("EQ%d", pop_key);
        basic_block_true = g_hash_table_lookup(label_mappings, block_label_str);
        assert(basic_block_true != NULL);
        g_free(block_label_str);

        block_label_str = g_strdup_printf("DIF%d", pop_key);
        basic_block_false = g_hash_table_lookup(label_mappings, block_label_str);
        assert(basic_block_false != NULL);
        g_free(block_label_str);

        ifx = jit_insn_eq(function, lookup_arg, jit_value_create_nint_constant(function, jit_type_int, pop_key));
        jit_insn_branch_if(function, ifx, basic_block_true);
        jit_insn_branch_if_not(function, ifx, basic_block_false);

        if (g_list_length(pre_order_stack) == 0) {
            // Move the entry block to the start of the function.
            jit_label_t final_label = jit_label_undefined;
            jit_insn_label(function, &final_label);
            jit_insn_nop(function);
            jit_insn_move_blocks_to_start(function, *block_label, final_label);
        }
    }

    // Uncomment this function to see the compiled IR.
    // jit_dump_function(stdout, function, "translate_avl_tree [uncompiled]");
    jit_function_compile(function);
    jit_context_build_end(context);

    return function;
}

int main() {
    double nonjit_elapsed = 0.0;
    double jit_elapsed = 0.0;
    double compile_elapsed = 0.0;
    clock_t t0, t1;
    GTree *avl_tree = NULL;
    GRand *grand;

    grand = g_rand_new_with_seed(SEED);
    printf("\nCreating tree of size %d...\n", TREE_SIZE);
    avl_tree = g_tree_new_full((GCompareDataFunc) compare_int,
                               NULL, NULL, NULL);

    for(int i = 0; i < TREE_SIZE; i++)
        g_tree_insert(avl_tree, GINT_TO_POINTER(i), GINT_TO_POINTER(i));

    printf("Tree info:\n");
    printf("Tree Height = %d\n", g_tree_height(avl_tree));
    printf("Tree Nodes  = %d\n", g_tree_nnodes(avl_tree));

    printf("Generating %d lookups...\n", LOOKUPS);
    int* lookups_array = malloc(sizeof(int) * LOOKUPS);
    for (int i = 0; i < LOOKUPS; i++) {
        lookups_array[i] = g_rand_int_range(grand, 0, TREE_SIZE);
    }

    printf("Doing %d lookups...\n", LOOKUPS);
    t0 = clock();
    for (int i = 0; i < LOOKUPS; i++) {
        g_tree_lookup(avl_tree, GINT_TO_POINTER(lookups_array[i]));
    }
    t1 = clock();
    nonjit_elapsed = ((double) t1-t0)/CLOCKS_PER_SEC;
    printf("Non-JIT took %.6f seconds !\n", nonjit_elapsed);

    jit_function_t avl_tree_lookup_function;

    t0 = clock();
    printf("JIT-ing AVL tree...\n");
    avl_tree_lookup_function = translate_avl_tree(avl_tree);
    FF compiled_lookup = jit_function_to_closure(avl_tree_lookup_function);
    t1 = clock();
    compile_elapsed = ((double) t1-t0)/CLOCKS_PER_SEC;
    printf("JIT Compile time took %.6f seconds !\n", compile_elapsed);

    t0 = clock();
    for (int i = 0; i < LOOKUPS; i++) {
        compiled_lookup(lookups_array[i]);
    }
    t1 = clock();
    jit_elapsed = ((double) t1-t0)/CLOCKS_PER_SEC;
    printf("JIT took %.6f seconds !\n", jit_elapsed);
}