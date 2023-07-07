/*
 * Copyright © 2023 Sietium Semiconductor.
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "cso_cache/cso_context.h"

#include "vk_cmd_enqueue_entrypoints.h"
#include "vk_util.h"

#include "rvgpu_private.h"

static inline enum pipe_shader_type
pipe_shader_type_from_mesa(gl_shader_stage stage)
{
   STATIC_ASSERT((enum pipe_shader_type) MESA_SHADER_VERTEX == PIPE_SHADER_VERTEX);
   STATIC_ASSERT((enum pipe_shader_type) MESA_SHADER_FRAGMENT == PIPE_SHADER_FRAGMENT);
   STATIC_ASSERT((enum pipe_shader_type) MESA_SHADER_TESS_CTRL == PIPE_SHADER_TESS_CTRL);
   STATIC_ASSERT((enum pipe_shader_type) MESA_SHADER_TESS_EVAL == PIPE_SHADER_TESS_EVAL);
   STATIC_ASSERT((enum pipe_shader_type) MESA_SHADER_GEOMETRY == PIPE_SHADER_GEOMETRY);
   STATIC_ASSERT((enum pipe_shader_type) MESA_SHADER_COMPUTE == PIPE_SHADER_COMPUTE);
   return (enum pipe_shader_type)stage;
}

enum gs_output {
  GS_OUTPUT_NONE,
  GS_OUTPUT_NOT_LINES,
  GS_OUTPUT_LINES,
};

struct rvgpu_render_attachment {
   struct rvgpu_image_view *imgv;
   VkResolveModeFlags resolve_mode;
   struct rvgpu_image_view *resolve_imgv;
   VkAttachmentLoadOp load_op;
   VkAttachmentStoreOp store_op;
   VkClearValue clear_value;
   bool read_only;
};

struct rendering_state {
   struct pipe_context *pctx;
   struct rvgpu_device *device; //for uniform inlining only
   struct u_upload_mgr *uploader;
   struct cso_context *cso;

   bool blend_dirty;
   bool rs_dirty;
   bool dsa_dirty;
   bool stencil_ref_dirty;
   bool clip_state_dirty;
   bool blend_color_dirty;
   bool ve_dirty;
   bool vb_dirty;
   bool constbuf_dirty[MESA_SHADER_STAGES];
   bool pcbuf_dirty[MESA_SHADER_STAGES];
   bool has_pcbuf[MESA_SHADER_STAGES];
   bool inlines_dirty[MESA_SHADER_STAGES];
   bool vp_dirty;
   bool scissor_dirty;
   bool ib_dirty;
   bool sample_mask_dirty;
   bool min_samples_dirty;
   bool poison_mem;
   bool noop_fs_bound;
   struct pipe_draw_indirect_info indirect_info;
   struct pipe_draw_info info;

   struct pipe_grid_info dispatch_info;
   struct pipe_framebuffer_state framebuffer;

   struct pipe_blend_state blend_state;
   struct {
      float offset_units;
      float offset_scale;
      float offset_clamp;
      bool enabled;
   } depth_bias;
   struct pipe_rasterizer_state rs_state;
   struct pipe_depth_stencil_alpha_state dsa_state;

   struct pipe_blend_color blend_color;
   struct pipe_stencil_ref stencil_ref;
   struct pipe_clip_state clip_state;

   int num_scissors;
   struct pipe_scissor_state scissors[16];

   int num_viewports;
   struct pipe_viewport_state viewports[16];
   struct {
      float min, max;
   } depth[16];

   uint8_t patch_vertices;
   ubyte index_size;
   unsigned index_offset;
   struct pipe_resource *index_buffer;
   struct pipe_constant_buffer const_buffer[MESA_SHADER_STAGES][16];
   int num_const_bufs[MESA_SHADER_STAGES];
   int num_vb;
   unsigned start_vb;
   struct pipe_vertex_buffer vb[PIPE_MAX_ATTRIBS];
   struct cso_velems_state velem;

   struct rvgpu_access_info access[MESA_SHADER_STAGES];
   struct pipe_sampler_view *sv[MESA_SHADER_STAGES][PIPE_MAX_SHADER_SAMPLER_VIEWS];
   int num_sampler_views[MESA_SHADER_STAGES];
   struct pipe_sampler_state ss[MESA_SHADER_STAGES][PIPE_MAX_SAMPLERS];
   /* cso_context api is stupid */
   const struct pipe_sampler_state *cso_ss_ptr[MESA_SHADER_STAGES][PIPE_MAX_SAMPLERS];
   int num_sampler_states[MESA_SHADER_STAGES];
   bool sv_dirty[MESA_SHADER_STAGES];
   bool ss_dirty[MESA_SHADER_STAGES];

   struct pipe_image_view iv[MESA_SHADER_STAGES][PIPE_MAX_SHADER_IMAGES];
   int num_shader_images[MESA_SHADER_STAGES];
   struct pipe_shader_buffer sb[MESA_SHADER_STAGES][PIPE_MAX_SHADER_BUFFERS];
   int num_shader_buffers[MESA_SHADER_STAGES];
   bool iv_dirty[MESA_SHADER_STAGES];
   bool sb_dirty[MESA_SHADER_STAGES];
   bool disable_multisample;
   enum gs_output gs_output_lines : 2;

   uint32_t color_write_disables:8;
   uint32_t pad:13;

   void *velems_cso;

   uint8_t push_constants[128 * 4];
   uint16_t push_size[2]; //gfx, compute
   uint16_t gfx_push_sizes[MESA_SHADER_COMPUTE];
   struct {
      void *block[MAX_PER_STAGE_DESCRIPTOR_UNIFORM_BLOCKS * MAX_SETS];
      uint16_t size[MAX_PER_STAGE_DESCRIPTOR_UNIFORM_BLOCKS * MAX_SETS];
      uint16_t count;
   } uniform_blocks[MESA_SHADER_STAGES];

   VkRect2D render_area;
   bool suspending;
   bool render_cond;
   uint32_t color_att_count;
   struct rvgpu_render_attachment *color_att;
   struct rvgpu_render_attachment depth_att;
   struct rvgpu_render_attachment stencil_att;
   struct rvgpu_image_view *ds_imgv;
   struct rvgpu_image_view *ds_resolve_imgv;
   uint32_t                                     forced_sample_count;
   VkResolveModeFlagBits                        forced_depth_resolve_mode;
   VkResolveModeFlagBits                        forced_stencil_resolve_mode;

   uint32_t sample_mask;
   unsigned min_samples;
   unsigned rast_samples;
   float min_sample_shading;
   bool force_min_sample;
   bool sample_shading;
   bool depth_clamp_sets_clip;

   uint32_t num_so_targets;
   struct pipe_stream_output_target *so_targets[PIPE_MAX_SO_BUFFERS];
   uint32_t so_offsets[PIPE_MAX_SO_BUFFERS];

   struct rvgpu_shader *shaders[MESA_SHADER_STAGES];

   bool tess_ccw;
   void *tess_states[2];
};

static void finish_fence(struct rendering_state *state)
{
#if 0
    struct pipe_fence_handle *handle = NULL;
    state->pctx->flush(state->pctx, &handle, 0);
    state->pctx->screen->fence_finish(state->pctx->screen, NULL, handle, PIPE_TIMEOUT_INFINITE);
    state->pctx->screen->fence_reference(state->pctx->screen, &handle, NULL);
#endif
}

static void
set_viewport_depth_xform(struct rendering_state *state, unsigned idx)
{
    double n = state->depth[idx].min;
    double f = state->depth[idx].max;

    if (!state->rs_state.clip_halfz) {
        state->viewports[idx].scale[2] = 0.5 * (f - n);
        state->viewports[idx].translate[2] = 0.5 * (n + f);
    } else {
        state->viewports[idx].scale[2] = (f - n);
        state->viewports[idx].translate[2] = n;
    }
}

static void
get_viewport_xform(struct rendering_state *state,
                   const VkViewport *viewport,
                   unsigned idx)
{
    float x = viewport->x;
    float y = viewport->y;
    float half_width = 0.5f * viewport->width;
    float half_height = 0.5f * viewport->height;

    state->viewports[idx].scale[0] = half_width;
    state->viewports[idx].translate[0] = half_width + x;
    state->viewports[idx].scale[1] = half_height;
    state->viewports[idx].translate[1] = half_height + y;

    memcpy(&state->depth[idx].min, &viewport->minDepth, sizeof(float) * 2);
}

static void
update_samples(struct rendering_state *state, VkSampleCountFlags samples)
{
    state->rast_samples = samples;
    state->rs_dirty |= state->rs_state.multisample != (samples > 1);
    state->rs_state.multisample = samples > 1;
    state->min_samples = 1;
    if (state->sample_shading) {
        state->min_samples = ceil(samples * state->min_sample_shading);
        if (state->min_samples > 1)
            state->min_samples = samples;
        if (state->min_samples < 1)
            state->min_samples = 1;
    }
    if (state->force_min_sample)
        state->min_samples = samples;
    state->min_samples_dirty = true;
    if (samples != state->framebuffer.samples) {
        state->framebuffer.samples = samples;
        // state->pctx->set_framebuffer_state(state->pctx, &state->framebuffer);
    }
}

static void
handle_graphics_stages(struct rendering_state *state, VkShaderStageFlagBits shader_stages, bool dynamic_tess_origin)
{
    u_foreach_bit(b, shader_stages) {
        VkShaderStageFlagBits vk_stage = (1 << b);
        gl_shader_stage stage = vk_to_mesa_shader_stage(vk_stage);

        state->iv_dirty[stage] |= state->num_shader_images[stage] &&
                                  (state->access[stage].images_read != state->shaders[stage]->access.images_read ||
                                   state->access[stage].images_written != state->shaders[stage]->access.images_written);
        state->sb_dirty[stage] |= state->num_shader_buffers[stage] && state->access[stage].buffers_written != state->shaders[stage]->access.buffers_written;
        memcpy(&state->access[stage], &state->shaders[stage]->access, sizeof(struct rvgpu_access_info));
        state->has_pcbuf[stage] = false;

        switch (vk_stage) {
            case VK_SHADER_STAGE_FRAGMENT_BIT:
                state->inlines_dirty[MESA_SHADER_FRAGMENT] = state->shaders[MESA_SHADER_FRAGMENT]->inlines.can_inline;
                if (!state->shaders[MESA_SHADER_FRAGMENT]->inlines.can_inline) {
                    // state->pctx->bind_fs_state(state->pctx, state->shaders[MESA_SHADER_FRAGMENT]->shader_cso);
                    state->noop_fs_bound = false;
                }
                break;
            case VK_SHADER_STAGE_VERTEX_BIT:
                state->inlines_dirty[MESA_SHADER_VERTEX] = state->shaders[MESA_SHADER_VERTEX]->inlines.can_inline;
                if (!state->shaders[MESA_SHADER_VERTEX]->inlines.can_inline)
                    // state->pctx->bind_vs_state(state->pctx, state->shaders[MESA_SHADER_VERTEX]->shader_cso);
                break;
            case VK_SHADER_STAGE_GEOMETRY_BIT:
                state->inlines_dirty[MESA_SHADER_GEOMETRY] = state->shaders[MESA_SHADER_GEOMETRY]->inlines.can_inline;
                // if (!state->shaders[MESA_SHADER_GEOMETRY]->inlines.can_inline)
                    // state->pctx->bind_gs_state(state->pctx, state->shaders[MESA_SHADER_GEOMETRY]->shader_cso);
                state->gs_output_lines = state->shaders[MESA_SHADER_GEOMETRY]->pipeline_nir->nir->info.gs.output_primitive == SHADER_PRIM_LINES ? GS_OUTPUT_LINES : GS_OUTPUT_NOT_LINES;
                break;
            case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
                state->inlines_dirty[MESA_SHADER_TESS_CTRL] = state->shaders[MESA_SHADER_TESS_CTRL]->inlines.can_inline;
                // if (!state->shaders[MESA_SHADER_TESS_CTRL]->inlines.can_inline)
                    // state->pctx->bind_tcs_state(state->pctx, state->shaders[MESA_SHADER_TESS_CTRL]->shader_cso);
                break;
            case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
                state->inlines_dirty[MESA_SHADER_TESS_EVAL] = state->shaders[MESA_SHADER_TESS_EVAL]->inlines.can_inline;
                state->tess_states[0] = NULL;
                state->tess_states[1] = NULL;
                if (!state->shaders[MESA_SHADER_TESS_EVAL]->inlines.can_inline) {
                    if (dynamic_tess_origin) {
                        state->tess_states[0] = state->shaders[MESA_SHADER_TESS_EVAL]->shader_cso;
                        state->tess_states[1] = state->shaders[MESA_SHADER_TESS_EVAL]->tess_ccw_cso;
                        // state->pctx->bind_tes_state(state->pctx, state->tess_states[state->tess_ccw]);
                    } else {
                        // state->pctx->bind_tes_state(state->pctx, state->shaders[MESA_SHADER_TESS_EVAL]->shader_cso);
                    }
                }
                if (!dynamic_tess_origin)
                    state->tess_ccw = false;
                break;
            default:
                assert(0);
                break;
        }
    }
}

static void
unbind_graphics_stages(struct rendering_state *state, VkShaderStageFlagBits shader_stages)
{
    u_foreach_bit(vkstage, shader_stages) {
        gl_shader_stage stage = vk_to_mesa_shader_stage(1<<vkstage);
        state->iv_dirty[stage] |= state->num_shader_images[stage] > 0;
        state->sb_dirty[stage] |= state->num_shader_buffers[stage] > 0;
        memset(&state->access[stage], 0, sizeof(struct rvgpu_access_info));
        state->has_pcbuf[stage] = false;
        switch (stage) {
            case MESA_SHADER_FRAGMENT:
                if (state->shaders[MESA_SHADER_FRAGMENT])
                    state->pctx->bind_fs_state(state->pctx, NULL);
                state->noop_fs_bound = false;
                break;
            case MESA_SHADER_GEOMETRY:
                if (state->shaders[MESA_SHADER_GEOMETRY])
                    state->pctx->bind_gs_state(state->pctx, NULL);
                break;
            case MESA_SHADER_TESS_CTRL:
                if (state->shaders[MESA_SHADER_TESS_CTRL])
                    state->pctx->bind_tcs_state(state->pctx, NULL);
                break;
            case MESA_SHADER_TESS_EVAL:
                if (state->shaders[MESA_SHADER_TESS_EVAL])
                    state->pctx->bind_tes_state(state->pctx, NULL);
                break;
            case MESA_SHADER_VERTEX:
                if (state->shaders[MESA_SHADER_VERTEX])
                    state->pctx->bind_vs_state(state->pctx, NULL);
                break;
            default:
                unreachable("what stage is this?!");
        }
        state->shaders[stage] = NULL;
    }
}

static void
handle_graphics_layout(struct rendering_state *state, gl_shader_stage stage, struct rvgpu_pipeline_layout *layout)
{
    state->uniform_blocks[stage].count = layout->stage[stage].uniform_block_count;
    for (unsigned j = 0; j < layout->stage[stage].uniform_block_count; j++)
        state->uniform_blocks[stage].size[j] = layout->stage[stage].uniform_block_sizes[j];
    if (layout->push_constant_stages & BITFIELD_BIT(stage)) {
        state->has_pcbuf[stage] = layout->push_constant_size > 0;
        if (!state->has_pcbuf[stage] && !state->uniform_blocks[stage].count)
            state->pcbuf_dirty[stage] = false;
    }
}

static void handle_graphics_pipeline(struct vk_cmd_queue_entry *cmd,
                                     struct rendering_state *state)
{
    RVGPU_FROM_HANDLE(rvgpu_pipeline, pipeline, cmd->u.bind_pipeline.pipeline);
    const struct vk_graphics_pipeline_state *ps = &pipeline->graphics_state;
    rvgpu_pipeline_shaders_compile(pipeline);
    bool dynamic_tess_origin = BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_TS_DOMAIN_ORIGIN);
    unbind_graphics_stages(state, (~pipeline->graphics_state.shader_stages) & VK_SHADER_STAGE_ALL_GRAPHICS);
    for (enum pipe_shader_type sh = MESA_SHADER_VERTEX; sh < MESA_SHADER_COMPUTE; sh++) {
        if (pipeline->graphics_state.shader_stages & mesa_to_vk_shader_stage(sh))
            state->shaders[sh] = &pipeline->shaders[sh];
    }
    handle_graphics_stages(state, pipeline->graphics_state.shader_stages, dynamic_tess_origin);
    for (unsigned i = 0; i < MESA_SHADER_COMPUTE; i++)
        handle_graphics_layout(state, i, pipeline->layout);
    /* rasterization state */
    if (ps->rs) {
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_DEPTH_CLAMP_ENABLE))
            state->rs_state.depth_clamp = ps->rs->depth_clamp_enable;
        if (BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_DEPTH_CLIP_ENABLE)) {
            state->depth_clamp_sets_clip = false;
        } else {
            state->depth_clamp_sets_clip =
                    ps->rs->depth_clip_enable == VK_MESA_DEPTH_CLIP_ENABLE_NOT_CLAMP;
            if (state->depth_clamp_sets_clip)
                state->rs_state.depth_clip_near = state->rs_state.depth_clip_far = !state->rs_state.depth_clamp;
            else
                state->rs_state.depth_clip_near = state->rs_state.depth_clip_far =
                        vk_rasterization_state_depth_clip_enable(ps->rs);
        }

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_RASTERIZER_DISCARD_ENABLE))
            state->rs_state.rasterizer_discard = ps->rs->rasterizer_discard_enable;

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_LINE_MODE)) {
            state->rs_state.line_smooth = pipeline->line_smooth;
            state->rs_state.line_rectangular = pipeline->line_rectangular;
        }
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_LINE_STIPPLE_ENABLE))
            state->rs_state.line_stipple_enable = ps->rs->line.stipple.enable;
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_POLYGON_MODE)) {
            state->rs_state.fill_front = vk_polygon_mode_to_pipe(ps->rs->polygon_mode);
            state->rs_state.fill_back = vk_polygon_mode_to_pipe(ps->rs->polygon_mode);
        }
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_PROVOKING_VERTEX)) {
            state->rs_state.flatshade_first =
                    ps->rs->provoking_vertex == VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT;
        }

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_LINE_WIDTH))
            state->rs_state.line_width = ps->rs->line.width;
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_LINE_STIPPLE)) {
            state->rs_state.line_stipple_factor = ps->rs->line.stipple.factor - 1;
            state->rs_state.line_stipple_pattern = ps->rs->line.stipple.pattern;
        }

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_DEPTH_BIAS_ENABLE))
            state->depth_bias.enabled = ps->rs->depth_bias.enable;
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_DEPTH_BIAS_FACTORS)) {
            state->depth_bias.offset_units = ps->rs->depth_bias.constant;
            state->depth_bias.offset_scale = ps->rs->depth_bias.slope;
            state->depth_bias.offset_clamp = ps->rs->depth_bias.clamp;
        }

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_CULL_MODE))
            state->rs_state.cull_face = vk_cull_to_pipe(ps->rs->cull_mode);

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_FRONT_FACE))
            state->rs_state.front_ccw = (ps->rs->front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE);
        state->rs_dirty = true;
    }
    if (ps->ds) {
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_TEST_ENABLE))
            state->dsa_state.depth_enabled = ps->ds->depth.test_enable;
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_WRITE_ENABLE))
            state->dsa_state.depth_writemask = ps->ds->depth.write_enable;
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_COMPARE_OP))
            state->dsa_state.depth_func = ps->ds->depth.compare_op;
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_ENABLE))
            state->dsa_state.depth_bounds_test = ps->ds->depth.bounds_test.enable;

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_BOUNDS)) {
            state->dsa_state.depth_bounds_min = ps->ds->depth.bounds_test.min;
            state->dsa_state.depth_bounds_max = ps->ds->depth.bounds_test.max;
        }

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_TEST_ENABLE)) {
            state->dsa_state.stencil[0].enabled = ps->ds->stencil.test_enable;
            state->dsa_state.stencil[1].enabled = ps->ds->stencil.test_enable;
        }

        const struct vk_stencil_test_face_state *front = &ps->ds->stencil.front;
        const struct vk_stencil_test_face_state *back = &ps->ds->stencil.back;

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_OP)) {
            state->dsa_state.stencil[0].func = front->op.compare;
            state->dsa_state.stencil[0].fail_op = vk_conv_stencil_op(front->op.fail);
            state->dsa_state.stencil[0].zpass_op = vk_conv_stencil_op(front->op.pass);
            state->dsa_state.stencil[0].zfail_op = vk_conv_stencil_op(front->op.depth_fail);

            state->dsa_state.stencil[1].func = back->op.compare;
            state->dsa_state.stencil[1].fail_op = vk_conv_stencil_op(back->op.fail);
            state->dsa_state.stencil[1].zpass_op = vk_conv_stencil_op(back->op.pass);
            state->dsa_state.stencil[1].zfail_op = vk_conv_stencil_op(back->op.depth_fail);
        }

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_COMPARE_MASK)) {
            state->dsa_state.stencil[0].valuemask = front->compare_mask;
            state->dsa_state.stencil[1].valuemask = back->compare_mask;
        }

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_WRITE_MASK)) {
            state->dsa_state.stencil[0].writemask = front->write_mask;
            state->dsa_state.stencil[1].writemask = back->write_mask;
        }

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_REFERENCE)) {
            state->stencil_ref.ref_value[0] = front->reference;
            state->stencil_ref.ref_value[1] = back->reference;
            state->stencil_ref_dirty = true;
        }
        state->dsa_dirty = true;
    }

    state->blend_state.independent_blend_enable = ps->rp->color_attachment_count > 1;
    if (ps->cb) {
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_CB_LOGIC_OP_ENABLE))
            state->blend_state.logicop_enable = ps->cb->logic_op_enable;
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_CB_LOGIC_OP))
            state->blend_state.logicop_func = vk_conv_logic_op(ps->cb->logic_op);

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES))
            state->color_write_disables = ~ps->cb->color_write_enables;

        for (unsigned i = 0; i < ps->cb->attachment_count; i++) {
            const struct vk_color_blend_attachment_state *att = &ps->cb->attachments[i];
            if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_CB_WRITE_MASKS))
                state->blend_state.rt[i].colormask = att->write_mask;
            if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_CB_BLEND_ENABLES))
                state->blend_state.rt[i].blend_enable = att->blend_enable;

            if (!att->blend_enable) {
                state->blend_state.rt[i].rgb_func = 0;
                state->blend_state.rt[i].rgb_src_factor = 0;
                state->blend_state.rt[i].rgb_dst_factor = 0;
                state->blend_state.rt[i].alpha_func = 0;
                state->blend_state.rt[i].alpha_src_factor = 0;
                state->blend_state.rt[i].alpha_dst_factor = 0;
            } else if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_CB_BLEND_EQUATIONS)) {
                state->blend_state.rt[i].rgb_func = vk_conv_blend_func(att->color_blend_op);
                state->blend_state.rt[i].rgb_src_factor = vk_conv_blend_factor(att->src_color_blend_factor);
                state->blend_state.rt[i].rgb_dst_factor = vk_conv_blend_factor(att->dst_color_blend_factor);
                state->blend_state.rt[i].alpha_func = vk_conv_blend_func(att->alpha_blend_op);
                state->blend_state.rt[i].alpha_src_factor = vk_conv_blend_factor(att->src_alpha_blend_factor);
                state->blend_state.rt[i].alpha_dst_factor = vk_conv_blend_factor(att->dst_alpha_blend_factor);
            }

            /* At least llvmpipe applies the blend factor prior to the blend function,
             * regardless of what function is used. (like i965 hardware).
             * It means for MIN/MAX the blend factor has to be stomped to ONE.
             */
            if (att->color_blend_op == VK_BLEND_OP_MIN ||
                att->color_blend_op == VK_BLEND_OP_MAX) {
                state->blend_state.rt[i].rgb_src_factor = PIPE_BLENDFACTOR_ONE;
                state->blend_state.rt[i].rgb_dst_factor = PIPE_BLENDFACTOR_ONE;
            }

            if (att->alpha_blend_op == VK_BLEND_OP_MIN ||
                att->alpha_blend_op == VK_BLEND_OP_MAX) {
                state->blend_state.rt[i].alpha_src_factor = PIPE_BLENDFACTOR_ONE;
                state->blend_state.rt[i].alpha_dst_factor = PIPE_BLENDFACTOR_ONE;
            }
        }
        state->blend_dirty = true;
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_CB_BLEND_CONSTANTS)) {
            memcpy(state->blend_color.color, ps->cb->blend_constants, 4 * sizeof(float));
            state->blend_color_dirty = true;
        }
    } else if (ps->rp->color_attachment_count == 0) {
        memset(&state->blend_state, 0, sizeof(state->blend_state));
        state->blend_dirty = true;
    }

    if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_RS_LINE_MODE))
        state->disable_multisample = pipeline->disable_multisample;
    if (ps->ms) {
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_MS_SAMPLE_MASK)) {
            state->sample_mask = ps->ms->sample_mask;
            state->sample_mask_dirty = true;
        }
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_MS_ALPHA_TO_COVERAGE_ENABLE))
            state->blend_state.alpha_to_coverage = ps->ms->alpha_to_coverage_enable;
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_MS_ALPHA_TO_ONE_ENABLE))
            state->blend_state.alpha_to_one = ps->ms->alpha_to_one_enable;
        state->force_min_sample = pipeline->force_min_sample;
        state->sample_shading = ps->ms->sample_shading_enable;
        state->min_sample_shading = ps->ms->min_sample_shading;
        state->blend_dirty = true;
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES))
            update_samples(state, ps->ms->rasterization_samples);
    } else {
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_MS_SAMPLE_MASK) &&
            !BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_MS_ALPHA_TO_ONE_ENABLE))
            state->rs_state.multisample = false;
        state->sample_shading = false;
        state->force_min_sample = false;
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_MS_SAMPLE_MASK)) {
            state->sample_mask_dirty = state->sample_mask != 0xffffffff;
            state->sample_mask = 0xffffffff;
            state->min_samples_dirty = state->min_samples;
            state->min_samples = 0;
        }
        state->blend_dirty |= state->blend_state.alpha_to_coverage || state->blend_state.alpha_to_one;
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_MS_ALPHA_TO_COVERAGE_ENABLE))
            state->blend_state.alpha_to_coverage = false;
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_MS_ALPHA_TO_ONE_ENABLE))
            state->blend_state.alpha_to_one = false;
        state->rs_dirty = true;
    }

    if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_VI_BINDING_STRIDES)) {
        u_foreach_bit(b, ps->vi->bindings_valid)
            state->vb[b].stride = ps->vi->bindings[b].stride;
        state->vb_dirty = true;
    }

    if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_VI)) {
        u_foreach_bit(a, ps->vi->attributes_valid) {
            uint32_t b = ps->vi->attributes[a].binding;
            state->velem.velems[a].src_offset = ps->vi->attributes[a].offset;
            state->velem.velems[a].vertex_buffer_index = b;
            state->velem.velems[a].src_format =
                    rvgpu_vk_format_to_pipe_format(ps->vi->attributes[a].format);
            state->velem.velems[a].dual_slot = false;

            uint32_t d = ps->vi->bindings[b].divisor;
            switch (ps->vi->bindings[b].input_rate) {
                case VK_VERTEX_INPUT_RATE_VERTEX:
                    state->velem.velems[a].instance_divisor = 0;
                    break;
                case VK_VERTEX_INPUT_RATE_INSTANCE:
                    state->velem.velems[a].instance_divisor = d ? d : UINT32_MAX;
                    break;
                default:
                    unreachable("Invalid vertex input rate");
            }
        }

        state->velem.count = util_last_bit(ps->vi->attributes_valid);
        state->vb_dirty = true;
        state->ve_dirty = true;
    }

    if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_IA_PRIMITIVE_TOPOLOGY)) {
        state->info.mode = vk_conv_topology(ps->ia->primitive_topology);
        state->rs_dirty = true;
    }
    if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_IA_PRIMITIVE_RESTART_ENABLE))
        state->info.primitive_restart = ps->ia->primitive_restart_enable;

    if (ps->ts && !BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_TS_PATCH_CONTROL_POINTS))
        state->patch_vertices = ps->ts->patch_control_points;

    if (ps->vp) {
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_VP_VIEWPORT_COUNT)) {
            state->num_viewports = ps->vp->viewport_count;
            state->vp_dirty = true;
        }
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_VP_SCISSOR_COUNT)) {
            state->num_scissors = ps->vp->scissor_count;
            state->scissor_dirty = true;
        }

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_VP_VIEWPORTS)) {
            for (uint32_t i = 0; i < ps->vp->viewport_count; i++) {
                get_viewport_xform(state, &ps->vp->viewports[i], i);
                set_viewport_depth_xform(state, i);
            }
            state->vp_dirty = true;
        }
        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_VP_SCISSORS)) {
            for (uint32_t i = 0; i < ps->vp->scissor_count; i++) {
                const VkRect2D *ss = &ps->vp->scissors[i];
                state->scissors[i].minx = ss->offset.x;
                state->scissors[i].miny = ss->offset.y;
                state->scissors[i].maxx = ss->offset.x + ss->extent.width;
                state->scissors[i].maxy = ss->offset.y + ss->extent.height;
            }
            state->scissor_dirty = true;
        }

        if (!BITSET_TEST(ps->dynamic, MESA_VK_DYNAMIC_VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE) &&
            state->rs_state.clip_halfz != !ps->vp->depth_clip_negative_one_to_one) {
            state->rs_state.clip_halfz = !ps->vp->depth_clip_negative_one_to_one;
            state->rs_dirty = true;
            for (uint32_t i = 0; i < state->num_viewports; i++)
                set_viewport_depth_xform(state, i);
            state->vp_dirty = true;
        }
    }
}
static void
handle_pipeline_access(struct rendering_state *state, gl_shader_stage stage)
{
    enum pipe_shader_type pstage = pipe_shader_type_from_mesa(stage);
    for (unsigned i = 0; i < PIPE_MAX_SHADER_IMAGES; i++) {
        state->iv[pstage][i].access = 0;
        state->iv[pstage][i].shader_access = 0;
    }
    u_foreach_bit64(idx, state->access[stage].images_read) {
        state->iv[pstage][idx].access |= PIPE_IMAGE_ACCESS_READ;
        state->iv[pstage][idx].shader_access |= PIPE_IMAGE_ACCESS_READ;
    }
    u_foreach_bit64(idx, state->access[stage].images_written) {
        state->iv[pstage][idx].access |= PIPE_IMAGE_ACCESS_WRITE;
        state->iv[pstage][idx].shader_access |= PIPE_IMAGE_ACCESS_WRITE;
    }
}

static void handle_pipeline(struct vk_cmd_queue_entry *cmd,
                            struct rendering_state *state)
{
    RVGPU_FROM_HANDLE(rvgpu_pipeline, pipeline, cmd->u.bind_pipeline.pipeline);
    pipeline->used = true;
    if (pipeline->is_compute_pipeline) {
#if 0 // TODO.zac handle compute pipeline
        handle_compute_pipeline(cmd, state);
        handle_pipeline_access(state, MESA_SHADER_COMPUTE);
#endif
    } else {
        handle_graphics_pipeline(cmd, state);
        for (unsigned i = 0; i < MESA_SHADER_COMPUTE; i++)
            handle_pipeline_access(state, i);
    }
    state->push_size[pipeline->is_compute_pipeline] = pipeline->layout->push_constant_size;
}

static void add_img_view_surface(struct rendering_state *state,
                                 struct rvgpu_image_view *imgv, int width, int height,
                                 int layer_count)
{
#if 0
    if (imgv->surface) {
        if (imgv->surface->width != width ||
            imgv->surface->height != height ||
            (imgv->surface->u.tex.last_layer - imgv->surface->u.tex.first_layer) != (layer_count - 1))
            pipe_surface_reference(&imgv->surface, NULL);
    }

    if (!imgv->surface) {
        imgv->surface = create_img_surface(state, imgv, imgv->vk.format,
                                           width, height,
                                           0, layer_count);
    }
#endif
}

static bool
render_needs_clear(struct rendering_state *state)
{
    for (uint32_t i = 0; i < state->color_att_count; i++) {
        if (state->color_att[i].load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
            return true;
    }
    if (state->depth_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
        return true;
    if (state->stencil_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
        return true;
    return false;
}

static void render_clear(struct rendering_state *state)
{
#if 0
    for (uint32_t i = 0; i < state->color_att_count; i++) {
        if (state->color_att[i].load_op != VK_ATTACHMENT_LOAD_OP_CLEAR)
            continue;

        union pipe_color_union color_clear_val = { 0 };
        const VkClearValue value = state->color_att[i].clear_value;
        color_clear_val.ui[0] = value.color.uint32[0];
        color_clear_val.ui[1] = value.color.uint32[1];
        color_clear_val.ui[2] = value.color.uint32[2];
        color_clear_val.ui[3] = value.color.uint32[3];

        struct rvgpu_image_view *imgv = state->color_att[i].imgv;
        assert(imgv->surface);

        if (state->info.view_mask) {
            u_foreach_bit(i, state->info.view_mask)
                clear_attachment_layers(state, imgv, &state->render_area,
                                        i, 1, 0, 0, 0, &color_clear_val);
        } else {
            state->pctx->clear_render_target(state->pctx,
                                             imgv->surface,
                                             &color_clear_val,
                                             state->render_area.offset.x,
                                             state->render_area.offset.y,
                                             state->render_area.extent.width,
                                             state->render_area.extent.height,
                                             false);
        }
    }

    uint32_t ds_clear_flags = 0;
    double dclear_val = 0;
    if (state->depth_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
        ds_clear_flags |= PIPE_CLEAR_DEPTH;
        dclear_val = state->depth_att.clear_value.depthStencil.depth;
    }

    uint32_t sclear_val = 0;
    if (state->stencil_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
        ds_clear_flags |= PIPE_CLEAR_STENCIL;
        sclear_val = state->stencil_att.clear_value.depthStencil.stencil;
    }

    if (ds_clear_flags) {
        if (state->info.view_mask) {
            u_foreach_bit(i, state->info.view_mask)
                clear_attachment_layers(state, state->ds_imgv, &state->render_area,
                                        i, 1, ds_clear_flags, dclear_val, sclear_val, NULL);
        } else {
            state->pctx->clear_depth_stencil(state->pctx,
                                             state->ds_imgv->surface,
                                             ds_clear_flags,
                                             dclear_val, sclear_val,
                                             state->render_area.offset.x,
                                             state->render_area.offset.y,
                                             state->render_area.extent.width,
                                             state->render_area.extent.height,
                                             false);
        }
    }
#endif
}

static void render_clear_fast(struct rendering_state *state)
{
    /*
     * the state tracker clear interface only works if all the attachments have the same
     * clear color.
     */
    /* llvmpipe doesn't support scissored clears yet */
    if (state->render_area.offset.x || state->render_area.offset.y)
        goto slow_clear;

    if (state->render_area.extent.width != state->framebuffer.width ||
        state->render_area.extent.height != state->framebuffer.height)
        goto slow_clear;

    if (state->info.view_mask)
        goto slow_clear;

    if (state->render_cond)
        goto slow_clear;

    uint32_t buffers = 0;
    bool has_color_value = false;
    VkClearValue color_value = {0};
    for (uint32_t i = 0; i < state->color_att_count; i++) {
        if (state->color_att[i].load_op != VK_ATTACHMENT_LOAD_OP_CLEAR)
            continue;

        buffers |= (PIPE_CLEAR_COLOR0 << i);

        if (has_color_value) {
            if (memcmp(&color_value, &state->color_att[i].clear_value, sizeof(VkClearValue)))
                goto slow_clear;
        } else {
            memcpy(&color_value, &state->color_att[i].clear_value, sizeof(VkClearValue));
            has_color_value = true;
        }
    }

    double dclear_val = 0;
    if (state->depth_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
        buffers |= PIPE_CLEAR_DEPTH;
        dclear_val = state->depth_att.clear_value.depthStencil.depth;
    }

    uint32_t sclear_val = 0;
    if (state->stencil_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
        buffers |= PIPE_CLEAR_STENCIL;
        sclear_val = state->stencil_att.clear_value.depthStencil.stencil;
    }

    union pipe_color_union col_val;
    for (unsigned i = 0; i < 4; i++)
        col_val.ui[i] = color_value.color.uint32[i];
#if 0
    state->pctx->clear(state->pctx, buffers,
                       NULL, &col_val,
                       dclear_val, sclear_val);
#endif
    return;

slow_clear:
    render_clear(state);
}

static void render_att_init(struct rvgpu_render_attachment* att,
                            const VkRenderingAttachmentInfo *vk_att, bool poison_mem, bool stencil)
{
    if (vk_att == NULL || vk_att->imageView == VK_NULL_HANDLE) {
        *att = (struct rvgpu_render_attachment) {
                .load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        };
        return;
    }

    *att = (struct rvgpu_render_attachment) {
            .imgv = rvgpu_image_view_from_handle(vk_att->imageView),
            .load_op = vk_att->loadOp,
            .store_op = vk_att->storeOp,
            .clear_value = vk_att->clearValue,
    };
    if (util_format_is_depth_or_stencil(att->imgv->pformat)) {
        if (stencil)
            att->read_only = vk_att->imageLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL ||
                             vk_att->imageLayout == VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL;
        else
            att->read_only = vk_att->imageLayout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL ||
                             vk_att->imageLayout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    }
    if (poison_mem && !att->read_only && att->load_op == VK_ATTACHMENT_LOAD_OP_DONT_CARE) {
        att->load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
        if (util_format_is_depth_or_stencil(att->imgv->pformat)) {
            att->clear_value.depthStencil.depth = 0.12351251;
            att->clear_value.depthStencil.stencil = rand() % UINT8_MAX;
        } else {
            memset(att->clear_value.color.uint32, rand() % UINT8_MAX, sizeof(att->clear_value.color.uint32));
        }
    }
    if (vk_att->resolveImageView && vk_att->resolveMode) {
        att->resolve_imgv = rvgpu_image_view_from_handle(vk_att->resolveImageView);
        att->resolve_mode = vk_att->resolveMode;
    }
}

static void handle_begin_rendering(struct vk_cmd_queue_entry *cmd, struct rendering_state *state)
{
    const VkRenderingInfo *info = cmd->u.begin_rendering.rendering_info;
    bool resuming = (info->flags & VK_RENDERING_RESUMING_BIT) == VK_RENDERING_RESUMING_BIT;
    bool suspending = (info->flags & VK_RENDERING_SUSPENDING_BIT) == VK_RENDERING_SUSPENDING_BIT;

    const VkMultisampledRenderToSingleSampledInfoEXT *ssi =
            vk_find_struct_const(info->pNext, MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_INFO_EXT);
    if (ssi && ssi->multisampledRenderToSingleSampledEnable) {
        state->forced_sample_count = ssi->rasterizationSamples;
        state->forced_depth_resolve_mode = info->pDepthAttachment ? info->pDepthAttachment->resolveMode : 0;
        state->forced_stencil_resolve_mode = info->pStencilAttachment ? info->pStencilAttachment->resolveMode : 0;
    } else {
        state->forced_sample_count = 0;
        state->forced_depth_resolve_mode = 0;
        state->forced_stencil_resolve_mode = 0;
    }

    state->info.view_mask = info->viewMask;
    state->render_area = info->renderArea;
    state->suspending = suspending;
    state->framebuffer.width = info->renderArea.offset.x +
                               info->renderArea.extent.width;
    state->framebuffer.height = info->renderArea.offset.y +
                                info->renderArea.extent.height;
    state->framebuffer.layers = info->viewMask ? util_last_bit(info->viewMask) : info->layerCount;
    state->framebuffer.nr_cbufs = info->colorAttachmentCount;

    state->color_att_count = info->colorAttachmentCount;
    state->color_att = realloc(state->color_att, sizeof(*state->color_att) * state->color_att_count);

    for (unsigned i = 0; i < info->colorAttachmentCount; i++) {
        render_att_init(&state->color_att[i], &info->pColorAttachments[i], state->poison_mem, false);
        if (state->color_att[i].imgv) {
            struct rvgpu_image_view *imgv = state->color_att[i].imgv;
            add_img_view_surface(state, imgv,
                                 state->framebuffer.width, state->framebuffer.height,
                                 state->framebuffer.layers);
#if 0 // TODO.zac multisample
            if (state->forced_sample_count && imgv->image->vk.samples == 1)
                state->color_att[i].imgv = create_multisample_surface(state, imgv, state->forced_sample_count,
                                                                      att_needs_replicate(state, imgv, state->color_att[i].load_op));
            state->framebuffer.cbufs[i] = state->color_att[i].imgv->surface;
            assert(state->render_area.offset.x + state->render_area.extent.width <= state->framebuffer.cbufs[i]->texture->width0);
            assert(state->render_area.offset.y + state->render_area.extent.height <= state->framebuffer.cbufs[i]->texture->height0);
#endif
        } else {
            state->framebuffer.cbufs[i] = NULL;
        }
    }

    render_att_init(&state->depth_att, info->pDepthAttachment, state->poison_mem, false);
    render_att_init(&state->stencil_att, info->pStencilAttachment, state->poison_mem, true);
    if (state->depth_att.imgv || state->stencil_att.imgv) {
        assert(state->depth_att.imgv == NULL ||
               state->stencil_att.imgv == NULL ||
               state->depth_att.imgv == state->stencil_att.imgv);
        state->ds_imgv = state->depth_att.imgv ? state->depth_att.imgv :
                         state->stencil_att.imgv;
        struct rvgpu_image_view *imgv = state->ds_imgv;
        add_img_view_surface(state, imgv,
                             state->framebuffer.width, state->framebuffer.height,
                             state->framebuffer.layers);
#if 0 // TODO.zac multisample
        if (state->forced_sample_count && imgv->image->vk.samples == 1) {
            VkAttachmentLoadOp load_op;
            if (state->depth_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR ||
                state->stencil_att.load_op == VK_ATTACHMENT_LOAD_OP_CLEAR)
                load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
            else if (state->depth_att.load_op == VK_ATTACHMENT_LOAD_OP_LOAD ||
                     state->stencil_att.load_op == VK_ATTACHMENT_LOAD_OP_LOAD)
                load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
            else
                load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            state->ds_imgv = create_multisample_surface(state, imgv, state->forced_sample_count,
                                                        att_needs_replicate(state, imgv, load_op));
        }
#endif
        state->framebuffer.zsbuf = state->ds_imgv->surface;
        assert(state->render_area.offset.x + state->render_area.extent.width <= state->framebuffer.zsbuf->texture->width0);
        assert(state->render_area.offset.y + state->render_area.extent.height <= state->framebuffer.zsbuf->texture->height0);
    } else {
        state->ds_imgv = NULL;
        state->framebuffer.zsbuf = NULL;
    }

    // state->pctx->set_framebuffer_state(state->pctx, &state->framebuffer);  // TODO.zac set_framebuffer_state
    if (!resuming && render_needs_clear(state))
        render_clear_fast(state);
}

static void set_viewport(unsigned first_viewport, unsigned viewport_count,
                         const VkViewport* viewports,
                         struct rendering_state *state)
{
   int i;
   unsigned base = 0;
   if (first_viewport == UINT32_MAX)
      state->num_viewports = viewport_count;
   else
      base = first_viewport;

   for (i = 0; i < viewport_count; i++) {
      int idx = i + base;
      const VkViewport *vp = &viewports[i];
      get_viewport_xform(state, vp, idx);
      set_viewport_depth_xform(state, idx);
   }
   state->vp_dirty = true;
}

static void handle_set_viewport(struct vk_cmd_queue_entry *cmd,
                                struct rendering_state *state)
{
   set_viewport(cmd->u.set_viewport.first_viewport,
                cmd->u.set_viewport.viewport_count,
                cmd->u.set_viewport.viewports,
                state);
}

static void handle_pipeline_barrier(struct vk_cmd_queue_entry *cmd,
                                    struct rendering_state *state)
{
    finish_fence(state);
}

void rvgpu_add_enqueue_cmd_entrypoints(struct vk_device_dispatch_table *disp)
{
   struct vk_device_dispatch_table cmd_enqueue_dispatch;
   vk_device_dispatch_table_from_entrypoints(&cmd_enqueue_dispatch,
      &vk_cmd_enqueue_device_entrypoints, true);

#define ENQUEUE_CMD(CmdName) \
   assert(cmd_enqueue_dispatch.CmdName != NULL); \
   disp->CmdName = cmd_enqueue_dispatch.CmdName;

   /* This list needs to match what's in rvgpu_execute_cmd_buffer exactly */
   ENQUEUE_CMD(CmdBindPipeline)
   ENQUEUE_CMD(CmdSetViewport)
   ENQUEUE_CMD(CmdSetViewportWithCount)
   ENQUEUE_CMD(CmdSetScissor)
   ENQUEUE_CMD(CmdSetScissorWithCount)
   ENQUEUE_CMD(CmdSetLineWidth)
   ENQUEUE_CMD(CmdSetDepthBias)
   ENQUEUE_CMD(CmdSetBlendConstants)
   ENQUEUE_CMD(CmdSetDepthBounds)
   ENQUEUE_CMD(CmdSetStencilCompareMask)
   ENQUEUE_CMD(CmdSetStencilWriteMask)
   ENQUEUE_CMD(CmdSetStencilReference)
   ENQUEUE_CMD(CmdBindDescriptorSets)
   ENQUEUE_CMD(CmdBindIndexBuffer)
   ENQUEUE_CMD(CmdBindVertexBuffers2)
   ENQUEUE_CMD(CmdDraw)
   ENQUEUE_CMD(CmdDrawMultiEXT)
   ENQUEUE_CMD(CmdDrawIndexed)
   ENQUEUE_CMD(CmdDrawIndirect)
   ENQUEUE_CMD(CmdDrawIndexedIndirect)
   ENQUEUE_CMD(CmdDrawMultiIndexedEXT)
   ENQUEUE_CMD(CmdDispatch)
   ENQUEUE_CMD(CmdDispatchBase)
   ENQUEUE_CMD(CmdDispatchIndirect)
   ENQUEUE_CMD(CmdCopyBuffer2)
   ENQUEUE_CMD(CmdCopyImage2)
   ENQUEUE_CMD(CmdBlitImage2)
   ENQUEUE_CMD(CmdCopyBufferToImage2)
   ENQUEUE_CMD(CmdCopyImageToBuffer2)
   ENQUEUE_CMD(CmdUpdateBuffer)
   ENQUEUE_CMD(CmdFillBuffer)
   ENQUEUE_CMD(CmdClearColorImage)
   ENQUEUE_CMD(CmdClearDepthStencilImage)
   ENQUEUE_CMD(CmdClearAttachments)
   ENQUEUE_CMD(CmdResolveImage2)
   ENQUEUE_CMD(CmdBeginQueryIndexedEXT)
   ENQUEUE_CMD(CmdEndQueryIndexedEXT)
   ENQUEUE_CMD(CmdBeginQuery)
   ENQUEUE_CMD(CmdEndQuery)
   ENQUEUE_CMD(CmdResetQueryPool)
   ENQUEUE_CMD(CmdCopyQueryPoolResults)
   ENQUEUE_CMD(CmdPushConstants)
   ENQUEUE_CMD(CmdExecuteCommands)
   ENQUEUE_CMD(CmdDrawIndirectCount)
   ENQUEUE_CMD(CmdDrawIndexedIndirectCount)
   ENQUEUE_CMD(CmdPushDescriptorSetKHR)
//   ENQUEUE_CMD(CmdPushDescriptorSetWithTemplateKHR)
   ENQUEUE_CMD(CmdBindTransformFeedbackBuffersEXT)
   ENQUEUE_CMD(CmdBeginTransformFeedbackEXT)
   ENQUEUE_CMD(CmdEndTransformFeedbackEXT)
   ENQUEUE_CMD(CmdDrawIndirectByteCountEXT)
   ENQUEUE_CMD(CmdBeginConditionalRenderingEXT)
   ENQUEUE_CMD(CmdEndConditionalRenderingEXT)
   ENQUEUE_CMD(CmdSetVertexInputEXT)
   ENQUEUE_CMD(CmdSetCullMode)
   ENQUEUE_CMD(CmdSetFrontFace)
   ENQUEUE_CMD(CmdSetPrimitiveTopology)
   ENQUEUE_CMD(CmdSetDepthTestEnable)
   ENQUEUE_CMD(CmdSetDepthWriteEnable)
   ENQUEUE_CMD(CmdSetDepthCompareOp)
   ENQUEUE_CMD(CmdSetDepthBoundsTestEnable)
   ENQUEUE_CMD(CmdSetStencilTestEnable)
   ENQUEUE_CMD(CmdSetStencilOp)
   ENQUEUE_CMD(CmdSetLineStippleEXT)
   ENQUEUE_CMD(CmdSetDepthBiasEnable)
   ENQUEUE_CMD(CmdSetLogicOpEXT)
   ENQUEUE_CMD(CmdSetPatchControlPointsEXT)
   ENQUEUE_CMD(CmdSetPrimitiveRestartEnable)
   ENQUEUE_CMD(CmdSetRasterizerDiscardEnable)
   ENQUEUE_CMD(CmdSetColorWriteEnableEXT)
   ENQUEUE_CMD(CmdBeginRendering)
   ENQUEUE_CMD(CmdEndRendering)
   ENQUEUE_CMD(CmdSetDeviceMask)
   ENQUEUE_CMD(CmdPipelineBarrier2)
   ENQUEUE_CMD(CmdResetEvent2)
   ENQUEUE_CMD(CmdSetEvent2)
   ENQUEUE_CMD(CmdWaitEvents2)
   ENQUEUE_CMD(CmdWriteTimestamp2)

   ENQUEUE_CMD(CmdSetPolygonModeEXT)
   ENQUEUE_CMD(CmdSetTessellationDomainOriginEXT)
   ENQUEUE_CMD(CmdSetDepthClampEnableEXT)
   ENQUEUE_CMD(CmdSetDepthClipEnableEXT)
   ENQUEUE_CMD(CmdSetLogicOpEnableEXT)
   ENQUEUE_CMD(CmdSetSampleMaskEXT)
   ENQUEUE_CMD(CmdSetRasterizationSamplesEXT)
   ENQUEUE_CMD(CmdSetAlphaToCoverageEnableEXT)
   ENQUEUE_CMD(CmdSetAlphaToOneEnableEXT)
   ENQUEUE_CMD(CmdSetDepthClipNegativeOneToOneEXT)
   ENQUEUE_CMD(CmdSetLineRasterizationModeEXT)
   ENQUEUE_CMD(CmdSetLineStippleEnableEXT)
   ENQUEUE_CMD(CmdSetProvokingVertexModeEXT)
   ENQUEUE_CMD(CmdSetColorBlendEnableEXT)
   ENQUEUE_CMD(CmdSetColorBlendEquationEXT)
   ENQUEUE_CMD(CmdSetColorWriteMaskEXT)

   ENQUEUE_CMD(CmdBindShadersEXT)
   /* required for EXT_shader_object */
   ENQUEUE_CMD(CmdSetCoverageModulationModeNV)
   ENQUEUE_CMD(CmdSetCoverageModulationTableEnableNV)
   ENQUEUE_CMD(CmdSetCoverageModulationTableNV)
   ENQUEUE_CMD(CmdSetCoverageReductionModeNV)
   ENQUEUE_CMD(CmdSetCoverageToColorEnableNV)
   ENQUEUE_CMD(CmdSetCoverageToColorLocationNV)
   ENQUEUE_CMD(CmdSetRepresentativeFragmentTestEnableNV)
   ENQUEUE_CMD(CmdSetShadingRateImageEnableNV)
   ENQUEUE_CMD(CmdSetViewportSwizzleNV)
   ENQUEUE_CMD(CmdSetViewportWScalingEnableNV)

#undef ENQUEUE_CMD
}

static void rvgpu_execute_cmd_buffer(struct rvgpu_cmd_buffer *cmd_buffer,
                                     struct rendering_state *state, bool print_cmds)
{
   struct vk_cmd_queue_entry *cmd;
   bool first = true;
   bool did_flush = false;

   LIST_FOR_EACH_ENTRY(cmd, &cmd_buffer->vk.cmd_queue.cmds, cmd_link) {
      if (print_cmds)
         fprintf(stderr, "%s\n", vk_cmd_queue_type_names[cmd->type]);
      switch (cmd->type) {
      case VK_CMD_BIND_PIPELINE:
         handle_pipeline(cmd, state);
         break;
      case VK_CMD_SET_VIEWPORT:
         handle_set_viewport(cmd, state);
         break;
      case VK_CMD_SET_VIEWPORT_WITH_COUNT:
         // // handle_set_viewport_with_count(cmd, state);
         break;
      case VK_CMD_SET_SCISSOR:
         // // handle_set_scissor(cmd, state);
         break;
      case VK_CMD_SET_SCISSOR_WITH_COUNT:
         // // handle_set_scissor_with_count(cmd, state);
         break;
      case VK_CMD_SET_LINE_WIDTH:
         // // handle_set_line_width(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_BIAS:
         // handle_set_depth_bias(cmd, state);
         break;
      case VK_CMD_SET_BLEND_CONSTANTS:
         // handle_set_blend_constants(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_BOUNDS:
         // handle_set_depth_bounds(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_COMPARE_MASK:
         // handle_set_stencil_compare_mask(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_WRITE_MASK:
         // handle_set_stencil_write_mask(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_REFERENCE:
         // handle_set_stencil_reference(cmd, state);
         break;
      case VK_CMD_BIND_DESCRIPTOR_SETS:
         // handle_descriptor_sets(cmd, state);
         break;
      case VK_CMD_BIND_INDEX_BUFFER:
         // handle_index_buffer(cmd, state);
         break;
      case VK_CMD_BIND_VERTEX_BUFFERS2:
         // handle_vertex_buffers2(cmd, state);
         break;
      case VK_CMD_DRAW:
         // emit_state(state);
         // handle_draw(cmd, state);
         break;
      case VK_CMD_DRAW_MULTI_EXT:
         // emit_state(state);
         // handle_draw_multi(cmd, state);
         break;
      case VK_CMD_DRAW_INDEXED:
         // emit_state(state);
         // handle_draw_indexed(cmd, state);
         break;
      case VK_CMD_DRAW_INDIRECT:
         // emit_state(state);
         // handle_draw_indirect(cmd, state, false);
         break;
      case VK_CMD_DRAW_INDEXED_INDIRECT:
         // emit_state(state);
         // handle_draw_indirect(cmd, state, true);
         break;
      case VK_CMD_DRAW_MULTI_INDEXED_EXT:
         // emit_state(state);
         // handle_draw_multi_indexed(cmd, state);
         break;
      case VK_CMD_DISPATCH:
         // emit_compute_state(state);
         // handle_dispatch(cmd, state);
         break;
      case VK_CMD_DISPATCH_BASE:
         // emit_compute_state(state);
         // handle_dispatch_base(cmd, state);
         break;
      case VK_CMD_DISPATCH_INDIRECT:
         // emit_compute_state(state);
         // handle_dispatch_indirect(cmd, state);
         break;
      case VK_CMD_COPY_BUFFER2:
         // handle_copy_buffer(cmd, state);
         break;
      case VK_CMD_COPY_IMAGE2:
         // handle_copy_image(cmd, state);
         break;
      case VK_CMD_BLIT_IMAGE2:
         // handle_blit_image(cmd, state);
         break;
      case VK_CMD_COPY_BUFFER_TO_IMAGE2:
         // handle_copy_buffer_to_image(cmd, state);
         break;
      case VK_CMD_COPY_IMAGE_TO_BUFFER2:
         // handle_copy_image_to_buffer2(cmd, state);
         break;
      case VK_CMD_UPDATE_BUFFER:
         // handle_update_buffer(cmd, state);
         break;
      case VK_CMD_FILL_BUFFER:
         // handle_fill_buffer(cmd, state);
         break;
      case VK_CMD_CLEAR_COLOR_IMAGE:
         // handle_clear_color_image(cmd, state);
         break;
      case VK_CMD_CLEAR_DEPTH_STENCIL_IMAGE:
         // handle_clear_ds_image(cmd, state);
         break;
      case VK_CMD_CLEAR_ATTACHMENTS:
         // handle_clear_attachments(cmd, state);
         break;
      case VK_CMD_RESOLVE_IMAGE2:
         // handle_resolve_image(cmd, state);
         break;
      case VK_CMD_PIPELINE_BARRIER2:
         /* skip flushes since every cmdbuf does a flush
            after iterating its cmds and so this is redundant
          */
         if (first || did_flush || cmd->cmd_link.next == &cmd_buffer->vk.cmd_queue.cmds)
            continue;
         handle_pipeline_barrier(cmd, state);
         did_flush = true;
         continue;
      case VK_CMD_BEGIN_QUERY_INDEXED_EXT:
         // handle_begin_query_indexed_ext(cmd, state);
         break;
      case VK_CMD_END_QUERY_INDEXED_EXT:
         // handle_end_query_indexed_ext(cmd, state);
         break;
      case VK_CMD_BEGIN_QUERY:
         // handle_begin_query(cmd, state);
         break;
      case VK_CMD_END_QUERY:
         // handle_end_query(cmd, state);
         break;
      case VK_CMD_RESET_QUERY_POOL:
         // handle_reset_query_pool(cmd, state);
         break;
      case VK_CMD_COPY_QUERY_POOL_RESULTS:
         // handle_copy_query_pool_results(cmd, state);
         break;
      case VK_CMD_PUSH_CONSTANTS:
         // handle_push_constants(cmd, state);
         break;
      case VK_CMD_EXECUTE_COMMANDS:
         // handle_execute_commands(cmd, state, print_cmds);
         break;
      case VK_CMD_DRAW_INDIRECT_COUNT:
         // emit_state(state);
         // handle_draw_indirect_count(cmd, state, false);
         break;
      case VK_CMD_DRAW_INDEXED_INDIRECT_COUNT:
         // emit_state(state);
         // handle_draw_indirect_count(cmd, state, true);
         break;
      case VK_CMD_PUSH_DESCRIPTOR_SET_KHR:
         // handle_push_descriptor_set(cmd, state);
         break;
      case VK_CMD_PUSH_DESCRIPTOR_SET_WITH_TEMPLATE_KHR:
         // handle_push_descriptor_set_with_template(cmd, state);
         break;
      case VK_CMD_BIND_TRANSFORM_FEEDBACK_BUFFERS_EXT:
         // handle_bind_transform_feedback_buffers(cmd, state);
         break;
      case VK_CMD_BEGIN_TRANSFORM_FEEDBACK_EXT:
         // handle_begin_transform_feedback(cmd, state);
         break;
      case VK_CMD_END_TRANSFORM_FEEDBACK_EXT:
         // handle_end_transform_feedback(cmd, state);
         break;
      case VK_CMD_DRAW_INDIRECT_BYTE_COUNT_EXT:
         // emit_state(state);
         // handle_draw_indirect_byte_count(cmd, state);
         break;
      case VK_CMD_BEGIN_CONDITIONAL_RENDERING_EXT:
         // handle_begin_conditional_rendering(cmd, state);
         break;
      case VK_CMD_END_CONDITIONAL_RENDERING_EXT:
         // handle_end_conditional_rendering(state);
         break;
      case VK_CMD_SET_VERTEX_INPUT_EXT:
         // handle_set_vertex_input(cmd, state);
         break;
      case VK_CMD_SET_CULL_MODE:
         // handle_set_cull_mode(cmd, state);
         break;
      case VK_CMD_SET_FRONT_FACE:
         // handle_set_front_face(cmd, state);
         break;
      case VK_CMD_SET_PRIMITIVE_TOPOLOGY:
         // handle_set_primitive_topology(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_TEST_ENABLE:
         // handle_set_depth_test_enable(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_WRITE_ENABLE:
         // handle_set_depth_write_enable(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_COMPARE_OP:
         // handle_set_depth_compare_op(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_BOUNDS_TEST_ENABLE:
         // handle_set_depth_bounds_test_enable(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_TEST_ENABLE:
         // handle_set_stencil_test_enable(cmd, state);
         break;
      case VK_CMD_SET_STENCIL_OP:
         // handle_set_stencil_op(cmd, state);
         break;
      case VK_CMD_SET_LINE_STIPPLE_EXT:
         // handle_set_line_stipple(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_BIAS_ENABLE:
         // handle_set_depth_bias_enable(cmd, state);
         break;
      case VK_CMD_SET_LOGIC_OP_EXT:
         // handle_set_logic_op(cmd, state);
         break;
      case VK_CMD_SET_PATCH_CONTROL_POINTS_EXT:
         // handle_set_patch_control_points(cmd, state);
         break;
      case VK_CMD_SET_PRIMITIVE_RESTART_ENABLE:
         // handle_set_primitive_restart_enable(cmd, state);
         break;
      case VK_CMD_SET_RASTERIZER_DISCARD_ENABLE:
         // handle_set_rasterizer_discard_enable(cmd, state);
         break;
      case VK_CMD_SET_COLOR_WRITE_ENABLE_EXT:
         // handle_set_color_write_enable(cmd, state);
         break;
      case VK_CMD_BEGIN_RENDERING:
         // handle_begin_rendering(cmd, state);
         break;
      case VK_CMD_END_RENDERING:
         // handle_end_rendering(cmd, state);
         break;
      case VK_CMD_SET_DEVICE_MASK:
         /* no-op */
         break;
      case VK_CMD_RESET_EVENT2:
         // handle_event_reset2(cmd, state);
         break;
      case VK_CMD_SET_EVENT2:
         // handle_event_set2(cmd, state);
         break;
      case VK_CMD_WAIT_EVENTS2:
         // handle_wait_events2(cmd, state);
         break;
      case VK_CMD_WRITE_TIMESTAMP2:
         // handle_write_timestamp2(cmd, state);
         break;

      case VK_CMD_SET_POLYGON_MODE_EXT:
         // handle_set_polygon_mode(cmd, state);
         break;
      case VK_CMD_SET_TESSELLATION_DOMAIN_ORIGIN_EXT:
         // handle_set_tessellation_domain_origin(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_CLAMP_ENABLE_EXT:
         // handle_set_depth_clamp_enable(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_CLIP_ENABLE_EXT:
         // handle_set_depth_clip_enable(cmd, state);
         break;
      case VK_CMD_SET_LOGIC_OP_ENABLE_EXT:
         // handle_set_logic_op_enable(cmd, state);
         break;
      case VK_CMD_SET_SAMPLE_MASK_EXT:
         // handle_set_sample_mask(cmd, state);
         break;
      case VK_CMD_SET_RASTERIZATION_SAMPLES_EXT:
         // handle_set_samples(cmd, state);
         break;
      case VK_CMD_SET_ALPHA_TO_COVERAGE_ENABLE_EXT:
         // handle_set_alpha_to_coverage(cmd, state);
         break;
      case VK_CMD_SET_ALPHA_TO_ONE_ENABLE_EXT:
         // handle_set_alpha_to_one(cmd, state);
         break;
      case VK_CMD_SET_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT:
         // handle_set_halfz(cmd, state);
         break;
      case VK_CMD_SET_LINE_RASTERIZATION_MODE_EXT:
         // handle_set_line_rasterization_mode(cmd, state);
         break;
      case VK_CMD_SET_LINE_STIPPLE_ENABLE_EXT:
         // handle_set_line_stipple_enable(cmd, state);
         break;
      case VK_CMD_SET_PROVOKING_VERTEX_MODE_EXT:
         // handle_set_provoking_vertex_mode(cmd, state);
         break;
      case VK_CMD_SET_COLOR_BLEND_ENABLE_EXT:
         // handle_set_color_blend_enable(cmd, state);
         break;
      case VK_CMD_SET_COLOR_WRITE_MASK_EXT:
         // handle_set_color_write_mask(cmd, state);
         break;
      case VK_CMD_SET_COLOR_BLEND_EQUATION_EXT:
         // handle_set_color_blend_equation(cmd, state);
         break;
      case VK_CMD_BIND_SHADERS_EXT:
         // handle_shaders(cmd, state);
         break;

      default:
         fprintf(stderr, "Unsupported command %s\n", vk_cmd_queue_type_names[cmd->type]);
         unreachable("Unsupported command");
         break;
      }
      first = false;
      did_flush = false;
   }
}

VkResult rvgpu_execute_cmds(struct rvgpu_device *device,
                            struct rvgpu_queue *queue,
                            struct rvgpu_cmd_buffer *cmd_buffer)
{
   struct rendering_state *state = queue->state;
   memset(state, 0, sizeof(*state));
   state->pctx = queue->ctx;
   state->device = device;
   state->uploader = queue->uploader;
   state->cso = queue->cso;
   state->blend_dirty = true;
   state->dsa_dirty = true;
   state->rs_dirty = true;
   state->vp_dirty = true;
   state->rs_state.point_tri_clip = true;
   state->rs_state.unclamped_fragment_depth_values = device->vk.enabled_extensions.EXT_depth_range_unrestricted;
   state->sample_mask_dirty = true;
   state->min_samples_dirty = true;
   state->sample_mask = UINT32_MAX;
   state->poison_mem = device->poison_mem;

   /* default values */
   state->rs_state.line_width = 1.0;
   state->rs_state.flatshade_first = true;
   state->rs_state.clip_halfz = true;
   state->rs_state.front_ccw = true;
   state->rs_state.point_size_per_vertex = true;
   state->rs_state.point_quad_rasterization = true;
   state->rs_state.half_pixel_center = true;
   state->rs_state.scissor = true;
   state->rs_state.no_ms_sample_mask_out = true;

   for (enum pipe_shader_type s = MESA_SHADER_VERTEX; s < MESA_SHADER_STAGES; s++) {
      for (unsigned i = 0; i < ARRAY_SIZE(state->cso_ss_ptr[s]); i++)
         state->cso_ss_ptr[s][i] = &state->ss[s][i];
   }
   /* create a gallium context */
   rvgpu_execute_cmd_buffer(cmd_buffer, state, false);

   state->start_vb = -1;
   state->num_vb = 0;
   // cso_unbind_context(queue->cso);
   for (unsigned i = 0; i < ARRAY_SIZE(state->so_targets); i++) {
      if (state->so_targets[i]) {
         state->pctx->stream_output_target_destroy(state->pctx, state->so_targets[i]);
      }
   }

   free(state->color_att);
   return VK_SUCCESS;
}

void *rvgpu_init_queue_rendering_state(void)
{
   return malloc(sizeof(struct rendering_state));
}
