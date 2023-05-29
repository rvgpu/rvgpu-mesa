/*
 * Copyright © 2023 Sietium Semiconductor
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "nir/nir_builder.h"

#include "rvgpu_private.h"

static void
shared_var_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size = glsl_type_is_boolean(type)
      ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length,
      *align = comp_size;
}

static bool
find_tex(const nir_instr *instr, const void *data_cb)
{
   if (instr->type == nir_instr_type_tex)
      return true;
   return false;
}

static nir_ssa_def *
fixup_tex_instr(struct nir_builder *b, nir_instr *instr, void *data_cb)
{
   nir_tex_instr *tex_instr = nir_instr_as_tex(instr);
   unsigned offset = 0;

   int idx = nir_tex_instr_src_index(tex_instr, nir_tex_src_texture_offset);
   if (idx == -1)
      return NULL;

   if (!nir_src_is_const(tex_instr->src[idx].src))
      return NULL;
   offset = nir_src_comp_as_uint(tex_instr->src[idx].src, 0);

   nir_tex_instr_remove_src(tex_instr, idx);
   tex_instr->texture_index += offset;
   return NIR_LOWER_INSTR_PROGRESS;
}

static bool
rvgpu_nir_fixup_indirect_tex(nir_shader *shader)
{
    return nir_shader_lower_instructions(shader, find_tex, fixup_tex_instr, NULL);
}

static void
optimize(nir_shader *nir)
{
    bool progress = false;
    do {
        progress = false;

        NIR_PASS(progress, nir, nir_lower_flrp, 32|64, true);
        NIR_PASS(progress, nir, nir_split_array_vars, nir_var_function_temp);
        NIR_PASS(progress, nir, nir_shrink_vec_array_vars, nir_var_function_temp);
        NIR_PASS(progress, nir, nir_opt_deref);
        NIR_PASS(progress, nir, nir_lower_vars_to_ssa);

        NIR_PASS(progress, nir, nir_opt_copy_prop_vars);

        NIR_PASS(progress, nir, nir_copy_prop);
        NIR_PASS(progress, nir, nir_opt_dce);
        NIR_PASS(progress, nir, nir_opt_peephole_select, 8, true, true);

        NIR_PASS(progress, nir, nir_opt_algebraic);
        NIR_PASS(progress, nir, nir_opt_constant_folding);

        NIR_PASS(progress, nir, nir_opt_remove_phis);
        bool trivial_continues = false;
        NIR_PASS(trivial_continues, nir, nir_opt_trivial_continues);
        progress |= trivial_continues;
        if (trivial_continues) {
            /* If nir_opt_trivial_continues makes progress, then we need to clean
             * things up if we want any hope of nir_opt_if or nir_opt_loop_unroll
             * to make progress.
             */
            NIR_PASS(progress, nir, nir_copy_prop);
            NIR_PASS(progress, nir, nir_opt_dce);
            NIR_PASS(progress, nir, nir_opt_remove_phis);
        }
        NIR_PASS(progress, nir, nir_opt_if, nir_opt_if_aggressive_last_continue | nir_opt_if_optimize_phi_true_false);
        NIR_PASS(progress, nir, nir_opt_dead_cf);
        NIR_PASS(progress, nir, nir_opt_conditional_discard);
        NIR_PASS(progress, nir, nir_opt_remove_phis);
        NIR_PASS(progress, nir, nir_opt_cse);
        NIR_PASS(progress, nir, nir_opt_undef);

        NIR_PASS(progress, nir, nir_opt_deref);
        NIR_PASS(progress, nir, nir_lower_alu_to_scalar, NULL, NULL);
        NIR_PASS(progress, nir, nir_opt_loop_unroll);
        NIR_PASS(progress, nir, rvgpu_nir_fixup_indirect_tex);
    } while (progress);
}

static void
rvgpu_shader_optimize(nir_shader *nir)
{
   optimize(nir);
   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_function_temp, NULL);
   NIR_PASS_V(nir, nir_opt_dce);
   nir_sweep(nir);
}

static void
set_image_access(struct rvgpu_shader *shader, struct rvgpu_pipeline_layout *layout, nir_shader *nir,
                 nir_intrinsic_instr *instr,
                 bool reads, bool writes)
{
    nir_variable *var = nir_intrinsic_get_var(instr, 0);
    /* calculate the variable's offset in the layout */
    uint64_t value = 0;
    const struct rvgpu_descriptor_set_binding_layout *binding =
            get_binding_layout(layout, var->data.descriptor_set, var->data.binding);
    for (unsigned s = 0; s < var->data.descriptor_set; s++) {
        if (layout->vk.set_layouts[s])
            value += get_set_layout(layout, s)->stage[nir->info.stage].image_count;
    }
    value += binding->stage[nir->info.stage].image_index;
    const unsigned size = glsl_type_is_array(var->type) ? glsl_get_aoa_size(var->type) : 1;
    uint64_t mask = BITFIELD64_MASK(MAX2(size, 1)) << value;

    if (reads)
        shader->access.images_read |= mask;
    if (writes)
        shader->access.images_written |= mask;
}

static void
set_buffer_access(struct rvgpu_shader *shader, struct rvgpu_pipeline_layout *layout, nir_shader *nir,
                  nir_intrinsic_instr *instr)
{
    nir_variable *var = nir_intrinsic_get_var(instr, 0);
    if (!var) {
        nir_deref_instr *deref = nir_instr_as_deref(instr->src[0].ssa->parent_instr);
        if (deref->modes != nir_var_mem_ssbo)
            return;
        nir_binding b = nir_chase_binding(instr->src[0]);
        var = nir_get_binding_variable(nir, b);
        if (!var)
            return;
    }
    if (var->data.mode != nir_var_mem_ssbo)
        return;
    /* calculate the variable's offset in the layout */
    uint64_t value = 0;
    const struct rvgpu_descriptor_set_binding_layout *binding =
            get_binding_layout(layout, var->data.descriptor_set, var->data.binding);
    for (unsigned s = 0; s < var->data.descriptor_set; s++) {
        if (layout->vk.set_layouts[s])
            value += get_set_layout(layout, s)->stage[nir->info.stage].shader_buffer_count;
    }
    value += binding->stage[nir->info.stage].shader_buffer_index;
    /* Structs have been lowered already, so get_aoa_size is sufficient. */
    const unsigned size = glsl_type_is_array(var->type) ? glsl_get_aoa_size(var->type) : 1;
    uint64_t mask = BITFIELD64_MASK(MAX2(size, 1)) << value;
    shader->access.buffers_written |= mask;
}

static void
scan_intrinsic(struct rvgpu_shader *shader, struct rvgpu_pipeline_layout *layout, nir_shader *nir, nir_intrinsic_instr *instr)
{
    switch (instr->intrinsic) {
        case nir_intrinsic_image_deref_sparse_load:
        case nir_intrinsic_image_deref_load:
        case nir_intrinsic_image_deref_size:
        case nir_intrinsic_image_deref_samples:
            set_image_access(shader, layout, nir, instr, true, false);
            break;
        case nir_intrinsic_image_deref_store:
            set_image_access(shader, layout, nir, instr, false, true);
            break;
        case nir_intrinsic_image_deref_atomic_add:
        case nir_intrinsic_image_deref_atomic_imin:
        case nir_intrinsic_image_deref_atomic_umin:
        case nir_intrinsic_image_deref_atomic_imax:
        case nir_intrinsic_image_deref_atomic_umax:
        case nir_intrinsic_image_deref_atomic_and:
        case nir_intrinsic_image_deref_atomic_or:
        case nir_intrinsic_image_deref_atomic_xor:
        case nir_intrinsic_image_deref_atomic_exchange:
        case nir_intrinsic_image_deref_atomic_comp_swap:
        case nir_intrinsic_image_deref_atomic_fadd:
            set_image_access(shader, layout, nir, instr, true, true);
            break;
        case nir_intrinsic_deref_atomic_add:
        case nir_intrinsic_deref_atomic_and:
        case nir_intrinsic_deref_atomic_comp_swap:
        case nir_intrinsic_deref_atomic_exchange:
        case nir_intrinsic_deref_atomic_fadd:
        case nir_intrinsic_deref_atomic_fcomp_swap:
        case nir_intrinsic_deref_atomic_fmax:
        case nir_intrinsic_deref_atomic_fmin:
        case nir_intrinsic_deref_atomic_imax:
        case nir_intrinsic_deref_atomic_imin:
        case nir_intrinsic_deref_atomic_or:
        case nir_intrinsic_deref_atomic_umax:
        case nir_intrinsic_deref_atomic_umin:
        case nir_intrinsic_deref_atomic_xor:
        case nir_intrinsic_store_deref:
            set_buffer_access(shader, layout, nir, instr);
            break;
        default: break;
    }
}

static void
scan_pipeline_info(struct rvgpu_shader *shader, struct rvgpu_pipeline_layout *layout, nir_shader *nir)
{
    nir_foreach_function(function, nir) {
        if (function->impl)
            nir_foreach_block(block, function->impl) {
            nir_foreach_instr(instr, block) {
                if (instr->type == nir_instr_type_intrinsic)
                    scan_intrinsic(shader, layout, nir, nir_instr_as_intrinsic(instr));
            }
        }
    }
}

static bool
lower_demote_impl(nir_builder *b, nir_instr *instr, void *data)
{
    if (instr->type != nir_instr_type_intrinsic)
        return false;
    nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
    if (intr->intrinsic == nir_intrinsic_demote || intr->intrinsic == nir_intrinsic_terminate) {
        intr->intrinsic = nir_intrinsic_discard;
        return true;
    }
    if (intr->intrinsic == nir_intrinsic_demote_if || intr->intrinsic == nir_intrinsic_terminate_if) {
        intr->intrinsic = nir_intrinsic_discard_if;
        return true;
    }
    return false;
}

static bool
lower_demote(nir_shader *nir)
{
    return nir_shader_instructions_pass(nir, lower_demote_impl, nir_metadata_dominance, NULL);
}

static nir_ssa_def *
load_frag_coord(nir_builder *b)
{
    nir_variable *pos =
            nir_find_variable_with_location(b->shader, nir_var_shader_in,
                                            VARYING_SLOT_POS);
    if (pos == NULL) {
        pos = nir_variable_create(b->shader, nir_var_shader_in,
                                  glsl_vec4_type(), NULL);
        pos->data.location = VARYING_SLOT_POS;
    }
    /**
     * From Vulkan spec:
     *   "The OriginLowerLeft execution mode must not be used; fragment entry
     *    points must declare OriginUpperLeft."
     *
     * So at this point origin_upper_left should be true
     */
    assert(b->shader->info.fs.origin_upper_left == true);

    return nir_load_var(b, pos);
}

static bool
try_lower_input_load(nir_function_impl *impl, nir_intrinsic_instr *load,
                     bool use_fragcoord_sysval)
{
    nir_deref_instr *deref = nir_src_as_deref(load->src[0]);
    assert(glsl_type_is_image(deref->type));

    enum glsl_sampler_dim image_dim = glsl_get_sampler_dim(deref->type);
    if (image_dim != GLSL_SAMPLER_DIM_SUBPASS &&
        image_dim != GLSL_SAMPLER_DIM_SUBPASS_MS)
        return false;

    nir_builder b;
    nir_builder_init(&b, impl);
    b.cursor = nir_before_instr(&load->instr);

    nir_ssa_def *frag_coord = use_fragcoord_sysval ? nir_load_frag_coord(&b)
                                                   : load_frag_coord(&b);
    frag_coord = nir_f2i32(&b, frag_coord);
    nir_ssa_def *offset = nir_ssa_for_src(&b, load->src[1], 2);
    nir_ssa_def *pos = nir_iadd(&b, frag_coord, offset);

    nir_ssa_def *layer = nir_load_view_index(&b);
    nir_ssa_def *coord =
            nir_vec4(&b, nir_channel(&b, pos, 0), nir_channel(&b, pos, 1), layer, nir_imm_int(&b, 0));

    nir_instr_rewrite_src(&load->instr, &load->src[1], nir_src_for_ssa(coord));

    return true;
}

static bool
rvgpu_lower_input_attachments(nir_shader *shader, bool use_fragcoord_sysval)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *load = nir_instr_as_intrinsic(instr);

            if (load->intrinsic != nir_intrinsic_image_deref_load)
               continue;

            progress |= try_lower_input_load(function->impl, load,
                                             use_fragcoord_sysval);
         }
      }
   }

   return progress;
}

static bool
remove_scoped_barriers_impl(nir_builder *b, nir_instr *instr, void *data)
{
    if (instr->type != nir_instr_type_intrinsic)
        return false;
    nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
    if (intr->intrinsic != nir_intrinsic_scoped_barrier)
        return false;
    if (data) {
        if (nir_intrinsic_memory_scope(intr) == NIR_SCOPE_WORKGROUP ||
            nir_intrinsic_memory_scope(intr) == NIR_SCOPE_DEVICE)
            return false;
    }
    nir_instr_remove(instr);
    return true;
}

static bool
remove_scoped_barriers(nir_shader *nir, bool is_compute)
{
    return nir_shader_instructions_pass(nir, remove_scoped_barriers_impl, nir_metadata_dominance, (void*)is_compute);
}

static struct rvgpu_pipeline_nir *
create_pipeline_nir(nir_shader *nir)
{
   struct rvgpu_pipeline_nir *pipeline_nir = ralloc(NULL, struct rvgpu_pipeline_nir);
   pipeline_nir->nir = nir;
   pipeline_nir->ref_cnt = 1;
   return pipeline_nir;
}

static bool
inline_variant_equals(const void *a, const void *b)
{
   const struct rvgpu_inline_variant *av = a, *bv = b;
   assert(av->mask == bv->mask);
   u_foreach_bit(slot, av->mask) {
      if (memcmp(av->vals[slot], bv->vals[slot], sizeof(av->vals[slot])))
         return false;
   }
   return true;
}

void
rvgpu_shader_lower(struct rvgpu_device *pdevice, nir_shader *nir, struct rvgpu_shader *shader, struct rvgpu_pipeline_layout *layout)
{
    if (nir->info.stage != MESA_SHADER_TESS_CTRL)
        NIR_PASS_V(nir, remove_scoped_barriers, nir->info.stage == MESA_SHADER_COMPUTE);

    const struct nir_lower_sysvals_to_varyings_options sysvals_to_varyings = {
            .frag_coord = true,
            .point_coord = true,
    };
    NIR_PASS_V(nir, nir_lower_sysvals_to_varyings, &sysvals_to_varyings);

    struct nir_lower_subgroups_options subgroup_opts = {0};
    subgroup_opts.lower_quad = true;
    subgroup_opts.ballot_components = 1;
    subgroup_opts.ballot_bit_size = 32;
    NIR_PASS_V(nir, nir_lower_subgroups, &subgroup_opts);

    if (nir->info.stage == MESA_SHADER_FRAGMENT)
        rvgpu_lower_input_attachments(nir, false);
    NIR_PASS_V(nir, nir_lower_system_values);
    NIR_PASS_V(nir, nir_lower_is_helper_invocation);
    NIR_PASS_V(nir, lower_demote);
    NIR_PASS_V(nir, nir_lower_compute_system_values, NULL);

    NIR_PASS_V(nir, nir_remove_dead_variables,
               nir_var_uniform | nir_var_image, NULL);

    scan_pipeline_info(shader, layout, nir);

    optimize(nir);
    nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

    rvgpu_lower_pipeline_layout(pdevice, layout, nir);

    NIR_PASS_V(nir, nir_lower_io_to_temporaries, nir_shader_get_entrypoint(nir), true, true);
    NIR_PASS_V(nir, nir_split_var_copies);
    NIR_PASS_V(nir, nir_lower_global_vars_to_local);

    NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_push_const,
               nir_address_format_32bit_offset);

    NIR_PASS_V(nir, nir_lower_explicit_io,
               nir_var_mem_ubo | nir_var_mem_ssbo,
               nir_address_format_32bit_index_offset);

    NIR_PASS_V(nir, nir_lower_explicit_io,
               nir_var_mem_global,
               nir_address_format_64bit_global);

    if (nir->info.stage == MESA_SHADER_COMPUTE) {
        NIR_PASS_V(nir, nir_lower_vars_to_explicit_types, nir_var_mem_shared, shared_var_info);
        NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_shared, nir_address_format_32bit_offset);
    }

    NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_shader_temp, NULL);

    if (nir->info.stage == MESA_SHADER_VERTEX ||
        nir->info.stage == MESA_SHADER_GEOMETRY) {
        NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);
    } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
        NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, true);
    }

    // TODO: also optimize the tex srcs. see radeonSI for reference */
    /* Skip if there are potentially conflicting rounding modes */
    struct nir_fold_16bit_tex_image_options fold_16bit_options = {
            .rounding_mode = nir_rounding_mode_undef,
            .fold_tex_dest_types = nir_type_float | nir_type_uint | nir_type_int,
    };
    NIR_PASS_V(nir, nir_fold_16bit_tex_image, &fold_16bit_options);

    rvgpu_shader_optimize(nir);

    if (nir->info.stage != MESA_SHADER_VERTEX)
        nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs, nir->info.stage);
    else {
        nir->num_inputs = util_last_bit64(nir->info.inputs_read);
        nir_foreach_shader_in_variable(var, nir) {
            var->data.driver_location = var->data.location - VERT_ATTRIB_GENERIC0;
        }
    }
    nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs,
                                nir->info.stage);

    nir_function_impl *impl = nir_shader_get_entrypoint(nir);
    if (impl->ssa_alloc > 100) //skip for small shaders
        shader->inlines.must_inline = rvgpu_find_inlinable_uniforms(shader, nir);
    shader->pipeline_nir = create_pipeline_nir(nir);
    if (shader->inlines.can_inline)
        _mesa_set_init(&shader->inlines.variants, NULL, NULL, inline_variant_equals);
}