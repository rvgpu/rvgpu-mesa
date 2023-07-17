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
#include <cstring>

#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Transforms/IPO/AlwaysInliner.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Core.h>
#include <c11/threads.h>

#include "rc_llvm_util.h"

bool rc_is_llvm_processor_supported(LLVMTargetMachineRef tm, const char *processor)
{     
   llvm::TargetMachine *TM = reinterpret_cast<llvm::TargetMachine *>(tm);
   return TM->getMCSubtargetInfo()->isCPUStringValid(processor);
}

LLVMTargetLibraryInfoRef rc_create_target_library_info(const char *triple)
{
   return reinterpret_cast<LLVMTargetLibraryInfoRef>(
      new llvm::TargetLibraryInfoImpl(llvm::Triple(triple)));
}

/* Implementation of raw_pwrite_stream that works on malloc()ed memory for
 * better compatibility with C code. */
struct raw_memory_ostream : public llvm::raw_pwrite_stream {
   char *buffer;
   size_t written;
   size_t bufsize;

   raw_memory_ostream()
   {
      buffer = NULL;
      written = 0;
      bufsize = 0;
      SetUnbuffered();
   }

   ~raw_memory_ostream()
   {
      free(buffer);
   }

   void clear()
   {
      written = 0;
   }

   void take(char *&out_buffer, size_t &out_size)
   {
      out_buffer = buffer;
      out_size = written;
      buffer = NULL;
      written = 0;
      bufsize = 0;
   }

   void flush() = delete;
   void write_impl(const char *ptr, size_t size) override
   {
      if (unlikely(written + size < written))
         abort();
      if (written + size > bufsize) {
         bufsize = MAX3(1024, written + size, bufsize / 3 * 4);
         buffer = (char *)realloc(buffer, bufsize);
         if (!buffer) {
            fprintf(stderr, "amd: out of memory allocating ELF buffer\n");
            abort();
         }
      }
      memcpy(buffer + written, ptr, size);
      written += size;
   }

   void pwrite_impl(const char *ptr, size_t size, uint64_t offset) override
   {
      assert(offset == (size_t)offset && offset + size >= offset && offset + size <= written);
      memcpy(buffer + offset, ptr, size);
   }

   uint64_t current_pos() const override
   {
      return written;
   }
};

/* The LLVM compiler is represented as a pass manager containing passes for
 * optimizations, instruction selection, and code generation.
 */
struct rc_compiler_passes {
   raw_memory_ostream ostream;        /* ELF shader binary stream */
   llvm::legacy::PassManager passmgr; /* list of passes */
};

struct rc_compiler_passes *rc_create_llvm_passes(LLVMTargetMachineRef tm)
{
   struct rc_compiler_passes *p = new rc_compiler_passes();
   if (!p)
      return NULL;

   llvm::TargetMachine *TM = reinterpret_cast<llvm::TargetMachine *>(tm);

   if (TM->addPassesToEmitFile(p->passmgr, p->ostream, nullptr, llvm::CGFT_ObjectFile)) {
      fprintf(stderr, "amd: TargetMachine can't emit a file of this type!\n");
      delete p;
      return NULL;
   }
   return p;
}

/* This returns false on failure. */
bool rc_compile_module_to_elf(struct rc_compiler_passes *p, LLVMModuleRef module, char **pelf_buffer, size_t *pelf_size)
{
   p->passmgr.run(*llvm::unwrap(module));
   p->ostream.take(*pelf_buffer, *pelf_size);
   return true;
}

LLVMTargetRef rc_get_llvm_target(const char *triple)
{
   LLVMTargetRef target = NULL;
   char *err_message = NULL;

   if (LLVMGetTargetFromTriple(triple, &target, &err_message)) {
      fprintf(stderr, "Cannot find target for triple %s ", triple);
      if (err_message) {
         fprintf(stderr, "%s\n", err_message);
      }
      LLVMDisposeMessage(err_message);
      return NULL;
   }
   return target;
}

static LLVMTargetMachineRef rc_create_target_machine(LLVMCodeGenOptLevel level, const char **out_triple)
{
   const char *triple = "riscv64-unknown-linux-gnu";
   LLVMTargetRef target = rc_get_llvm_target(triple);

   const char *name = "generic-rv64";

   LLVMTargetMachineRef tm =
      LLVMCreateTargetMachine(target, triple, name, "", level,
                              LLVMRelocDefault, LLVMCodeModelDefault);

   if (!rc_is_llvm_processor_supported(tm, name)) {
      LLVMDisposeTargetMachine(tm);
      fprintf(stderr, "amd: LLVM doesn't support %s, bailing out...\n", name);
      return NULL;
   }

   if (out_triple)
      *out_triple = triple;

   return tm;
}

LLVMPassManagerRef rc_create_passmgr(LLVMTargetLibraryInfoRef target_library_info)
{  
   LLVMPassManagerRef passmgr = LLVMCreatePassManager();
   if (!passmgr)
      return NULL;
   
   if (target_library_info)
      LLVMAddTargetLibraryInfo(target_library_info, passmgr);
 
#if 0  // TODO: zac
   if (check_ir)
      unwrap(passmgr)->add(createMachineVerifierPass("mesa ir"));
#endif
   
   llvm::unwrap(passmgr)->add(llvm::createAlwaysInlinerLegacyPass());
   
   /* Normally, the pass manager runs all passes on one function before
    * moving onto another. Adding a barrier no-op pass forces the pass
    * manager to run the inliner on all functions first, which makes sure
    * that the following passes are only run on the remaining non-inline
    * function, so it removes useless work done on dead inline functions.
    */
  llvm::unwrap(passmgr)->add(llvm::createBarrierNoopPass());
   
   /* This pass eliminates all loads and stores on alloca'd pointers. */
   llvm::unwrap(passmgr)->add(llvm::createPromoteMemoryToRegisterPass());
   llvm::unwrap(passmgr)->add(llvm::createSROAPass(true));
   /* TODO: restore IPSCCP */
   llvm::unwrap(passmgr)->add(llvm::createLoopSinkPass());
   /* TODO: restore IPSCCP */
   llvm::unwrap(passmgr)->add(llvm::createLICMPass());
   llvm::unwrap(passmgr)->add(llvm::createCFGSimplificationPass());
   /* This is recommended by the instruction combining pass. */
   llvm::unwrap(passmgr)->add(llvm::createEarlyCSEPass(true));
   llvm::unwrap(passmgr)->add(llvm::createInstructionCombiningPass());
   return passmgr;
}

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

bool rc_init_llvm_compiler(struct rc_llvm_compiler *compiler)
{
   const char *triple;
   memset(compiler, 0, sizeof(*compiler));

   compiler->tm = rc_create_target_machine(LLVMCodeGenLevelDefault, &triple);
   if (!compiler->tm)
      return false;

   compiler->target_library_info = rc_create_target_library_info(triple);
   if (!compiler->target_library_info)
      goto fail;

   compiler->passmgr = rc_create_passmgr(compiler->target_library_info);
   if (!compiler->passmgr)
      goto fail;

   return true;
fail:
   rc_destroy_llvm_compiler(compiler);
   return false;
}

void rc_destroy_llvm_compiler(struct rc_llvm_compiler *compiler)
{
   delete compiler->passes;
   delete compiler->low_opt_passes;

   if (compiler->passmgr)
      LLVMDisposePassManager(compiler->passmgr);
   if (compiler->target_library_info) {
      delete reinterpret_cast<llvm::TargetLibraryInfoImpl *>(compiler->target_library_info);
   }
   if (compiler->low_opt_tm)
      LLVMDisposeTargetMachine(compiler->low_opt_tm);
   if (compiler->tm)
      LLVMDisposeTargetMachine(compiler->tm);
}
