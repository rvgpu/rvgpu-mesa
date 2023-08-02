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

#include <iostream>
#include <iomanip>

#include <llvm/Target/TargetMachine.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Disassembler.h>

#include "rc_llvm_build.h"
#include "rc_llvm_util.h"
#include "rc_build_type.h"

void rc_disassemble(char *bytes, uint32_t bufsize) {
    printf("RISC-V Binary:\n");
    const char *triple = "riscv64-unknown-linux-gnu";
    const char *mcpu = "rvgpu";
    LLVMDisasmContextRef D = LLVMCreateDisasmCPU(triple, mcpu, NULL, 0, NULL, NULL);
    char outline[1024];

    bytes = bytes + 0x40;
    uint32_t pc = 0;
    while (pc < bufsize) {
        size_t Size;
        Size = LLVMDisasmInstruction(D, (uint8_t *)bytes + pc, bufsize - pc, 0, outline, sizeof outline);
        if (!Size) {
            printf("invalid\n");
            pc += 1;
            break;
        }

        printf("%06d: %08x\t%s\n", pc, *(uint32_t *)((uint8_t *)bytes + pc), outline);

        pc += Size;
    }

    LLVMDisasmDispose(D);
}

struct rc_llvm_pointer rc_build_main(struct rc_llvm_context *ctx)
{
   // uint64_t main(uint64_t desc, uint32_t vid)
   LLVMTypeRef arg_types[2];
   arg_types[0] = LLVMInt64TypeInContext(ctx->context);
   arg_types[1] = LLVMInt32TypeInContext(ctx->context);
   LLVMTypeRef ret_type;
   ret_type = LLVMInt32TypeInContext(ctx->context);

   LLVMTypeRef main_function_type = LLVMFunctionType(ret_type, arg_types, 2, 0);
   LLVMValueRef main_function = LLVMAddFunction(ctx->module, "main", main_function_type);

   LLVMBasicBlockRef main_function_body = LLVMAppendBasicBlockInContext(ctx->context, main_function, "main_body");
   LLVMPositionBuilderAtEnd(ctx->builder, main_function_body);
   LLVMSetFunctionCallConv(main_function, 0);

   // Test Zac
#if 0
   LLVMValueRef a = LLVMGetParam(main_function, 0);
   LLVMValueRef b = LLVMGetParam(main_function, 1);
   LLVMValueRef c = LLVMBuildIntCast2(ctx->builder, b, LLVMTypeOf(a), 0, "c");
   LLVMValueRef ret = LLVMBuildAdd(ctx->builder, a, c, "sum");

   LLVMBuildRet(ctx->builder, ret);
#endif
   ctx->main_function.value = main_function;
   ctx->main_function.pointee_type = main_function_type;
   return ctx->main_function;
}

LLVMModuleRef rc_create_module(LLVMTargetMachineRef tm, LLVMContextRef ctx)
{   
   llvm::TargetMachine *TM = reinterpret_cast<llvm::TargetMachine *>(tm);
   LLVMModuleRef module = LLVMModuleCreateWithNameInContext("mesa-shader", ctx);

   llvm::unwrap(module)->setTargetTriple(TM->getTargetTriple().getTriple());
   llvm::unwrap(module)->setDataLayout(TM->createDataLayout());
   return module; 
}

/* Initialize module-independent parts of the context.
 *
 * The caller is responsible for initializing ctx::module and ctx::builder.
 */
void rc_llvm_context_init(struct rc_llvm_context *ctx, struct rc_llvm_compiler *compiler)
{
   ctx->context = LLVMContextCreate();
   ctx->module = rc_create_module(compiler->tm, ctx->context);
   ctx->builder = LLVMCreateBuilderInContext(ctx->context);

   ctx->voidt = LLVMVoidTypeInContext(ctx->context);
}

static LLVMTypeRef
build_vec_type(struct rc_llvm_context *ctx, struct rc_type type) {
    LLVMTypeRef elem_type = rc_build_elem_type(ctx, type);
    if (type.length == 1)
        return elem_type;
    else
        return LLVMVectorType(elem_type, type.length);
}

static LLVMValueRef
build_one(struct rc_llvm_context *ctx, struct rc_type type) {
    LLVMTypeRef elem_type;
    LLVMValueRef elems[MAX_VECTOR_LENGTH];

    elem_type = rc_build_elem_type(ctx, type);

    if (type.floating) {
        elems[0] = LLVMConstReal(elem_type, 1.0);
    } else if (type.fixed) {
        elems[0] = LLVMConstInt(elem_type, 1LL << (type.width/2), 0);
    } else if (!type.norm) {
        elems[0] = LLVMConstInt(elem_type, 1, 0);
    } else if (type.sign) {
        elems[0] = LLVMConstInt(elem_type, (1LL << (type.width - 1)) - 1, 0);
    } else {
        LLVMTypeRef vec_type = build_vec_type(ctx, type);
        LLVMValueRef vec = LLVMConstAllOnes(vec_type);
        return vec;
    }
    for (unsigned i = 1; i < type.length; ++i)
        elems[i] = elems[0];

    if (type.length == 1)
        return elems[0];
    else
        return LLVMConstVector(elems, type.length);
}

void
rc_build_context_init(struct rc_build_context *bld, struct rc_llvm_context *ctx, struct rc_type type) {
    bld->type = type;
    bld->int_elem_type = LLVMIntTypeInContext(ctx->context, type.width);
    bld->rc = ctx;

    if (type.floating)
        bld->elem_type = rc_build_elem_type(ctx, type);
    else
        bld->elem_type = bld->int_elem_type;

    if (type.length == 1) {
        bld->int_vec_type = bld->int_elem_type;
        bld->vec_type = bld->elem_type;
    } else {
        bld->int_vec_type = LLVMVectorType(bld->int_elem_type, type.length);
        bld->vec_type = LLVMVectorType(bld->elem_type, type.length);
    }
    bld->undef = LLVMGetUndef(bld->vec_type);
    bld->zero = LLVMConstNull(bld->vec_type);
    bld->one = build_one(ctx, type);
}