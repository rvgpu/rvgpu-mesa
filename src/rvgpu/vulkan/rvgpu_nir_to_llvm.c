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

#include <llvm-c/Core.h>

#include "nir/nir.h"

#include "rc_llvm_util.h"
#include "rc_llvm_build.h"

#include "rvgpu_private.h"
#include "rvgpu_llvm_helper.h"

static LLVMModuleRef
rc_translate_nir_to_llvm(struct rc_llvm_compiler *rc_llvm, struct nir_shader *nir)
{
   struct rc_llvm_context rc;
   rc_llvm_context_init(&rc, rc_llvm);

   struct rc_llvm_pointer main_function;
   // rc_build_main(rc_context, calling_convention, )
   main_function = rc_build_main(&rc);

   // LLVMRunPassManager(rc_llvm->passmgr, rc.module);
   LLVMDisposeBuilder(rc.builder);

   return rc.module;
}

void rvgpu_llvm_compile_shader(struct nir_shader *shader) {
   struct rc_llvm_compiler rc_llvm;

   rvgpu_init_llvm_compiler(&rc_llvm);

   LLVMModuleRef llvm_module;
   llvm_module = rc_translate_nir_to_llvm(&rc_llvm, shader);

   printf("DUMP LLVMIR\n");
   char *str = LLVMPrintModuleToString(llvm_module);
   printf("%s", str);

   char *elf_buffer = NULL;
   size_t elf_size = 0;
   LLVMContextRef llvm_ctx;

   llvm_ctx = LLVMGetModuleContext(llvm_module);
   rvgpu_compile_to_elf(&rc_llvm, llvm_module, &elf_buffer, &elf_size);

   rc_disassemble(elf_buffer, elf_size);
}
