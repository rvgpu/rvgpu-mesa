#include <llvm-c/Core.h>
#include "nir/nir.h"
#include "nir/nir_deref.h"
#include "rc_nir_to_llvm.h"
#include "rc_build_type.h"
#include "rc_llvm_build_arit.h"
#include "rc_bld_flow.h"

#define NUM_CHANNELS 4 // same with TGSI_NUM_CHANNELS
#define MAX_SHADER_OUTPUTS 80  //same with PIPE_MAX_SHADER_OUTPUTS
struct rc_shader_abi {
    LLVMValueRef vertex_id;
};

struct rc_nir_context {
    struct rc_llvm_context rc;
    struct rc_shader_abi abi;
    LLVMValueRef *ssa_defs;
    LLVMValueRef outputs[MAX_SHADER_OUTPUTS][NUM_CHANNELS];

    gl_shader_stage stage;

    struct rc_llvm_pointer constant_data;

    struct hash_table *defs;
    struct hash_table *regs;
    struct hash_table *vars;

    struct rc_build_context base;
    struct rc_build_context uint_bld;
    struct rc_build_context int_bld;
    struct rc_build_context uint8_bld;
    struct rc_build_context int8_bld;
    struct rc_build_context uint16_bld;
    struct rc_build_context int16_bld;
    struct rc_build_context half_bld;
    struct rc_build_context dbl_bld;
    struct rc_build_context uint64_bld;
    struct rc_build_context int64_bld;

};

static bool visit_load_const(struct rc_nir_context *ctx, const nir_load_const_instr *instr) {

    LLVMValueRef values[NIR_MAX_VEC_COMPONENTS];
    LLVMTypeRef element_type = LLVMIntTypeInContext(ctx->rc.context, instr->def.bit_size);

    for (unsigned i = 0; i < instr->def.num_components; ++i) {
        switch (instr->def.bit_size) {
            case 32:
                values[i] = LLVMConstInt(element_type, instr->value[i].u32, false);
                break;
            default:
                fprintf(stderr, "unsupported nir load_const bit_size: %d\n", instr->def.bit_size);
                return false;
        }
    }

    assign_ssa_dest(ctx, &instr->def, values);

    return true;
}

static LLVMValueRef
rc_nir_array_build_gather_values(LLVMBuilderRef builder, LLVMValueRef *values, unsigned value_count) {
    LLVMTypeRef arr_type = LLVMArrayType(LLVMTypeOf(values[0]), value_count);
    LLVMValueRef arr = LLVMGetUndef(arr_type);
    for (unsigned i = 0; i < value_count; i++) {
        arr = LLVMBuildInsertValue(builder, arr, values[i], i, "");
    }
    return arr;
}

static inline struct rc_build_context *
get_int_bld(struct rc_nir_context *ctx, bool is_unsigned, unsigned op_bit_size) {
    if (is_unsigned) {
        switch (op_bit_size) {
            case 64:
                return &ctx->uint64_bld;
            case 32:
            default:
                return &ctx->uint_bld;
            case 16:
                return &ctx->uint16_bld;
            case 8:
                return &ctx->uint8_bld;
        }
    } else {
        switch (op_bit_size) {
            case 64:
                return &ctx->int64_bld;
            case 32:
            default:
                return &ctx->int_bld;
            case 16:
                return &ctx->uint16_bld;
            case 8:
                return &ctx->int8_bld;
        }
    }
}

static LLVMValueRef get_reg_src(struct rc_nir_context *ctx, nir_reg_src src);

static LLVMValueRef get_src(struct rc_nir_context *nir, nir_src src) {
    if (src.is_ssa)
        return nir->ssa_defs[src.ssa->index];
    else
        return get_reg_src(nir, src.reg);
}

static void assign_reg(struct rc_nir_context *ctx, const nir_reg_dest *reg,
                       unsigned write_mask, LLVMValueRef vals[NIR_MAX_VEC_COMPONENTS]);

static void assign_dest(struct rc_nir_context *ctx, const nir_dest *dest, LLVMValueRef vals[NIR_MAX_VEC_COMPONENTS]) {
    if (dest->is_ssa)
        assign_ssa_dest(ctx, &dest->ssa, vals);
    else
        assign_reg(ctx, &dest->reg, WRITE_MASK, vals);
}

static LLVMValueRef
cast_type(struct rc_nir_context *ctx, LLVMValueRef val,
          nir_alu_type alu_type, unsigned bit_size) {
    LLVMBuilderRef builder = ctx->rc.builder;
    switch (alu_type) {
        case nir_type_float:
            switch (bit_size) {
                case 16:
                    return LLVMBuildBitCast(builder, val, ctx->half_bld.vec_type, "");
                case 32:
                    return LLVMBuildBitCast(builder, val, ctx->base.vec_type, "");
                case 64:
                    return LLVMBuildBitCast(builder, val, ctx->dbl_bld.vec_type, "");
                default:
                    assert(0);
                    break;
            }
            break;
        case nir_type_int:
            switch (bit_size) {
                case 8:
                    return LLVMBuildBitCast(builder, val, ctx->int8_bld.vec_type, "");
                case 16:
                    return LLVMBuildBitCast(builder, val, ctx->int16_bld.vec_type, "");
                case 32:
                    return LLVMBuildBitCast(builder, val, ctx->int_bld.vec_type, "");
                case 64:
                    return LLVMBuildBitCast(builder, val, ctx->int64_bld.vec_type, "");
                default:
                    assert(0);
                    break;
            }
            break;
        case nir_type_uint:
            switch (bit_size) {
                case 8:
                    return LLVMBuildBitCast(builder, val, ctx->uint8_bld.vec_type, "");
                case 16:
                    return LLVMBuildBitCast(builder, val, ctx->uint16_bld.vec_type, "");
                case 1:
                case 32:
                    return LLVMBuildBitCast(builder, val, ctx->uint_bld.vec_type, "");
                case 64:
                    return LLVMBuildBitCast(builder, val, ctx->uint64_bld.vec_type, "");
                default:
                    assert(0);
                    break;
            }
            break;
        case nir_type_uint32:
            return LLVMBuildBitCast(builder, val, ctx->uint_bld.vec_type, "");
        default:
            return val;
    }
    return NULL;
}

static void get_deref_offset(struct rc_nir_context *ctx, nir_deref_instr *instr,
                             bool vs_in, unsigned *vertex_index_out,
                             LLVMValueRef *vertex_index_ref,
                             unsigned *const_out, LLVMValueRef *indir_out) {
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
        }else if (path.path[idx_lvl]->deref_type == nir_deref_type_array) {
            unsigned size = glsl_count_attribute_slots(path.path[idx_lvl]->type, vs_in);
            if (nir_src_is_const(path.path[idx_lvl]->arr.index)) {
                const_offset += nir_src_comp_as_int(path.path[idx_lvl]->arr.index, 0) * size;
            } else {
                LLVMValueRef idx_src = get_src(ctx, path.path[idx_lvl]->arr.index);
                idx_src = cast_type(ctx, idx_src, nir_type_uint, 32);
                LLVMValueRef array_off = rc_build_mul(&ctx->uint_bld, rc_build_const_int_vec(&ctx->rc, ctx->base.type, size),
                                                      idx_src);
                if (offset) {
                    offset = rc_build_add(&ctx->uint_bld, offset, array_off);
                } else {
                    offset = array_off;
                }
            }
        } else {
            unreachable("Uhandled deref type in get_deref_instr_offset");
        }
    }
out:
    nir_deref_path_finish(&path);

    if (const_offset && offset)
        offset = LLVMBuildAdd(ctx->rc.builder, offset,
                              rc_build_const_int_vec(ctx->base.rc, ctx->uint_bld.type, const_offset),
                              "");
    *const_out = const_offset;
    *indir_out = offset;
}

static void store_chan(struct rc_nir_context *ctx, nir_variable_mode deref_mode,
                       unsigned bit_size, unsigned location, unsigned comp,
                       unsigned chan, LLVMValueRef dst) {
    if (bit_size == 64) {
        chan *= 2;
        chan += comp;
        if (chan >= 4) {
            chan -= 4;
            location++;
        }
        //store_64bit_chan(ctx, ctx->outputs[location][chan],
         //                ctx->outputs[location][chan + 1], dst);
    } else {
        dst = LLVMBuildBitCast(ctx->rc.builder, dst, ctx->base.vec_type, "");
        LLVMBuildStore(ctx->rc.builder, dst, ctx->outputs[location][chan + comp]);
    }
}

static void store_var(struct rc_nir_context *ctx, nir_variable_mode deref_mode,
                      unsigned num_components,  unsigned bit_size,
                      nir_variable *var, unsigned writemask,
                      LLVMValueRef indir_vertex_index, unsigned const_index,
                      LLVMValueRef indir_index, LLVMValueRef dst) {
    unsigned location = var->data.driver_location;
    unsigned comp = var->data.location_frac;

    switch (deref_mode) {
        case nir_var_shader_out: {
            if (ctx->stage == MESA_SHADER_FRAGMENT) {
                if (var->data.location == FRAG_RESULT_STENCIL) {
                    comp = 1;
                } else if (var->data.location == FRAG_RESULT_DEPTH) {
                    comp = 2;
                }
            }
            if (var->data.compact) {
                location += const_index / 4;
                comp += const_index % 4;
                const_index = 0;
            }

            for (unsigned chan = 0; chan < num_components; chan++) {
                if (writemask & (1u << chan)) {
                    LLVMValueRef chan_val = (num_components == 1) ? dst :
                                            LLVMBuildExtractValue(ctx->rc.builder, dst, chan, "");
                    //if (bld->tcs_iface){
                        //TODO
                   // } else {
                        store_chan(ctx, deref_mode, bit_size, location + const_index, comp, chan, chan_val);
                   // }
                }
            }
        }
            break;
        default:
            break;
    }
}

static bool compact_array_index_oob(struct rc_nir_context *ctx,  nir_variable *var, const uint32_t index) {
    auto type = var->type;
    if (nir_is_arrayed_io(var, ctx->stage)) {
        assert(glsl_type_is_array(type));
        type = glsl_get_array_element(type);
    }
    return index >= glsl_get_length(type);
}

static void visit_store_var(struct rc_nir_context *ctx, nir_intrinsic_instr *instr) {
    nir_deref_instr *deref = nir_instr_as_deref(instr->src[0].ssa->parent_instr);
    nir_variable *var = nir_deref_instr_get_variable(deref);
    assert(util_bitcount(deref->modes) == 1);
    nir_variable_mode mode = deref->modes;
    int writemask = instr->const_index[0];
    unsigned bit_size = nir_src_bit_size(instr->src[1]);
    LLVMValueRef src = get_src(ctx, instr->src[1]);

    unsigned const_index = 0;
    LLVMValueRef indir_index, indir_vertex_index = NULL;

    if (var) {
        bool tsc_out = (ctx->stage == MESA_SHADER_TESS_CTRL) &&
                (var->data.mode == nir_var_shader_out) &&
                !var->data.patch;
        get_deref_offset(ctx, deref, false, NULL,
                         tsc_out ? &indir_vertex_index : NULL,
                         &const_index, &indir_index);
        if (var->data.compact && compact_array_index_oob(ctx, var, const_index)) {
            return;
        }
    }
    store_var(ctx, mode, instr->num_components, bit_size,
              var, writemask, indir_vertex_index, const_index,
              indir_index, src);
}

static bool visit_intrinsic(struct rc_nir_context *ctx, nir_intrinsic_instr *instr) {
    LLVMValueRef result[NIR_MAX_VEC_COMPONENTS] = {0};

    switch (instr->intrinsic) {
        case nir_intrinsic_load_vertex_id:
            printf("nir: load vertex id \n");
            result[0] = ctx->abi.vertex_id;
            break;
        case nir_intrinsic_store_deref:
            printf("nir: store deref \n");
            visit_store_var(ctx, instr);
            break;
        default:
            fprintf(stdout, "Unknown intrinsic: ");
            nir_print_instr(&instr->instr, stdout);
            fprintf(stdout, "\n");
            return true;
    }

    if (result[0]) {
        assign_dest(ctx, &instr->dest, result);
    }
    return true;
}

static LLVMValueRef
get_alu_src(struct rc_nir_context *ctx, nir_alu_src src, unsigned num_components) {
    assert(!src.negate);
    assert(!src.abs);
    assert(num_components >= 1);
    assert(num_components <= 4);

    const unsigned src_components = nir_src_num_components(src.src);
    assert(src_components > 0);
    LLVMValueRef value = get_src(ctx, src.src);
#if 0
    printf("[get_alu_src] get src %s from ssa_%d \n", LLVMPrintValueToString(value), src.src.ssa->index);
#endif
    assert(value);

    bool need_swizzle = false;
    for (unsigned i = 0; i < src_components; ++i) {
        if (src.swizzle[i] != i) {
            need_swizzle = true;
            break;
        }
    }
    if (need_swizzle || (num_components != src_components)) {
        if ((src_components > 1) && (num_components == 1)) {
            value = LLVMBuildExtractValue(ctx->rc.builder, value, src.swizzle[0], "");
        } else if ((src_components == 1) && (num_components > 1)) {
            printf("TODO: get src number components > 1 \n");
        }else {
            LLVMValueRef arr = LLVMGetUndef(LLVMArrayType(LLVMTypeOf(LLVMBuildExtractValue(ctx->rc.builder, value, 0, "")), num_components));
            for (unsigned i = 0; i < num_components; i++) {
                arr = LLVMBuildInsertValue(ctx->rc.builder, arr, LLVMBuildExtractValue(ctx->rc.builder, value, src.swizzle[i], ""), i, "");
            }
            value = arr;
        }
    }
    return value;
}

unsigned
get_alu_src_components(const nir_alu_instr *instr) {
    unsigned src_components = 0;

    switch (instr->op) {
        case nir_op_vec2:
        case nir_op_vec3:
        case nir_op_vec4:
        case nir_op_vec8:
        case nir_op_vec16:
            src_components = 1;
            break;
        case nir_op_pack_half_2x16:
            src_components = 2;
            break;
        case nir_op_unpack_half_2x16:
            src_components = 1;
            break;
        case nir_op_cube_face_coord_amd:
        case nir_op_cube_face_index_amd:
            src_components = 3;
            break;
        case nir_op_fsum2:
        case nir_op_fsum3:
        case nir_op_fsum4:
            src_components = nir_op_infos[instr->op].input_sizes[0];
            break;
        default:
            src_components = nir_dest_num_components(instr->dest.dest);
            break;
    }
    return src_components;
}

static LLVMValueRef
do_alu_action(struct rc_nir_context *ctx, const nir_alu_instr *instr,
              unsigned src_bit_size[NIR_MAX_VEC_COMPONENTS], LLVMValueRef src[NIR_MAX_VEC_COMPONENTS]) {
    LLVMValueRef result;
    switch (instr->op) {
        case nir_op_mov:
            printf("mov op \n");
            result = src[0];
            break;
        case nir_op_iadd:
            printf("iadd op \n");
            result = rc_build_add(get_int_bld(ctx, false, src_bit_size[0]), src[0], src[1]);
            break;
        default:
            fprintf(stdout, "Unknown alu instr op: %d\n", instr->op);
            fprintf(stdout, "\n");
            assert(0);
            break;
    }
    return result;
}

void assign_ssa_dest(struct rc_nir_context *ctx, const nir_ssa_def *ssa,
                     LLVMValueRef vals[NIR_MAX_VEC_COMPONENTS]) {
    if (ssa->num_components == 1) {
        ctx->ssa_defs[ssa->index] = vals[0];
#if 0
        printf("[assign_ssa_dest] write res = %s to ssa_%d \n", LLVMPrintValueToString(vals[0]), ssa->index);
#endif
    } else {

        LLVMValueRef res = rc_nir_array_build_gather_values(ctx->rc.builder, vals,
                                                            ssa->num_components);
#if 0
        printf("[assign_ssa_dest] write res = %s to ssa_%d \n", LLVMPrintValueToString(res), ssa->index);
#endif
        ctx->ssa_defs[ssa->index] = res;
    }
}

static LLVMValueRef build_array_get_ptr2(struct rc_llvm_context *rc, LLVMTypeRef array_type,
                                         LLVMValueRef ptr, LLVMValueRef index) {
    LLVMValueRef indices[2];
    LLVMValueRef element_ptr;
    assert(LLVMGetTypeKind(LLVMTypeOf(ptr)) == LLVMPointerTypeKind);
    // assert(LLVM_VERSION_MAJOR >= 15 || LLVMGetTypeKind(LLVMGetElementType(LLVMTypeOf(ptr))) == LLVMArrayTypeKind);
    indices[0] = LLVMConstInt(LLVMInt32TypeInContext(rc->context), 0, 0);
    indices[1] = index;
    element_ptr = LLVMBuildGEP2(rc->builder, array_type, ptr, indices, ARRAY_SIZE(indices), "");
    return element_ptr;
}

static LLVMValueRef reg_chan_pointer(struct rc_nir_context *ctx,
                                     struct rc_build_context *reg_bld,
                                     const nir_register *reg,
                                     LLVMValueRef reg_storage,
                                     int array_index, int chan) {
    int nc = reg->num_components;
    LLVMTypeRef chan_type = reg_bld->vec_type;
    if (nc > 1)
        chan_type = LLVMArrayType(chan_type, nc);

    if (reg->num_array_elems > 0) {
        LLVMTypeRef array_type = LLVMArrayType(chan_type, reg->num_array_elems);
        reg_storage = build_array_get_ptr2(&ctx->rc, array_type, reg_storage,
                                           LLVMConstInt(LLVMInt32TypeInContext(ctx->rc.context), array_index, 0));
    }
    if (nc > 1) {
        reg_storage = build_array_get_ptr2(&ctx->rc, chan_type, reg_storage,
                                           LLVMConstInt(LLVMInt32TypeInContext(ctx->rc.context), chan, 0));
    }
    return reg_storage;
}

static LLVMValueRef
get_soa_array_offsets(struct rc_build_context *uint_bld, LLVMValueRef indirect_index,
                        int num_components, unsigned chan_index, bool need_perelement_offset) {
    LLVMValueRef chan_vec = rc_build_const_int_vec(uint_bld->rc, uint_bld->type, chan_index);
    LLVMValueRef length_vec = rc_build_const_int_vec(uint_bld->rc, uint_bld->type, uint_bld->type.length);
    LLVMValueRef index_vec;

    /* index_vec = (indirect_index * num_components + chan_index) * length + offsets */
    index_vec = rc_build_mul(uint_bld, indirect_index, rc_build_const_int_vec(uint_bld->rc, uint_bld->type, num_components));
    index_vec = rc_build_add(uint_bld, index_vec, chan_vec);
    index_vec = rc_build_mul(uint_bld, index_vec, length_vec);

    if (need_perelement_offset) {
        LLVMValueRef pixel_offsets = uint_bld->undef;

        for (auto i = 0; i < uint_bld->type.length; i++) {
            LLVMValueRef ii = LLVMConstInt(LLVMInt32TypeInContext(uint_bld->rc->context), i, 0);
            pixel_offsets = LLVMBuildInsertElement(uint_bld->rc->builder, pixel_offsets, ii, ii, "");
        }
        index_vec = rc_build_add(uint_bld, index_vec, pixel_offsets);
    }

    return index_vec;
}

static LLVMValueRef build_gather(struct rc_nir_context *ctx, struct rc_build_context *bld,
                                 LLVMTypeRef base_type, LLVMValueRef base_ptr,
                                 LLVMValueRef indexes, LLVMValueRef overflow_mask, LLVMValueRef indexes2) {

    LLVMValueRef res;
#if 0
    if (indexes2)
        res = LLVMGetUndef(LLVMVectorType(LLVMFloatTypeInContext(ctx->rc.context), ctx->base.type.length * 2));
    else
        res = bld->undef;

    if (overflow_mask) {
        indexes = rc_build_select(&ctx->uint_bld, overflow_mask, ctx->uint_bld.zero, indexes);
        if (indexes2)
            indexes2 = rc_build_select(&ctx->uint_bld, overflow_mask, ctx->uint_bld.zero, indexes2);
    }

    printf("bld->type.length = %d \n", bld->type.length);
    for (auto i = 0; i < bld->type.length * (indexes2 ? 2 : 1); i++) {
        LLVMValueRef si, di;
        LLVMValueRef index;
        LLVMValueRef scalar_ptr, scalar;

        di = LLVMConstInt(LLVMInt32TypeInContext(ctx->rc.context), i, 0);
        if (indexes2)
            si = LLVMConstInt(LLVMInt32TypeInContext(ctx->rc.context), i >> 1, 0);
        else
            si = di;

        printf("[build_gather] di = %s , indexes = %s\n", LLVMPrintValueToString(di), LLVMPrintValueToString(indexes));
        if (indexes2 && (i & 1)) {
            index = LLVMBuildExtractElement(ctx->rc.builder, indexes2, si, "");
        } else {
            index = LLVMBuildExtractElement(ctx->rc.builder, indexes, si, "");
        }

        scalar_ptr = LLVMBuildGEP2(ctx->rc.builder, base_type, base_ptr, &index, 1, "gather_ptr");
        scalar = LLVMBuildLoad2(ctx->rc.builder, base_type, scalar_ptr, "");

        res = LLVMBuildInsertElement(ctx->rc.builder, res, scalar, di, "");
    }

    if (overflow_mask) {
        if (indexes2) {
            res = LLVMBuildBitCast(ctx->rc.builder, res, ctx->dbl_bld.vec_type, "");
            overflow_mask = LLVMBuildSExt(ctx->rc.builder, overflow_mask,
                                          ctx->dbl_bld.int_vec_type, "");
            res = rc_build_select(&ctx->dbl_bld, overflow_mask, ctx->dbl_bld.zero, res);
        } else {
            res = rc_build_select(bld, overflow_mask, bld->zero, res);
        }
    }
#endif
    LLVMValueRef scalar_ptr;
    scalar_ptr = LLVMBuildGEP2(ctx->rc.builder, base_type, base_ptr, &indexes, 1, "gather_ptr");
    res = LLVMBuildLoad2(ctx->rc.builder, base_type, scalar_ptr, "");
    return res;
}

static LLVMValueRef load_reg(struct rc_nir_context *ctx, struct rc_build_context *reg_bld,
                            const nir_reg_src *reg, LLVMValueRef indir_src, LLVMValueRef reg_storage) {
    int nc = reg->reg->num_components;
    LLVMValueRef vals[NIR_MAX_VEC_COMPONENTS] = {NULL};
    if (reg->indirect) {
        LLVMValueRef indirect_val = rc_build_const_int_vec(&ctx->rc, ctx->uint_bld.type, reg->base_offset);
        LLVMValueRef max_index = rc_build_const_int_vec(&ctx->rc, ctx->uint_bld.type, reg->reg->num_array_elems - 1);
        indirect_val = LLVMBuildAdd(ctx->rc.builder, indirect_val, indir_src, "");

        indirect_val = rc_build_min(&ctx->uint_bld, indirect_val, max_index);
        reg_storage = LLVMBuildBitCast(ctx->rc.builder, reg_storage, LLVMPointerType(reg_bld->elem_type, 0), "");

        for (auto i = 0; i < nc; i++) {
            //LLVMValueRef indirect_offset = get_soa_array_offsets(&ctx->uint_bld, indirect_val, nc, i, false);
            //vals[i] = build_gather(ctx, reg_bld, reg_bld->elem_type,reg_storage, indirect_offset, NULL, NULL);

            vals[i] = LLVMBuildLoad2(ctx->rc.builder, reg_bld->vec_type,
                                     reg_chan_pointer(ctx, reg_bld, reg->reg, reg_storage,
                                                      i, i), "");
        }
    } else {
        for (auto i = 0; i < nc; i++) {
            vals[i] = LLVMBuildLoad2(ctx->rc.builder, reg_bld->vec_type,
                                     reg_chan_pointer(ctx, reg_bld, reg->reg, reg_storage,
                                                      reg->base_offset, i), "");
        }
    }
     return nc == 1 ? vals[0] : rc_nir_array_build_gather_values(ctx->rc.builder, vals, nc);
}

static LLVMValueRef get_reg_src(struct rc_nir_context *ctx, nir_reg_src src) {
    struct hash_entry *entry = _mesa_hash_table_search(ctx->regs, src.reg);
    LLVMValueRef reg_storage = (LLVMValueRef) entry->data;
    struct rc_build_context *reg_bld = get_int_bld(ctx, true, src.reg->bit_size);
    LLVMValueRef indir_src = NULL;
    if (src.indirect) {
        indir_src = get_src(ctx, *src.indirect);
    }
    return load_reg(ctx, reg_bld, &src, indir_src, reg_storage);
}

static void assign_reg(struct rc_nir_context *ctx, const nir_reg_dest *reg,
                       unsigned write_mask, LLVMValueRef vals[NIR_MAX_VEC_COMPONENTS]) {
    assert(write_mask != 0x0);
    struct hash_entry *entry = _mesa_hash_table_search(ctx->regs, reg->reg);
    LLVMValueRef reg_storage = (LLVMValueRef) entry->data;
    auto reg_bld = get_int_bld(ctx, true, reg->reg->bit_size);
    LLVMValueRef indir_src = NULL;
    if (reg->indirect) {
        indir_src = get_src(ctx, *reg->indirect);
        //TODO: emit_store_reg finction in lp_bld_nir_soa.c
        printf("TODO: store indirect reg \n");
    }

    for (unsigned int i = 0; i < reg->reg->num_components; i++) {
        if (!(write_mask & (1 << i))) {
            continue;
        }
        vals[i] = LLVMBuildBitCast(ctx->rc.builder, vals[i], reg_bld->vec_type, "");
        auto dst_ptr = reg_chan_pointer(ctx, reg_bld, reg->reg, reg_storage,
                                        reg->base_offset, i);
#if 0
        printf("[assign_reg] write res = %s to reg_%d, offset = %d\n",
               LLVMPrintValueToString(vals[i]), reg->reg->index, reg->base_offset);
#endif
        LLVMBuildStore(ctx->rc.builder, vals[i], dst_ptr);
    }
}

static void assign_alu_dest(struct rc_nir_context *ctx,
                            const nir_alu_dest *dest,
                            LLVMValueRef vals[NIR_MAX_VEC_COMPONENTS]) {
    if (dest->dest.is_ssa) {
        assign_ssa_dest(ctx, &dest->dest.ssa, vals);
    } else {
        assign_reg(ctx, &dest->dest.reg, dest->write_mask, vals);
    }
}

static void
visit_alu(struct rc_nir_context *ctx, const nir_alu_instr *instr) {
    LLVMValueRef src[NIR_MAX_VEC_COMPONENTS];
    unsigned src_bit_size[NIR_MAX_VEC_COMPONENTS];
    const unsigned num_components = nir_dest_num_components(instr->dest.dest);
    unsigned src_components = get_alu_src_components(instr);

    for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
        src[i] = get_alu_src(ctx, instr->src[i], src_components);
        src_bit_size[i] = nir_src_bit_size(instr->src[i].src);
    }

    LLVMValueRef result[NIR_MAX_VEC_COMPONENTS];
    if (instr->op == nir_op_vec4 ||
        instr->op == nir_op_vec3 ||
        instr->op == nir_op_vec2 ||
        instr->op == nir_op_vec8 ||
        instr->op == nir_op_vec16) {
        if (        instr->op == nir_op_vec3 ||
                    instr->op == nir_op_vec2 ||
                    instr->op == nir_op_vec8 ||
                    instr->op == nir_op_vec16) {
            printf("TODO: alu vec op \n");
        }
        for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
            result[i] = cast_type(ctx, src[i],
                                  nir_op_infos[instr->op].input_types[i],
                                  src_bit_size[i]);
        }

    } else if (instr->op == nir_op_fsum4 ||
               instr->op == nir_op_fsum3 ||
               instr->op == nir_op_fsum2) {
        //TODO: see lp_bld_nir.c visit_alu function
    } else {
        for (unsigned c = 0; c < num_components; c++) {
            LLVMValueRef src_chan[NIR_MAX_VEC_COMPONENTS];

            for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
                if (num_components > 1) {
                    src_chan[i] = LLVMBuildExtractValue(ctx->rc.builder, src[i], c, "");
                } else {
                    src_chan[i] = src[i];
                }
                src_chan[i] = cast_type(ctx, src_chan[i], nir_op_infos[instr->op].input_types[i], src_bit_size[i]);
            }
            result[c] = do_alu_action(ctx, instr, src_bit_size, src_chan);
            result[c] = cast_type(ctx, result[c], nir_op_infos[instr->op].output_type,
                                  nir_dest_bit_size(instr->dest.dest));
        }
    }

    assign_alu_dest(ctx, &instr->dest, result);
}

static void
assign_ssa(struct rc_nir_context *ctx, int idx, LLVMValueRef ptr)
{
    ctx->ssa_defs[idx] = ptr;
}

static void visit_deref(struct rc_nir_context *ctx, nir_deref_instr *instr) {
    if (!nir_deref_mode_is_one_of(instr, nir_var_mem_shared | nir_var_mem_global)) {
        return;
    }
    LLVMValueRef result = NULL;
    switch(instr->deref_type) {
        case nir_deref_type_var: {
            struct hash_entry *entry = _mesa_hash_table_search(ctx->vars, instr->var);
            result = (LLVMValueRef)entry->data;
            break;
        }
        default:
            unreachable("unexpected alu type");
    }
    assign_ssa(ctx, instr->dest.ssa.index, result);
}

static bool visit_block(struct rc_nir_context *ctx, nir_block *block) {

    nir_foreach_instr(instr, block)
    {
        switch (instr->type) {
            case nir_instr_type_alu:
                printf("nir: alu \n");
                visit_alu(ctx, nir_instr_as_alu(instr));
                break;
            case nir_instr_type_load_const:
                printf("nir: load const \n");
                if (!visit_load_const(ctx, nir_instr_as_load_const(instr)))
                    return false;
                break;
            case nir_instr_type_intrinsic:
                if (!visit_intrinsic(ctx, nir_instr_as_intrinsic(instr)))
                    return false;
                break;
            case nir_instr_type_deref:
                printf("nir: deref \n");
                visit_deref(ctx, nir_instr_as_deref(instr));
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
    foreach_list_typed(nir_cf_node, node, node, list)
    {
        switch (node->type) {
            case nir_cf_node_block:
                if (!visit_block(ctx, nir_cf_node_as_block(node))) {
                    return false;
                }
                break;
            default:
                printf("unknown node type \n");
                return false;
        }
    }
    return true;
}

static struct rc_type rc_uint_type(struct rc_type type) {
    struct rc_type res_type;
    memset(&res_type, 0, sizeof res_type);
    res_type.width = type.width;
    res_type.length = type.length;

    return res_type;
}

static struct rc_type rc_int_type(struct rc_type type) {
    struct rc_type res_type;
    memset(&res_type, 0, sizeof res_type);
    res_type.width = type.width;
    res_type.length = type.length;
    res_type.sign = 1;

    return res_type;
}

static LLVMTypeRef get_register_type(struct rc_nir_context *ctx, nir_register *reg) {
    struct rc_build_context *int_bld = get_int_bld(ctx, true, reg->bit_size == 1 ? 32 : reg->bit_size);
    LLVMTypeRef type = int_bld->vec_type;
    if (reg->num_components > 1)
        type = LLVMArrayType(type, reg->num_components);
    if (reg->num_array_elems)
        type = LLVMArrayType(type, reg->num_array_elems);

    return type;
}

static LLVMBuilderRef create_builder_at_entry(struct rc_llvm_context *rc) {
    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(rc->builder);
    LLVMValueRef function = LLVMGetBasicBlockParent(current_block);
    LLVMBasicBlockRef first_block = LLVMGetEntryBasicBlock(function);
    LLVMValueRef first_instr = LLVMGetFirstInstruction(first_block);
    LLVMBuilderRef first_builder = LLVMCreateBuilderInContext(rc->context);

    if (first_instr) {
        LLVMPositionBuilderBefore(first_builder, first_instr);
    } else {
        LLVMPositionBuilderAtEnd(first_builder, first_block);
    }

    return first_builder;
}

static LLVMValueRef build_alloca(struct rc_llvm_context *rc, LLVMTypeRef type, const char *name) {
    LLVMBuilderRef first_builder = create_builder_at_entry(rc);
    LLVMValueRef res = LLVMBuildAlloca(first_builder, type, name);
    LLVMBuildStore(rc->builder, LLVMConstNull(type), res);
    LLVMDisposeBuilder(first_builder);

    return res;
}

static void init_var_slots(struct rc_nir_context *ctx, nir_variable *var, unsigned sc) {
    unsigned slots = glsl_count_attribute_slots(var->type, false) * 4;

    for (unsigned comp = sc; comp < slots + sc; comp++) {
        auto this_loc = var->data.driver_location + (comp / 4);
        auto this_chan = comp % 4;

        if (!ctx->outputs[this_loc][this_chan]) {
            ctx->outputs[this_loc][this_chan] = rc_build_alloca(ctx->base.rc, ctx->base.vec_type, "output");
        }
    }
}

static void var_decl(struct rc_nir_context *ctx, struct nir_variable *var) {
    unsigned sc = var->data.location_frac;
    switch (var->data.mode) {
        case nir_var_shader_out:{
            if (ctx->stage == MESA_SHADER_FRAGMENT) {
                if (var->data.location == FRAG_RESULT_STENCIL) {
                    sc = 1;
                } else if (var->data.location == FRAG_RESULT_DEPTH) {
                    sc = 2;
                }
            }
            init_var_slots(ctx, var, sc);
        }
    }
}

bool rc_nir_translate(struct rc_llvm_context *rc, struct nir_shader *nir) {
    struct rc_nir_context ctx;
    memset(&ctx, 0, sizeof ctx);
    struct nir_function *func;

    nir_convert_from_ssa(nir, true);
    nir_lower_locals_to_regs(nir);
    nir_remove_dead_derefs(nir);
    nir_remove_dead_variables(nir, nir_var_function_temp, NULL);

    nir_print_shader(nir, stdout);

    if (ctx.stage == MESA_SHADER_VERTEX) {
        ctx.abi.vertex_id = LLVMGetParam(rc->main_function.value, 1);
        //ctx.abi.io = LLVMGetParam(rc->main_function.value, 0);
        LLVMSetValueName(ctx.abi.vertex_id, "vertex_id");

        struct rc_type vs_type;
        memset(&vs_type, 0, sizeof vs_type);
        vs_type.floating = TRUE; /* floating point values */
        vs_type.sign = TRUE;     /* values are signed */
        vs_type.norm = FALSE;    /* values are not limited to [0,1] or [-1,1] */
        vs_type.width = 32;      /* 32-bit float */
        vs_type.length = MAX_VECTOR_WIDTH / 32;

        rc_build_context_init(&ctx.base, rc, vs_type);
        rc_build_context_init(&ctx.uint_bld, rc, rc_uint_type(vs_type));
        rc_build_context_init(&ctx.int_bld, rc, rc_int_type(vs_type));
    }

    nir_foreach_shader_out_variable(variable, nir)
        var_decl(&ctx, variable);

    ctx.rc = *rc;
    ctx.stage = nir->info.stage;

    func = (struct nir_function *) exec_list_get_head(&nir->functions);
    nir_index_ssa_defs(func->impl);

    ctx.defs = _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
    ctx.ssa_defs = (LLVMValueRef *) calloc(func->impl->ssa_alloc, sizeof(LLVMValueRef));
    ctx.vars = _mesa_hash_table_create(NULL, _mesa_hash_pointer,_mesa_key_pointer_equal);

    ctx.regs = _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
    nir_foreach_register(reg, &func->impl->registers)
    {
        LLVMTypeRef type = get_register_type(&ctx, reg);
        LLVMValueRef reg_alloc = build_alloca(&ctx.rc, type, "reg");
        _mesa_hash_table_insert(ctx.regs, reg, reg_alloc);
    }

    if (!visit_cf_list(&ctx, &func->impl->body)) {
        return false;
    }
    // add the terminator inst at the end of block
    LLVMValueRef ret = LLVMConstInt(LLVMInt32TypeInContext(rc->context), 1, 0);
    LLVMBuildRet(ctx.rc.builder, ret);

    free(ctx.ssa_defs);
    ralloc_free(ctx.defs);
    ralloc_free(ctx.regs);
    ralloc_free(ctx.vars);

    return true;
}