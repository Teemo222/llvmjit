#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/SourceMgr.h"
#include <llvm/Transforms/Utils/Cloning.h>
#include "llvm/Linker/Linker.h"

#include "jit.h"
#include <easy/jit.h>

#include <iostream>
#include <memory>
#include <string>

using namespace llvm;
using namespace llvm::orc;
using namespace std::placeholders;

struct block {
  int b1;
  int b2;
};

extern "C" {

  int add (int a, int b) {
    int *dynamic_mem = (int *) malloc(sizeof(int));
    *dynamic_mem = 100;

    struct block blk;
    blk.b1 = 2;
    blk.b2 = 3;

    return a + b + *dynamic_mem + blk.b1 + blk.b2;
  }
}

template<class T, class ... Args>
std::unique_ptr<easy::Function> get_function(easy::Context const &C, T &&Fun) {
  auto* FunPtr = easy::meta::get_as_pointer(Fun);
  return easy::Function::Compile(reinterpret_cast<void*>(FunPtr), C);
}

template<class T, class ... Args>
std::unique_ptr<easy::Function> EASY_JIT_COMPILER_INTERFACE _jit(T &&Fun, Args&& ... args) {
  auto C = easy::get_context_for<T, Args...>(std::forward<Args>(args)...);
  return get_function<T, Args...>(C, std::forward<T>(Fun));
}

void WriteOptimizedToFile(llvm::Module const &M) {

  std::error_code Error;
  llvm::raw_fd_ostream Out("bitcode", Error, llvm::sys::fs::F_None);

  Out << M;
}

std::unique_ptr<Module> buildprog(LLVMContext& ctx)
{
    std::unique_ptr<Module> llmod = std::make_unique<Module>("test", ctx);

    Module* m = llmod.get();

    Function* add1_fn = Function::Create(
        FunctionType::get(Type::getInt32Ty(ctx), { Type::getInt32Ty(ctx) }, false),
        Function::ExternalLinkage, "add1", m);

    BasicBlock* bb = BasicBlock::Create(ctx, "EntryBlock", add1_fn);

    IRBuilder<> builder(bb);

    Value* one = builder.getInt32(1);

    Argument* argx = add1_fn->getArg(0);
    argx->setName("x");

    Value* add = builder.CreateAdd(one, argx);
    
    builder.CreateRet(add);

    return llmod;
}

std::unique_ptr<Module> buildsrc(LLVMContext& Context)
{
    const StringRef add1_src =
        R"(
        define i32 @add1(i32 %x) {
        entry:
            %r = add nsw i32 %x, 1
            ret i32 %r
        }
    )";

    SMDiagnostic err;
    auto llmod = parseIR(MemoryBufferRef(add1_src, "test"), err, Context);
    if (!llmod) {
        std::cout << err.getMessage().str() << std::endl;
        return nullptr;
    }
    else {
        return llmod;
    }
}

int main()
{
    std::unique_ptr<easy::Function> CompiledFunction = _jit(add, _1, 1);
    llvm::Module const & M = CompiledFunction->getLLVMModule();
    std::unique_ptr<llvm::Module> Embed = llvm::CloneModule(M);
    WriteOptimizedToFile(*Embed);

    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();

    auto jit = cantFail(TilJIT::Create());
    auto& ctx = jit->getContext();

    auto llmod = buildprog(ctx);
    //auto llmod = std::move(buildsrc(ctx));
    if (!llmod) return -1;

    // Link the main module with the easy::jit extracted module
    llvm::Linker::linkModules(*llmod, std::move(Embed));

    llvm::Function * add_func = llmod->getFunction("add");
    assert(add_func);

    llvm::Function * add1_func = llmod->getFunction("add1");


    // Call add in add1
    {
      BasicBlock * entry_block = &(add1_func->getEntryBlock());
      Instruction * ret =	entry_block->getTerminator();
      IRBuilder<> builder(ret);

      std::vector<llvm::Value *> llvm_parameters;
      llvm_parameters.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 100, true));

      llvm::Value * result = builder.CreateCall(add_func, llvm_parameters);
      builder.CreateRet(result);

      ret->eraseFromParent();
    }
   
    cantFail(jit->addModule(std::move(llmod)));

    JITEvaluatedSymbol sym = cantFail(jit->lookup("add1"));

    auto* add1 = (int (*)(int))(intptr_t)sym.getAddress();
    std::cout << "Result: " << add1(10) << std::endl;

    return 0;
}
