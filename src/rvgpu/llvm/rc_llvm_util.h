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

#ifndef RC_LLVM_UTILS_H__
#define RC_LLVM_UTILS_H__

#include <stdbool.h>
#include <llvm-c/TargetMachine.h>

#include "util/macros.h"

#ifdef __cplusplus
extern "C" {
#endif

struct rc_compiler_passes;

/* Per-thread persistent LLVM objects. */
struct rc_llvm_compiler {
   LLVMTargetLibraryInfoRef target_library_info;
   LLVMPassManagerRef passmgr;

   /* Default compiler. */
   LLVMTargetMachineRef tm;
   struct rc_compiler_passes *passes;

   /* Optional compiler for faster compilation with fewer optimizations.
    * LLVM modules can be created with "tm" too. There is no difference.
    */
   LLVMTargetMachineRef low_opt_tm; /* uses -O1 instead of -O2 */
   struct rc_compiler_passes *low_opt_passes;
};

PUBLIC void rc_init_shared_llvm_once(void); /* Do not use directly, use rc_init_llvm_once */
void rc_init_llvm_once(void);

/* RC Compiler interface */
bool rc_init_llvm_compiler(struct rc_llvm_compiler *compiler);
void rc_destroy_llvm_compiler(struct rc_llvm_compiler *compiler);

struct rc_compiler_passes *rc_create_llvm_passes(LLVMTargetMachineRef tm);
void rc_destroy_llvm_passes(struct rc_compiler_passes *p);
bool rc_compile_module_to_elf(struct rc_compiler_passes *p, LLVMModuleRef module, char **pelf_buffer, size_t *pelf_size);

void rc_disassemble(char *buffer, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
