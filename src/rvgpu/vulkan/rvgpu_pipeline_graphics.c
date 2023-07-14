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

#include "spirv/nir_spirv.h"
#include "nir/nir_xfb_info.h"

#include "vk_pipeline_cache.h"
#include "vk_shader_module.h"
#include "rvgpu_private.h"

static const struct nir_shader_compiler_options global_rvgpu_nir_options = {
   .lower_scmp = true,
   .lower_flrp32 = true,
   .lower_flrp64 = true,
   .lower_fsat = true,
   .lower_bitfield_insert_to_shifts = true,
   .lower_bitfield_extract_to_shifts = true,
   .lower_fdot = true,
   .lower_fdph = true,
   .lower_ffma16 = true,
   .lower_ffma32 = true,
   .lower_ffma64 = true,
   .lower_flrp16 = true,
   .lower_fmod = true,
   .lower_hadd = true,
   .lower_uadd_sat = true,
   .lower_usub_sat = true,
   .lower_iadd_sat = true,
   .lower_ldexp = true,
   .lower_pack_snorm_2x16 = true,
   .lower_pack_snorm_4x8 = true,
   .lower_pack_unorm_2x16 = true,
   .lower_pack_unorm_4x8 = true,
   .lower_pack_half_2x16 = true,
   .lower_pack_split = true,
   .lower_unpack_snorm_2x16 = true,
   .lower_unpack_snorm_4x8 = true,
   .lower_unpack_unorm_2x16 = true,
   .lower_unpack_unorm_4x8 = true,
   .lower_unpack_half_2x16 = true,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_insert_byte = true,
   .lower_insert_word = true,
   .lower_rotate = true,
   .lower_uadd_carry = true,
   .lower_usub_borrow = true,
   .lower_mul_2x32_64 = true,
   .lower_ifind_msb = true,
   .lower_int64_options = nir_lower_imul_2x32_64,
   .max_unroll_iterations = 32,
   .use_interpolated_input_intrinsics = true,
   .lower_to_scalar = true,
   .lower_uniforms_to_ubo = true,
   .lower_vector_cmp = true,
   .lower_device_index_to_zero = true,
   .support_16bit_alu = true,
   .lower_fisnormal = true,
   .use_scoped_barrier = true,
};

static inline const struct nir_shader_compiler_options *
rvgpu_get_compiler_options(enum pipe_shader_ir ir, enum pipe_shader_type shader)
{
   assert(ir == PIPE_SHADER_IR_NIR);
   return &global_rvgpu_nir_options;
}

static uint32_t
get_required_subgroup_size(const VkPipelineShaderStageCreateInfo *info)
{
   const VkPipelineShaderStageRequiredSubgroupSizeCreateInfo *rss_info =
      vk_find_struct_const(info->pNext,
                           PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO);
   return rss_info != NULL ? rss_info->requiredSubgroupSize : 0;
}

#ifndef NDEBUG
static bool
layouts_equal(const struct rvgpu_descriptor_set_layout *a, const struct rvgpu_descriptor_set_layout *b)
{
   const uint8_t *pa = (const uint8_t*)a, *pb = (const uint8_t*)b;
   uint32_t hash_start_offset = sizeof(struct vk_descriptor_set_layout);
   uint32_t binding_offset = offsetof(struct rvgpu_descriptor_set_layout, binding);
   /* base equal */
   if (memcmp(pa + hash_start_offset, pb + hash_start_offset, binding_offset - hash_start_offset))
      return false;

   /* bindings equal */
   if (a->binding_count != b->binding_count)
      return false;
   size_t binding_size = a->binding_count * sizeof(struct rvgpu_descriptor_set_binding_layout);
   const struct rvgpu_descriptor_set_binding_layout *la = a->binding;
   const struct rvgpu_descriptor_set_binding_layout *lb = b->binding;
   if (memcmp(la, lb, binding_size)) {
      for (unsigned i = 0; i < a->binding_count; i++) {
         if (memcmp(&la[i], &lb[i], offsetof(struct rvgpu_descriptor_set_binding_layout, immutable_samplers)))
            return false;
      }
   }

   /* immutable sampler equal */
   if (a->immutable_sampler_count != b->immutable_sampler_count)
      return false;
   if (a->immutable_sampler_count) {
      size_t sampler_size = a->immutable_sampler_count * sizeof(struct lvp_sampler *);
      if (memcmp(pa + binding_offset + binding_size, pb + binding_offset + binding_size, sampler_size)) {
         struct rvgpu_sampler **sa = (struct rvgpu_sampler **)(pa + binding_offset);
         struct rvgpu_sampler **sb = (struct rvgpu_sampler **)(pb + binding_offset);
         for (unsigned i = 0; i < a->immutable_sampler_count; i++) {
            if (memcmp(sa[i], sb[i], sizeof(struct rvgpu_sampler)))
               return false;
         }
      }
   }
   return true;
}
#endif

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

static void
copy_shader_sanitized(struct rvgpu_shader *dst, const struct rvgpu_shader *src)
{
   *dst = *src;
   dst->pipeline_nir = NULL; //this gets handled later
   dst->tess_ccw = NULL; //this gets handled later
   assert(!dst->shader_cso);
   assert(!dst->tess_ccw_cso);
   if (src->inlines.can_inline)
      _mesa_set_init(&dst->inlines.variants, NULL, NULL, inline_variant_equals);
}

static void
merge_layouts(struct vk_device *device, struct rvgpu_pipeline *dst, struct rvgpu_pipeline_layout *src)
{
   if (!src)
      return;
   if (dst->layout) {
      /* these must match */
      ASSERTED VkPipelineCreateFlags src_flag = src->vk.create_flags & VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT;
      ASSERTED VkPipelineCreateFlags dst_flag = dst->layout->vk.create_flags & VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT;
      assert(src_flag == dst_flag);
   }
   /* always try to reuse existing layout: independent sets bit doesn't guarantee independent sets */
   if (!dst->layout) {
      dst->layout = (struct rvgpu_pipeline_layout*)vk_pipeline_layout_ref(&src->vk);
      return;
   }
   /* this is a big optimization when hit */
   if (dst->layout == src)
      return;
#ifndef NDEBUG
   /* verify that layouts match */
   const struct rvgpu_pipeline_layout *smaller = dst->layout->vk.set_count < src->vk.set_count ? dst->layout : src;
   const struct rvgpu_pipeline_layout *bigger = smaller == dst->layout ? src : dst->layout;
   for (unsigned i = 0; i < smaller->vk.set_count; i++) {
      if (!smaller->vk.set_layouts[i] || !bigger->vk.set_layouts[i] ||
          smaller->vk.set_layouts[i] == bigger->vk.set_layouts[i])
         continue;

      const struct rvgpu_descriptor_set_layout *smaller_set_layout =
         vk_to_rvgpu_descriptor_set_layout(smaller->vk.set_layouts[i]);
      const struct rvgpu_descriptor_set_layout *bigger_set_layout =
         vk_to_rvgpu_descriptor_set_layout(bigger->vk.set_layouts[i]);

      assert(!smaller_set_layout->binding_count ||
             !bigger_set_layout->binding_count ||
             layouts_equal(smaller_set_layout, bigger_set_layout));
   }
#endif
   /* must be independent sets with different layouts: reallocate to avoid modifying original layout */
   struct rvgpu_pipeline_layout *old_layout = dst->layout;
   dst->layout = vk_zalloc(&device->alloc, sizeof(struct rvgpu_pipeline_layout), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   memcpy(dst->layout, old_layout, sizeof(struct rvgpu_pipeline_layout));
   dst->layout->vk.ref_cnt = 1;
   for (unsigned i = 0; i < dst->layout->vk.set_count; i++) {
      if (dst->layout->vk.set_layouts[i])
         vk_descriptor_set_layout_ref(dst->layout->vk.set_layouts[i]);
   }
   vk_pipeline_layout_unref(device, &old_layout->vk);

   for (unsigned i = 0; i < src->vk.set_count; i++) {
      if (!dst->layout->vk.set_layouts[i]) {
         dst->layout->vk.set_layouts[i] = src->vk.set_layouts[i];
         if (dst->layout->vk.set_layouts[i])
            vk_descriptor_set_layout_ref(src->vk.set_layouts[i]);
      }
   }
   dst->layout->vk.set_count = MAX2(dst->layout->vk.set_count,
                                    src->vk.set_count);
   dst->layout->push_constant_size += src->push_constant_size;
   dst->layout->push_constant_stages |= src->push_constant_stages;
}

static VkResult
compile_spirv(struct rvgpu_device *pdevice, const VkPipelineShaderStageCreateInfo *sinfo, nir_shader **nir)
{
   gl_shader_stage stage = vk_to_mesa_shader_stage(sinfo->stage);
   assert(stage <= MESA_SHADER_COMPUTE && stage != MESA_SHADER_NONE);
   VkResult result;

   const struct spirv_to_nir_options spirv_options = {
      .environment = NIR_SPIRV_VULKAN,
      .caps = {
         .float64 = false,
         .int16 = true,
         .int64 = false,
         .tessellation = true,
         .float_controls = true,
         .float32_atomic_add = true,
         .float32_atomic_min_max = true,
         .image_ms_array = true,
         .image_read_without_format = true,
         .image_write_without_format = true,
         .storage_image_ms = true,
         .geometry_streams = true,
         .storage_8bit = true,
         .storage_16bit = true,
         .variable_pointers = true,
         .stencil_export = true,
         .post_depth_coverage = true,
         .transform_feedback = true,
         .device_group = true,
         .draw_parameters = true,
         .shader_viewport_index_layer = true,
         .shader_clock = true,
         .multiview = true,
         .physical_storage_buffer_address = true,
         .int64_atomics = true,
         .subgroup_arithmetic = true,
         .subgroup_basic = true,
         .subgroup_ballot = true,
         .subgroup_quad = true,
         .subgroup_shuffle = true,
         .subgroup_vote = true,
         .vk_memory_model = true,
         .vk_memory_model_device_scope = true,
         .int8 = true,
         .float16 = true,
         .demote_to_helper_invocation = true,
      },
      .ubo_addr_format = nir_address_format_32bit_index_offset,
      .ssbo_addr_format = nir_address_format_32bit_index_offset,
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .push_const_addr_format = nir_address_format_logical,
      .shared_addr_format = nir_address_format_32bit_offset,
   };

   result = vk_pipeline_shader_stage_to_nir(&pdevice->vk, sinfo,
                                            &spirv_options, rvgpu_get_compiler_options(PIPE_SHADER_IR_NIR, stage),
                                            NULL, nir);
   return result;
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

static VkResult
rvgpu_shader_compile_to_ir(struct rvgpu_pipeline *pipeline,
                           const VkPipelineShaderStageCreateInfo *sinfo)
{
   struct rvgpu_device *pdevice = pipeline->device;
   gl_shader_stage stage = vk_to_mesa_shader_stage(sinfo->stage);
   assert(stage <= MESA_SHADER_COMPUTE && stage != MESA_SHADER_NONE);
   struct rvgpu_shader *shader = &pipeline->shaders[stage];
   nir_shader *nir;
   VkResult result = compile_spirv(pdevice, sinfo, &nir);
   if (result == VK_SUCCESS)
      rvgpu_shader_lower(pdevice, nir, shader, pipeline->layout);
   return result;
}

static void
merge_tess_info(struct shader_info *tes_info,
                const struct shader_info *tcs_info)
{
   /* The Vulkan 1.0.38 spec, section 21.1 Tessellator says:
    *
    *    "PointMode. Controls generation of points rather than triangles
    *     or lines. This functionality defaults to disabled, and is
    *     enabled if either shader stage includes the execution mode.
    *
    * and about Triangles, Quads, IsoLines, VertexOrderCw, VertexOrderCcw,
    * PointMode, SpacingEqual, SpacingFractionalEven, SpacingFractionalOdd,
    * and OutputVertices, it says:
    *
    *    "One mode must be set in at least one of the tessellation
    *     shader stages."
    *
    * So, the fields can be set in either the TCS or TES, but they must
    * agree if set in both.  Our backend looks at TES, so bitwise-or in
    * the values from the TCS.
    */
   assert(tcs_info->tess.tcs_vertices_out == 0 ||
          tes_info->tess.tcs_vertices_out == 0 ||
          tcs_info->tess.tcs_vertices_out == tes_info->tess.tcs_vertices_out);
   tes_info->tess.tcs_vertices_out |= tcs_info->tess.tcs_vertices_out;

   assert(tcs_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tes_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tcs_info->tess.spacing == tes_info->tess.spacing);
   tes_info->tess.spacing |= tcs_info->tess.spacing;

   assert(tcs_info->tess._primitive_mode == 0 ||
          tes_info->tess._primitive_mode == 0 ||
          tcs_info->tess._primitive_mode == tes_info->tess._primitive_mode);
   tes_info->tess._primitive_mode |= tcs_info->tess._primitive_mode;
   tes_info->tess.ccw |= tcs_info->tess.ccw;
   tes_info->tess.point_mode |= tcs_info->tess.point_mode;
}

static struct rvgpu_pipeline_nir *create_pipeline_nir(nir_shader *nir)
{
   struct rvgpu_pipeline_nir *pipeline_nir = ralloc(NULL, struct rvgpu_pipeline_nir);
   pipeline_nir->nir = nir;
   pipeline_nir->ref_cnt = 1;
   return pipeline_nir;
}

static inline void
rvgpu_pipeline_nir_ref(struct rvgpu_pipeline_nir **dst, struct rvgpu_pipeline_nir *src)
{
   struct rvgpu_pipeline_nir *old_dst = *dst;
   if (old_dst == src || (old_dst && src && old_dst->nir == src->nir))
      return;

   if (old_dst && p_atomic_dec_zero(&old_dst->ref_cnt)) {
      ralloc_free(old_dst->nir);
      ralloc_free(old_dst);
   }
   if (src)
      p_atomic_inc(&src->ref_cnt);
   *dst = src;
}

static void
rvgpu_shader_xfb_init(struct rvgpu_shader *shader)
{
   nir_xfb_info *xfb_info = shader->pipeline_nir->nir->xfb_info;
   if (xfb_info) {
      uint8_t output_mapping[VARYING_SLOT_TESS_MAX];
      memset(output_mapping, 0, sizeof(output_mapping));

      nir_foreach_shader_out_variable(var, shader->pipeline_nir->nir) {
         unsigned slots = var->data.compact ? DIV_ROUND_UP(glsl_get_length(var->type), 4)
                                            : glsl_count_attribute_slots(var->type, false);
         for (unsigned i = 0; i < slots; i++)
            output_mapping[var->data.location + i] = var->data.driver_location + i;
      }

      shader->stream_output.num_outputs = xfb_info->output_count;
      for (unsigned i = 0; i < PIPE_MAX_SO_BUFFERS; i++) {
         if (xfb_info->buffers_written & (1 << i)) {
            shader->stream_output.stride[i] = xfb_info->buffers[i].stride / 4;
         }
      }
      for (unsigned i = 0; i < xfb_info->output_count; i++) {
         shader->stream_output.output[i].output_buffer = xfb_info->outputs[i].buffer;
         shader->stream_output.output[i].dst_offset = xfb_info->outputs[i].offset / 4;
         shader->stream_output.output[i].register_index = output_mapping[xfb_info->outputs[i].location];
         shader->stream_output.output[i].num_components = util_bitcount(xfb_info->outputs[i].component_mask);
         shader->stream_output.output[i].start_component = ffs(xfb_info->outputs[i].component_mask) - 1;
         shader->stream_output.output[i].stream = xfb_info->buffer_to_stream[xfb_info->outputs[i].buffer];
      }

   }
}

static void
rvgpu_pipeline_xfb_init(struct rvgpu_pipeline *pipeline)
{
   gl_shader_stage stage = MESA_SHADER_VERTEX;
   if (pipeline->shaders[MESA_SHADER_GEOMETRY].pipeline_nir)
      stage = MESA_SHADER_GEOMETRY;
   else if (pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir)
      stage = MESA_SHADER_TESS_EVAL;
   pipeline->last_vertex = stage;
   rvgpu_shader_xfb_init(&pipeline->shaders[stage]);
}

static VkResult
rvgpu_graphics_pipeline_init(struct rvgpu_pipeline *pipeline,
                             struct rvgpu_device *device,
                             struct vk_pipeline_cache *cache,
                             const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   VkResult result;

   const VkGraphicsPipelineLibraryCreateInfoEXT *libinfo = vk_find_struct_const(pCreateInfo,
                                                                                GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT);
   const VkPipelineLibraryCreateInfoKHR *libstate = vk_find_struct_const(pCreateInfo,
                                                                         PIPELINE_LIBRARY_CREATE_INFO_KHR);
   const VkGraphicsPipelineLibraryFlagsEXT layout_stages = VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
                                                           VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT;
   if (libinfo)
      pipeline->stages = libinfo->flags;
   else if (!libstate)
      pipeline->stages = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT |
                         VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
                         VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT |
                         VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT;

   if (pCreateInfo->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR)
      pipeline->library = true;

   struct rvgpu_pipeline_layout *layout = rvgpu_pipeline_layout_from_handle(pCreateInfo->layout);

   if (!layout || !(layout->vk.create_flags & VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT))
      /* this is a regular pipeline with no partials: directly reuse */
      pipeline->layout = layout ? (void*)vk_pipeline_layout_ref(&layout->vk) : NULL;
   else if (pipeline->stages & layout_stages) {
      if ((pipeline->stages & layout_stages) == layout_stages)
         /* this has all the layout stages: directly reuse */
         pipeline->layout = (void*)vk_pipeline_layout_ref(&layout->vk);
      else {
         /* this is a partial: copy for later merging to avoid modifying another layout */
         merge_layouts(&device->vk, pipeline, layout);
      }
   }

   if (libstate) {
      for (unsigned i = 0; i < libstate->libraryCount; i++) {
         RVGPU_FROM_HANDLE(rvgpu_pipeline, p, libstate->pLibraries[i]);
         vk_graphics_pipeline_state_merge(&pipeline->graphics_state,
                                          &p->graphics_state);
         if (p->stages & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
            pipeline->line_smooth = p->line_smooth;
            pipeline->disable_multisample = p->disable_multisample;
            pipeline->line_rectangular = p->line_rectangular;
            memcpy(pipeline->shaders, p->shaders, sizeof(struct rvgpu_shader) * 4);
            for (unsigned i = 0; i < MESA_SHADER_COMPUTE; i++) {
               copy_shader_sanitized(&pipeline->shaders[i], &p->shaders[i]);
            }
         }
         if (p->stages & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
            pipeline->force_min_sample = p->force_min_sample;
            copy_shader_sanitized(&pipeline->shaders[MESA_SHADER_FRAGMENT], &p->shaders[MESA_SHADER_FRAGMENT]);
         }
         if (p->stages & layout_stages) {
            if (!layout || (layout->vk.create_flags & VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT))
               merge_layouts(&device->vk, pipeline, p->layout);
         }
         pipeline->stages |= p->stages;
      }
   }

   result = vk_graphics_pipeline_state_fill(&device->vk,
                                            &pipeline->graphics_state,
                                            pCreateInfo, NULL, NULL, NULL,
                                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
                                            &pipeline->state_data);
   if (result != VK_SUCCESS)
      return result;

   assert(pipeline->library || pipeline->stages == (VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT |
                                                    VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
                                                    VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT |
                                                    VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT));

   pipeline->device = device;

   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      const VkPipelineShaderStageCreateInfo *sinfo = &pCreateInfo->pStages[i];
      gl_shader_stage stage = vk_to_mesa_shader_stage(sinfo->stage);
      if (stage == MESA_SHADER_FRAGMENT) {
         if (!(pipeline->stages & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT))
            continue;
      } else {
         if (!(pipeline->stages & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT))
            continue;
      }
      result = rvgpu_shader_compile_to_ir(pipeline, sinfo);
      if (result != VK_SUCCESS)
         goto fail;

      switch (stage) {
      case MESA_SHADER_FRAGMENT:
         if (pipeline->shaders[MESA_SHADER_FRAGMENT].pipeline_nir->nir->info.fs.uses_sample_shading)
            pipeline->force_min_sample = true;
         break;
      default: break;
      }
   }
   if (pCreateInfo->stageCount && pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir) {
      nir_lower_patch_vertices(pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir->nir, pipeline->shaders[MESA_SHADER_TESS_CTRL].pipeline_nir->nir->info.tess.tcs_vertices_out, NULL);
      merge_tess_info(&pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir->nir->info, &pipeline->shaders[MESA_SHADER_TESS_CTRL].pipeline_nir->nir->info);
      if (BITSET_TEST(pipeline->graphics_state.dynamic,
                      MESA_VK_DYNAMIC_TS_DOMAIN_ORIGIN)) {
         pipeline->shaders[MESA_SHADER_TESS_EVAL].tess_ccw = create_pipeline_nir(nir_shader_clone(NULL, pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir->nir));
         pipeline->shaders[MESA_SHADER_TESS_EVAL].tess_ccw->nir->info.tess.ccw = !pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir->nir->info.tess.ccw;
      } else if (pipeline->graphics_state.ts->domain_origin == VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT) {
         pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir->nir->info.tess.ccw = !pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir->nir->info.tess.ccw;
      }
   }
   if (libstate) {
       for (unsigned i = 0; i < libstate->libraryCount; i++) {
          RVGPU_FROM_HANDLE(rvgpu_pipeline, p, libstate->pLibraries[i]);
          if (p->stages & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
             if (p->shaders[MESA_SHADER_FRAGMENT].pipeline_nir)
                rvgpu_pipeline_nir_ref(&pipeline->shaders[MESA_SHADER_FRAGMENT].pipeline_nir, p->shaders[MESA_SHADER_FRAGMENT].pipeline_nir);
          }
          if (p->stages & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
             for (unsigned j = MESA_SHADER_VERTEX; j < MESA_SHADER_FRAGMENT; j++) {
                if (p->shaders[j].pipeline_nir)
                   rvgpu_pipeline_nir_ref(&pipeline->shaders[j].pipeline_nir, p->shaders[j].pipeline_nir);
             }
             if (p->shaders[MESA_SHADER_TESS_EVAL].tess_ccw)
                rvgpu_pipeline_nir_ref(&pipeline->shaders[MESA_SHADER_TESS_EVAL].tess_ccw, p->shaders[MESA_SHADER_TESS_EVAL].tess_ccw);
          }
       }
   } else if (pipeline->stages & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
      const struct vk_rasterization_state *rs = pipeline->graphics_state.rs;
      if (rs) {
         /* always draw bresenham if !smooth */
         pipeline->line_smooth = rs->line.mode == VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT;
         pipeline->disable_multisample = rs->line.mode == VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT ||
                                         rs->line.mode == VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT;
         pipeline->line_rectangular = rs->line.mode != VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT;
      } else
         pipeline->line_rectangular = true;
      rvgpu_pipeline_xfb_init(pipeline);
   }
   if (!libstate && !pipeline->library)
      rvgpu_pipeline_shaders_compile(pipeline);

   return VK_SUCCESS;

fail:
   for (unsigned i = 0; i < ARRAY_SIZE(pipeline->shaders); i++) {
      rvgpu_pipeline_nir_ref(&pipeline->shaders[i].pipeline_nir, NULL);
   }
   vk_free(&device->vk.alloc, pipeline->state_data);

   return result;

}

static VkResult
rvgpu_graphics_pipeline_create(VkDevice _device, VkPipelineCache _cache,
                               const VkGraphicsPipelineCreateInfo *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline)
{
   RVGPU_FROM_HANDLE(rvgpu_device, device, _device);
   VK_FROM_HANDLE(vk_pipeline_cache, cache, _cache);
   struct rvgpu_pipeline *pipeline;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);

   pipeline = vk_zalloc(&device->vk.alloc, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pipeline->base,
                       VK_OBJECT_TYPE_PIPELINE);
   uint64_t t0 = os_time_get_nano();
   result = rvgpu_graphics_pipeline_init(pipeline, device, cache, pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, pipeline);
      return result;
   }

   VkPipelineCreationFeedbackCreateInfo *feedback = (void*)vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);
   if (feedback) {
      feedback->pPipelineCreationFeedback->duration = os_time_get_nano() - t0;
      feedback->pPipelineCreationFeedback->flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;
      memset(feedback->pPipelineStageCreationFeedbacks, 0, sizeof(VkPipelineCreationFeedback) * feedback->pipelineStageCreationFeedbackCount);
   }

   *pPipeline = rvgpu_pipeline_to_handle(pipeline);

   return VK_SUCCESS;
  
}

void
rvgpu_pipeline_shaders_compile(struct rvgpu_pipeline *pipeline)
{
   if (pipeline->compiled)
      return;
   for (uint32_t i = 0; i < ARRAY_SIZE(pipeline->shaders); i++) {
      if (!pipeline->shaders[i].pipeline_nir)
         continue;

      gl_shader_stage stage = i;
      assert(stage == pipeline->shaders[i].pipeline_nir->nir->info.stage);

      if (!pipeline->shaders[stage].inlines.can_inline) {
         pipeline->shaders[stage].shader_cso = rvgpu_shader_compile(pipeline->device, &pipeline->shaders[stage],
                                                            nir_shader_clone(NULL, pipeline->shaders[stage].pipeline_nir->nir));
         if (pipeline->shaders[MESA_SHADER_TESS_EVAL].tess_ccw)
            pipeline->shaders[MESA_SHADER_TESS_EVAL].tess_ccw_cso = rvgpu_shader_compile(pipeline->device, &pipeline->shaders[stage],
                                                          nir_shader_clone(NULL, pipeline->shaders[MESA_SHADER_TESS_EVAL].tess_ccw->nir));
      }
   }
   pipeline->compiled = true;
}

VKAPI_ATTR VkResult VKAPI_CALL
rvgpu_CreateGraphicsPipelines(VkDevice _device, VkPipelineCache pipelineCache, uint32_t count,
                              const VkGraphicsPipelineCreateInfo *pCreateInfos,
                              const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
   VkResult result = VK_SUCCESS;
   unsigned i = 0;

   for (; i < count; i++) {
      VkResult r;
      r = rvgpu_graphics_pipeline_create(_device, pipelineCache, &pCreateInfos[i], pAllocator, &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;

         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; ++i)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}
