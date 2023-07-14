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

// RISC-V Compiler

#include <llvm-c/Target.h>
#include <c11/threads.h>

#include "rc_llvm_util.h"

static void rc_init_llvm_target(void)
{
   LLVMInitializeRISCVTargetInfo();
   LLVMInitializeRISCVTarget();
   LLVMInitializeRISCVTargetMC();
   LLVMInitializeRISCVAsmPrinter();

   /* For inline assembly. */
   LLVMInitializeRISCVAsmParser();

   /* For ACO disassembly. */
   LLVMInitializeRISCVDisassembler();

#if 0
   const char *argv[] = {
      /* error messages prefix */
      "mesa",
      "-amdgpu-atomic-optimizations=true",
   };

   ac_reset_llvm_all_options_occurences();
   LLVMParseCommandLineOptions(ARRAY_SIZE(argv), argv, NULL);

   ac_llvm_run_atexit_for_destructors();
#endif
}

PUBLIC void rc_init_shared_llvm_once(void)
{
   static once_flag rc_init_llvm_target_once_flag = ONCE_FLAG_INIT;
   call_once(&rc_init_llvm_target_once_flag, rc_init_llvm_target);
}

void rc_init_llvm_once(void)
{   
   rc_init_shared_llvm_once();
}   
