#include <llvm-c/Core.h>
#include "nir/nir.h"
#include "rc_nir_to_llvm.h"
struct rc_nir_context {
    struct rc_llvm_context rc;
    LLVMValueRef *ssa_defs;

    gl_shader_stage stage;

    struct rc_llvm_pointer constant_data;

    struct hash_table *defs;
};

static bool visit_load_const(struct rc_nir_context *ctx, const nir_load_const_instr *instr) {
    LLVMValueRef values[16], value = NULL;
    LLVMTypeRef element_type = LLVMIntTypeInContext(ctx->rc.context, instr->def.bit_size);

    for (unsigned i = 0; i < instr->def.num_components; ++i) {
        switch (instr->def.bit_size) {
            case 32:
                values[i] = LLVMConstInt(element_type, instr->value[i].u32, false);
                LLVMBuildRet(ctx->rc.builder, values[i]);
                break;
            default:
                fprintf(stderr, "unsupported nir load_const bit_size: %d\n", instr->def.bit_size);
                return false;
        }
    }
    if (instr->def.num_components > 1) {
        value = LLVMConstVector(values, instr->def.num_components);
    } else
        value = values[0];

    ctx->ssa_defs[instr->def.index] = value;
    return true;
}

static LLVMValueRef get_src(struct rc_nir_context *nir, nir_src src)
{
    assert(src.is_ssa);
    return nir->ssa_defs[src.ssa->index];
}

static bool visit_intrinsic(struct rc_nir_context *ctx, nir_intrinsic_instr *instr) {
    LLVMValueRef result = NULL;

    switch (instr->intrinsic) {
        default:
            fprintf(stdout, "Unknown intrinsic: ");
            nir_print_instr(&instr->instr, stdout);
            fprintf(stdout, "\n");
            return true;
    }

    if (result) {
      ctx->ssa_defs[instr->dest.ssa.index] = result;
   }
   return true;
}

static bool visit_block(struct rc_nir_context *ctx, nir_block *block) {

    nir_foreach_instr (instr, block) {
        switch(instr->type) {
            case nir_instr_type_load_const:
                if (!visit_load_const(ctx, nir_instr_as_load_const(instr)))
                    return false;
                break;
            case nir_instr_type_intrinsic:
                if (!visit_intrinsic(ctx, nir_instr_as_intrinsic(instr)))
                    return false;
                break;
            case nir_instr_type_deref:
                assert (!nir_deref_mode_is_one_of(nir_instr_as_deref(instr),
                                                  nir_var_mem_shared | nir_var_mem_global));
                break;
            default:
                fprintf(stdout, "Unknown NIR instr type: ");
                nir_print_instr(instr, stdout);
                fprintf(stdout, "\n");
                return true;
        }
    }
    _mesa_hash_table_insert(ctx->defs, block, LLVMGetInsertBlock(ctx->rc.builder));
    return true;
}

static bool visit_cf_list(struct rc_nir_context *ctx, struct exec_list *list) {
    foreach_list_typed(nir_cf_node, node, node, list) {
        switch(node->type) {
            case nir_cf_node_block:
                if (!visit_block(ctx, nir_cf_node_as_block(node))) {
                    return false;
                }
                break;
            default:
                return false;
        }
    }
    return false;
}

static LLVMBasicBlockRef get_block(struct rc_nir_context *nir, const struct nir_block *b)
{
    struct hash_entry *entry = _mesa_hash_table_search(nir->defs, b);
    return (LLVMBasicBlockRef)entry->data;
}

static void visit_post_phi(struct rc_nir_context *ctx, nir_phi_instr *instr, LLVMValueRef llvm_phi)
{
    nir_foreach_phi_src (src, instr) {
        LLVMBasicBlockRef block = get_block(ctx, src->pred);
        LLVMValueRef llvm_src = get_src(ctx, src->src);

        LLVMAddIncoming(llvm_phi, &llvm_src, &block, 1);
    }
}

bool rc_nir_translate(struct rc_llvm_context *rc, struct nir_shader *nir) {
    struct rc_nir_context ctx= {0};
    struct nir_function *func;

    nir_convert_from_ssa(nir, true);
    nir_lower_locals_to_regs(nir);
    nir_remove_dead_derefs(nir);
    nir_remove_dead_variables(nir, nir_var_function_temp, NULL);

    ctx.rc = *rc;
    ctx.stage = nir->info.stage;

    func = (struct nir_function *)exec_list_get_head(&nir->functions);
    nir_index_ssa_defs(func->impl);

    ctx.defs = _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
    ctx.ssa_defs = (LLVMValueRef *)calloc(func->impl->ssa_alloc, sizeof(LLVMValueRef));

    if (!visit_cf_list(&ctx, &func->impl->body))
        return false;

    free(ctx.ssa_defs);
    ralloc_free(ctx.defs);

    return true;
}