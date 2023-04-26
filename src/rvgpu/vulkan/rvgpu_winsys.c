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

#include "rvgpu_winsys.h"

struct rvgpu_winsys *
rvgpu_winsys_create(int fd, uint64_t debug_flags, uint64_t perftest_flags)
{
   uint32_t drm_major, drm_minor, r;
   amdgpu_device_handle dev;
   struct radv_amdgpu_winsys *ws = NULL;

   r = amdgpu_device_initialize(fd, &drm_major, &drm_minor, &dev);
   if (r) {
      fprintf(stderr, "radv/amdgpu: failed to initialize device.\n");
      return NULL;
   }

   /* We have to keep this lock till insertion. */
   simple_mtx_lock(&winsys_creation_mutex);
   if (!winsyses)
      winsyses = _mesa_pointer_hash_table_create(NULL);
   if (!winsyses) {
      fprintf(stderr, "radv/amdgpu: failed to alloc winsys hash table.\n");
      goto fail;
   }

   struct hash_entry *entry = _mesa_hash_table_search(winsyses, dev);
   if (entry) {
      ws = (struct radv_amdgpu_winsys *)entry->data;
      ++ws->refcount;
   }

   if (ws) {
      simple_mtx_unlock(&winsys_creation_mutex);
      amdgpu_device_deinitialize(dev);

      /* Check that options don't differ from the existing winsys. */
      if (((debug_flags & RADV_DEBUG_ALL_BOS) && !ws->debug_all_bos) ||
          ((debug_flags & RADV_DEBUG_HANG) && !ws->debug_log_bos) ||
          ((debug_flags & RADV_DEBUG_NO_IBS) && ws->use_ib_bos) ||
          (perftest_flags != ws->perftest)) {
         fprintf(stderr, "radv/amdgpu: Found options that differ from the existing winsys.\n");
         return NULL;
      }

      /* RADV_DEBUG_ZERO_VRAM is the only option that is allowed to be set again. */
      if (debug_flags & RADV_DEBUG_ZERO_VRAM)
         ws->zero_all_vram_allocs = true;

      return &ws->base;
   }

   ws = calloc(1, sizeof(struct radv_amdgpu_winsys));
   if (!ws)
      goto fail;

   ws->refcount = 1;
   ws->dev = dev;
   ws->info.drm_major = drm_major;
   ws->info.drm_minor = drm_minor;
   if (!do_winsys_init(ws, fd))
      goto winsys_fail;

   ws->debug_all_bos = !!(debug_flags & RADV_DEBUG_ALL_BOS);
   ws->debug_log_bos = debug_flags & RADV_DEBUG_HANG;
   if (debug_flags & RADV_DEBUG_NO_IBS)
      ws->use_ib_bos = false;

   ws->reserve_vmid = reserve_vmid;
   if (ws->reserve_vmid) {
      r = amdgpu_vm_reserve_vmid(dev, 0);
      if (r) {
         fprintf(stderr, "radv/amdgpu: failed to reserve vmid.\n");
         goto vmid_fail;
      }
   }
   int num_sync_types = 0;

   ws->syncobj_sync_type = vk_drm_syncobj_get_type(amdgpu_device_get_fd(ws->dev));
   if (ws->syncobj_sync_type.features) {
      ws->sync_types[num_sync_types++] = &ws->syncobj_sync_type;
      if (!(ws->syncobj_sync_type.features & VK_SYNC_FEATURE_TIMELINE)) {
         ws->emulated_timeline_sync_type = vk_sync_timeline_get_type(&ws->syncobj_sync_type);
         ws->sync_types[num_sync_types++] = &ws->emulated_timeline_sync_type.sync;
      }
   }

   ws->sync_types[num_sync_types++] = NULL;
   assert(num_sync_types <= ARRAY_SIZE(ws->sync_types));

   ws->perftest = perftest_flags;
   ws->zero_all_vram_allocs = debug_flags & RADV_DEBUG_ZERO_VRAM;
   u_rwlock_init(&ws->global_bo_list.lock);
   list_inithead(&ws->log_bo_list);
   u_rwlock_init(&ws->log_bo_list_lock);
   ws->base.query_info = radv_amdgpu_winsys_query_info;
   ws->base.query_value = radv_amdgpu_winsys_query_value;
   ws->base.read_registers = radv_amdgpu_winsys_read_registers;
   ws->base.get_chip_name = radv_amdgpu_winsys_get_chip_name;
   ws->base.destroy = radv_amdgpu_winsys_destroy;
   ws->base.get_fd = radv_amdgpu_winsys_get_fd;
   ws->base.get_sync_types = radv_amdgpu_winsys_get_sync_types;
   radv_amdgpu_bo_init_functions(ws);
   radv_amdgpu_cs_init_functions(ws);
   radv_amdgpu_surface_init_functions(ws);

   _mesa_hash_table_insert(winsyses, dev, ws);
   simple_mtx_unlock(&winsys_creation_mutex);

   return &ws->base;

vmid_fail:
   ac_addrlib_destroy(ws->addrlib);
winsys_fail:
   free(ws);
fail:
   if (winsyses && _mesa_hash_table_num_entries(winsyses) == 0) {
      _mesa_hash_table_destroy(winsyses, NULL);
      winsyses = NULL;
   }
   simple_mtx_unlock(&winsys_creation_mutex);
   amdgpu_device_deinitialize(dev);
   return NULL;
}
