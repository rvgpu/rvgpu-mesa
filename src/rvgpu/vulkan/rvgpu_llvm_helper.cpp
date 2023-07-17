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

#include "rvgpu_llvm_helper.h"
#include "rc_llvm_util.h"

#include <list>
class rvgpu_llvm_per_thread_info {
 public:
   rvgpu_llvm_per_thread_info()
       : passes(NULL)
   {
   }

   ~rvgpu_llvm_per_thread_info()
   {
      rc_destroy_llvm_compiler(&llvm_info);
   }

   bool init(void)
   {
      if (!rc_init_llvm_compiler(&llvm_info))
         return false;

      passes = rc_create_llvm_passes(llvm_info.tm);
      if (!passes)
         return false;

      return true;
   }

   bool compile_to_memory_buffer(LLVMModuleRef module, char **pelf_buffer, size_t *pelf_size)
   {
      return rc_compile_module_to_elf(passes, module, pelf_buffer, pelf_size);
   }

   struct rc_llvm_compiler llvm_info;

 private:
   struct rc_compiler_passes *passes;
};

/* we have to store a linked list per thread due to the possiblity of multiple gpus being required */
static thread_local std::list<rvgpu_llvm_per_thread_info> rvgpu_llvm_per_thread_list;

bool 
rvgpu_init_llvm_compiler(struct rc_llvm_compiler *info) {
   for (auto &I : rvgpu_llvm_per_thread_list) {
      *info = I.llvm_info;
      return true;
   }

   rvgpu_llvm_per_thread_list.emplace_back();
   rvgpu_llvm_per_thread_info &tinfo = rvgpu_llvm_per_thread_list.back();

   if (!tinfo.init()) {
      rvgpu_llvm_per_thread_list.pop_back();
      return false;
   }

   *info = tinfo.llvm_info;
   return true;
}

