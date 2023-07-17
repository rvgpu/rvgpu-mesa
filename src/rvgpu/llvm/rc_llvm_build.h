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

#ifndef RC_LLVM_BUILD_H__
#define RC_LLVM_BUILD_H__

#ifdef __cplusplus
extern "C" {
#endif

struct rc_llvm_pointer {
   union {
      LLVMValueRef value;
      LLVMValueRef v;
   };
   /* Doesn't support complex types (pointer to pointer to etc...),
    * but this isn't a problem since there's no place where this
    * would be required.
    */
   union {
      LLVMTypeRef pointee_type;
      LLVMTypeRef t;
   };
}; 

struct rc_llvm_context {
   LLVMContextRef context;
   LLVMModuleRef module;
   LLVMBuilderRef builder;

   LLVMTypeRef voidt;

   struct rc_llvm_pointer main_function;
};

void rc_llvm_context_init(struct rc_llvm_context *ctx, struct rc_llvm_compiler *compiler);

struct rc_llvm_pointer rc_build_main(struct rc_llvm_context *ctx);

#ifdef __cplusplus
}
#endif

#endif
