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

#include "nir_builder.h"
#include "nir_loop_analyze.h"

#include "rvgpu_private.h"

static bool
is_src_uniform_load(nir_src src)
{
   if (nir_src_bit_size(src) != 32 || nir_src_num_components(src) != 1 || nir_src_is_const(src))
      return false;
   return nir_collect_src_uniforms(&src, 0, NULL, NULL,
                                   PIPE_MAX_CONSTANT_BUFFERS, UINT_MAX);
}

static void
process_node(nir_cf_node *node, nir_loop_info *info,
             uint32_t *uni_offsets, uint8_t *num_offsets,
             struct set *stores)
{
   switch (node->type) {
   case nir_cf_node_if: {
      nir_if *if_node = nir_cf_node_as_if(node);
      const nir_src *cond = &if_node->condition;
      nir_add_inlinable_uniforms(cond, info, uni_offsets, num_offsets,
                                 PIPE_MAX_CONSTANT_BUFFERS, UINT_MAX);

      /* Do not pass loop info down so only alow induction variable
       * in loop terminator "if":
       *
       *     for (i = 0; true; i++)
       *         if (i == count)
       *             if (i == num)
       *                 <no break>
       *             break
       *
       * so "num" won't be inlined due to the "if" is not a
       * terminator.
       */
      info = NULL;

      foreach_list_typed(nir_cf_node, nested_node, node, &if_node->then_list)
         process_node(nested_node, info, uni_offsets, num_offsets, stores);
      foreach_list_typed(nir_cf_node, nested_node, node, &if_node->else_list)
         process_node(nested_node, info, uni_offsets, num_offsets, stores);
      break;
   }

   case nir_cf_node_loop: {
      nir_loop *loop = nir_cf_node_as_loop(node);

      /* Replace loop info, no nested loop info currently:
       *
       *     for (i = 0; i < count0; i++)
       *         for (j = 0; j < count1; j++)
       *             if (i == num)
       *
       * so "num" won't be inlined due to "i" is an induction
       * variable of upper loop.
       */
      info = loop->info;

      foreach_list_typed(nir_cf_node, nested_node, node, &loop->body) {
         bool is_terminator = false;
         list_for_each_entry(nir_loop_terminator, terminator,
                             &info->loop_terminator_list,
                             loop_terminator_link) {
            if (nested_node == &terminator->nif->cf_node) {
               is_terminator = true;
               break;
            }
         }

         /* Allow induction variables for terminator "if" only:
          *
          *     for (i = 0; i < count; i++)
          *         if (i == num)
          *             <no break>
          *
          * so "num" won't be inlined due to the "if" is not a
          * terminator.
          */
         nir_loop_info *use_info = is_terminator ? info : NULL;
         process_node(nested_node, use_info, uni_offsets, num_offsets, stores);
      }
      break;
   }

   case nir_cf_node_block: {
      nir_block *block = nir_cf_node_as_block(node);
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_intrinsic) {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic == nir_intrinsic_store_deref && is_src_uniform_load(intr->src[1]))
               _mesa_set_add(stores, &intr->src[1]);
         }
      }
      break;
   }
   default:
      break;
   }
}

bool
rvgpu_find_inlinable_uniforms(struct rvgpu_shader *shader, nir_shader *nir)
{
   bool ret = false;
   struct set *stores = _mesa_set_create(nir, _mesa_hash_pointer, _mesa_key_pointer_equal);
   nir_foreach_function(function, nir) {
      if (function->impl) {
         nir_metadata_require(function->impl, nir_metadata_loop_analysis, nir_var_all);

         foreach_list_typed(nir_cf_node, node, node, &function->impl->body)
            process_node(node, NULL, (uint32_t*)shader->inlines.uniform_offsets, shader->inlines.count, stores);
      }
   }
   const unsigned threshold = 5;
   set_foreach(stores, entry) {
      const nir_src *src = entry->key;
      unsigned counter = 0;
      list_for_each_entry(nir_src, rsrc, &src->ssa->uses, use_link) {
         counter++;
         if (counter >= threshold)
            break;
      }
      if (counter >= threshold) {
         uint8_t new_num[PIPE_MAX_CONSTANT_BUFFERS];
         memcpy(new_num, shader->inlines.count, sizeof(new_num));

         uint32_t *uni_offsets =
            (uint32_t *) shader->inlines.uniform_offsets;

         if (nir_collect_src_uniforms(src, 0, uni_offsets, new_num,
                                      PIPE_MAX_CONSTANT_BUFFERS, UINT_MAX)) {
            ret = true;
            memcpy(shader->inlines.count, new_num, sizeof(new_num));
         }
      }
   }
   for (unsigned i = 0; i < PIPE_MAX_CONSTANT_BUFFERS; i++) {
      if (shader->inlines.count[i]) {
         shader->inlines.can_inline |= BITFIELD_BIT(i);
         break;
      }
   }
   return ret;
}