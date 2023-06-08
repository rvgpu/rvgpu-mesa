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
         // // handle_pipeline(cmd, state);
         break;
      case VK_CMD_SET_VIEWPORT:
         // // handle_set_viewport(cmd, state);
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
         // handle_pipeline_barrier(cmd, state);
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
