#include <llvm-c/Core.h>
#include "nir/nir.h"
#include "rc_nir_to_llvm.h"
struct rc_nir_context {
    struct rc_llvm_context rc;
    LLVMValueRef *ssa_defs;

    gl_shader_stage stage;
    struct rc_llvm_pointer scratch;
    struct rc_llvm_pointer constant_data;

    struct hash_table *defs;
    struct hash_table *phis;
};

static LLVMTypeRef get_def_type(struct rc_nir_context *ctx,const nir_ssa_def *def ) {
    LLVMTypeRef type = LLVMIntTypeInContext(ctx->rc.context, def->bit_size);
    if (def->num_components > 1) {
        type = LLVMVectorType(type, def->num_components);
    }
    return type;
}

static void visit_phi(struct rc_nir_context *ctx, nir_phi_instr *instr) {
    LLVMTypeRef type = get_def_type(ctx, &instr->dest.ssa);
    LLVMValueRef result = LLVMBuildPhi(ctx->rc.builder, type, "");

    ctx->ssa_defs[instr->dest.ssa.index] = result;
    _mesa_hash_table_insert(ctx->phis, instr, result);
}

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

static LLVMValueRef
cast_type(struct rc_nir_context *ctx, LLVMValueRef val,
          nir_alu_type alu_type, unsigned bit_size) {
    LLVMBuilderRef builder = ctx->rc.builder;
    switch (alu_type) {
        case nir_type_float:
            switch (bit_size) {
                case 16:
                    return LLVMBuildBitCast(builder, val, LLVMHalfTypeInContext(ctx->rc.context), "");
                case 32:
                    return LLVMBuildBitCast(builder, val, LLVMFloatTypeInContext(ctx->rc.context), "");
                case 64:
                    return LLVMBuildBitCast(builder, val, LLVMDoubleTypeInContext(ctx->rc.context), "");
                default:
                    assert(0);
                    break;
            }
            break;
        case nir_type_int:
        case nir_type_uint:
            switch (bit_size) {
                case 8:
                    return LLVMBuildBitCast(builder, val, LLVMIntTypeInContext(ctx->rc.context, 8), "");
                case 16:
                    return LLVMBuildBitCast(builder, val, LLVMIntTypeInContext(ctx->rc.context, 16), "");
                case 32:
                    return LLVMBuildBitCast(builder, val, LLVMIntTypeInContext(ctx->rc.context, 32), "");
                case 64:
                    return LLVMBuildBitCast(builder, val, LLVMIntTypeInContext(ctx->rc.context, 64), "");
                default:
                    assert(0);
                    break;
            }
            break;
        default:
            return val;
    }
    return NULL;
}

static LLVMValueRef get_src(struct rc_nir_context *nir, nir_src src)
{
    assert(src.is_ssa);
    return nir->ssa_defs[src.ssa->index];
}

static void
get_deref_offset(struct rc_nir_context *ctx, nir_deref_instr *instr,
        bool vs_in, unsigned *vertex_index_out, LLVMValueRef *vertex_index_ref,
        unsigned *const_out, LLVMValueRef *indir_out) {
#if 0
    LLVMBuilderRef builder = ctx->rc.builder;
    nir_variable *var = nir_deref_instr_get_variable(instr);
    nir_deref_path path;
    unsigned idx_lvl = 1;

    nir_deref_path_init(&path, instr, NULL);

    if ((vertex_index_out != NULL) || (vertex_index_ref != NULL)) {
        if (vertex_index_ref) {
            *vertex_index_ref = get_src(ctx, path.path[idx_lvl]->arr.index);
            if (vertex_index_out)
                *vertex_index_out = 0;
        } else {
            *vertex_index_out = nir_src_as_uint(path.path[idx_lvl]->arr.index);
        }
        ++idx_lvl;
    }

    uint32_t const_offset = 0;
    LLVMValueRef offset = NULL;

    if (var->data.compact && nir_src_is_const(instr->arr.index)) {
        assert(instr->deref_type == nir_deref_type_array);
        const_offset = nir_src_as_uint(instr->arr.index);
        goto out;
    }

    for (; path.path[idx_lvl]; ++idx_lvl) {
        const struct glsl_type *parent_type = path.path[idx_lvl - 1]->type;
        if (path.path[idx_lvl]->deref_type == nir_deref_type_struct) {
            unsigned index = path.path[idx_lvl]->strct.index;

            for (unsigned i = 0; i < index; i++) {
                const struct glsl_type *ft = glsl_get_struct_field(parent_type, i);
                const_offset += glsl_count_attribute_slots(ft, vs_in);
            }
        } else if (path.path[idx_lvl]->deref_type == nir_deref_type_array) {
            unsigned size = glsl_count_attribute_slots(path.path[idx_lvl]->type, vs_in);
            if (nir_src_is_const(path.path[idx_lvl]->arr.index)) {
                const_offset += nir_src_comp_as_int(path.path[idx_lvl]->arr.index, 0) * size;
            } else {
                LLVMValueRef idx_src = get_src(ctx, path.path[idx_lvl]->arr.index);
                idx_src = cast_type(ctx, idx_src, nir_type_uint, 32);
                LLVMValueRef array_off =
            }
        }
    }
#endif
}


static bool visit_intrinsic(struct rc_nir_context *ctx, nir_intrinsic_instr *instr) {
    LLVMValueRef result = NULL;

    switch (instr->intrinsic) {
        case nir_intrinsic_store_deref:
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
    LLVMBasicBlockRef blockref = LLVMGetInsertBlock(ctx->rc.builder);
    LLVMValueRef first = LLVMGetFirstInstruction(blockref);
    if (first) {
        LLVMPositionBuilderBefore(ctx->rc.builder, LLVMGetFirstInstruction(blockref));
    }

    nir_foreach_instr(instr, block) {
        if (instr->type != nir_instr_type_phi)
            break;
        visit_phi(ctx, nir_instr_as_phi(instr));
    }
    LLVMPositionBuilderAtEnd(ctx->rc.builder, blockref);

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

#if 0
static void setup_constant_data(struct rc_nir_context *ctx, struct nir_shader *shader) {
    if (!shader->constant_data)
        return;

    LLVMValueRef data = LLVMConstStringInContext(ctx->rc.context, (const char*)shader->constant_data,
                                                shader->constant_data_size, true);
    LLVMTypeRef type = LLVMArrayType(ctx->rc.i8, shader->constant_data_size);
    LLVMValueRef global =
            LLVMAddGlobalInAddressSpace(ctx->rc.module, type, "const_data", AC_ADDR_SPACE_CONST);
    LLVMSetInitializer(global, data);
    LLVMSetGlobalConstant(global, true);
    LLVMSetVisibility(global, LLVMHiddenVisibility);
    ctx->constant_data = (struct rc_llvm_pointer) {
            .value = global,
            .pointee_type = type
    };
}
#endif

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
static void phi_post_pass(struct rc_nir_context *ctx){
    hash_table_foreach(ctx->phis, entry) {
        visit_post_phi(ctx, (nir_phi_instr *)entry->key, (LLVMValueRef)entry->data);
    }

}

#if 0
static void setup_scratch(struct rc_nir_context *ctx, struct nir_shader *shader)
{
    if (shader->scratch_size == 0)
        return;

    LLVMTypeRef type = LLVMArrayType(ctx->rc.i8, shader->scratch_size);
    ctx->scratch = (struct rc_llvm_pointer) {
            .value = rc_build_alloca_undef(&ctx->rc, type, "scratch"),
            .pointee_type = type
    };
}
#endif

bool rc_nir_translate(struct rc_llvm_context *rc, struct nir_shader *nir) {
    struct rc_nir_context ctx= {0};
    struct nir_function *func;

    ctx.rc = *rc;
    ctx.stage = nir->info.stage;

    func = (struct nir_function *)exec_list_get_head(&nir->functions);
    nir_index_ssa_defs(func->impl);

    ctx.defs = _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
    ctx.ssa_defs = (LLVMValueRef *)calloc(func->impl->ssa_alloc, sizeof(LLVMValueRef));
    ctx.phis = _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);

    //setup_scratch(&ctx, nir);
    //setup_constant_data(&ctx, nir);
    //setup_gds(&ctx, func->impl);

    if (!visit_cf_list(&ctx, &func->impl->body))
        return false;

    phi_post_pass(&ctx);

    free(ctx.ssa_defs);
    ralloc_free(ctx.defs);
    ralloc_free(ctx.phis);
    return true;
}