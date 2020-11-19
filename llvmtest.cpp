#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include <llvm/IR/Module.h>
#include "llvm/IR/PassManager.h"
#include "llvm/IR/LegacyPassManager.h"
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/SourceMgr.h>
#include "llvm/Support/raw_ostream.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <utility>
#include <cstdlib>
#include <cstdint>
#include <chrono>
#include <pthread.h>

#include <sqlite3.h>


using namespace llvm;

// in Makefile
// defined BITCODE_FILE as "filename.bc"

struct Params {
    ExecutionEngine* ee;
    Module *module;
};

void optimize_module(ExecutionEngine *ee, Module *module) {
    PassManagerBuilder pass_builder;

    /* optimize level : -O2 */
    pass_builder.OptLevel = 2;

    /* Don't optimize for code size : corresponds to -O2/ -O3 */
    pass_builder.SizeLevel = 0;
    pass_builder.Inliner = llvm::createFunctionInliningPass();

    /*
     * Specifying the data layout is necessary for some optimizations
     * e.g. : removing many of the loads/stores produced by structs.
     */
    llvm::TargetIRAnalysis target_analysis = ee->getTargetMachine()->getTargetIRAnalysis();

    /*
     * Before running any other optimization passes, run the internalize pass, giving
     * it the names of all functions registered by addFunctionToJIT(), followed by the
     * global dead code elimination pass. This causes all functions not registered to be
     * JIT'd to be marked as internal, and any internal functions that are not used are
     * deleted by DCE pass. This greatly decreases compile time by removing unused code.
     *
     * REMOVED FOR NOW
    unordered_set<string> exported_fn_names;
    foreach (cell, m_machineCodeJitCompiled) {
        Llvm_Map<llvm::Function*, void**>* tmpMap = (Llvm_Map<llvm::Function*, void**>*)lfirst(cell);
        llvm::Function* func = tmpMap->key;
        exported_fn_names.insert(func->getName().data());
    }
    */

    legacy::PassManager* module_pass_manager(new legacy::PassManager());
    module_pass_manager->add(createTargetTransformInfoWrapperPass(target_analysis));
    /* REMOVED FOR NOW
    module_pass_manager->add(llvm::createInternalizePass([&exported_fn_names](const llvm::GlobalValue& gv) {
        return exported_fn_names.find(gv.getName().str()) != exported_fn_names.end();
    }));
    */

    /* boost:: scoped_ptr<PassManager> module_pass_manager(new PassManager() */
    module_pass_manager->add(createGlobalDCEPass());
    module_pass_manager->run(*module);
    delete module_pass_manager;

    /*
     * Create and run function pass manager:
     * boost::scoped_ptr<FunctionPassManager> fn_pass_manager(new FunctionPassManager(M));
     */
    legacy::FunctionPassManager* fn_pass_manager = new legacy::FunctionPassManager(module);
    fn_pass_manager->add(llvm::createTargetTransformInfoWrapperPass(target_analysis));
    pass_builder.populateFunctionPassManager(*fn_pass_manager);
    fn_pass_manager->doInitialization();
    llvm::Module::iterator it = module->begin();
    llvm::Module::iterator end = module->end();
    while (it != end) {
        if (!it->isDeclaration()) {
            fn_pass_manager->run(*it);
        }
        ++it;
    }
    fn_pass_manager->doFinalization();

    /* Create and run module pass manager */
    module_pass_manager = new legacy::PassManager();
    module_pass_manager->add(llvm::createTargetTransformInfoWrapperPass(target_analysis));
    pass_builder.populateModulePassManager(*module_pass_manager);
    module_pass_manager->run(*module);
    delete module_pass_manager;
    delete fn_pass_manager;
}

void *thread_routine(void *args) {
    const Params *params = reinterpret_cast<Params *>(args);

    /* Optimization */
    optimize_module(params->ee, params->module);

    /* Compilation */
    params->ee->finalizeObject();
    return 0x0;
}

int main()
{
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();

    outs() << "Is multithreaded: " << llvm::llvm_is_multithreaded() << '\n';

    //outs() << "BITCODE_FILE = " #BITCODE_FILE << '\n';
    LLVMContext context;
    SMDiagnostic error;
    std::unique_ptr<Module> uni_module = parseIRFile(BITCODE_FILE, error, context);
    if (uni_module)
    {
        outs() << "Module generated\n";
        //module->print(llvm::errs(), nullptr);
    } else {
        outs() << "ERROR\n";
        error.print("PROGNAMEHERE", outs());
        outs().flush();
        abort();
    }
    Module *module = uni_module.get();

    /* Timer */
    std::chrono::system_clock::time_point start_time_point;
    start_time_point = std::chrono::system_clock::now();

    /* Optimization and compilation in a seperate thread */
    outs() << "## Starting optimization and compilation ##\n\n";

    EngineBuilder engineBuilder(std::move(uni_module));
    engineBuilder.setEngineKind(EngineKind::JIT);
    ExecutionEngine* ee = engineBuilder.create();

    pthread_t child_thread;
    Params *thread_params = new Params { ee, module };
    if (pthread_create(&child_thread, nullptr, thread_routine, reinterpret_cast<void *>(thread_params)) != 0) {
        outs() << "Error while creating a thread\n";
        abort();
    }

    // Loop while the thread has not terminated (for debugging purposes)
    uint64_t i = 0;
    int result;
    while ((result = pthread_tryjoin_np(child_thread, nullptr)) == EBUSY) {
        ++i;
    }
    if (result != 0) {
        outs() << "Error while joining with a thread\n";
    }


    outs() << "## Compilation finished ##\n";
    outs() << "Errors: " << ee->getErrorMessage() << '\n';

    outs() << "Value of i = " << i << '\n';

    uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start_time_point).count();
    outs() << "Time elapsed = " << ms / 1000 << '.' << ms % 1000 << " s\n";

    /* TEST SQLITE */
    
    auto sqlite3_open = reinterpret_cast<int (*)(const char *, sqlite3 **)>(ee->getFunctionAddress("sqlite3_open"));
    auto sqlite3_close = reinterpret_cast<int (*)(sqlite3 *)>(ee->getFunctionAddress("sqlite3_close"));
    sqlite3* DB; 
    int exit = 0; 
    exit = sqlite3_open("example.db", &DB); 
  
    if (exit) { 
        outs() << "Error opening DB";
        abort();
    } 
    else {
        outs() << "Opened Database Successfully!" << '\n'; 
    }
    sqlite3_close(DB); 

    /* test sqlite END */
    /* TEST BCTEST
    // Test by running a function
    auto f = reinterpret_cast<int32_t (*)(int32_t)>(
            ee->getFunctionAddress("foo"));
    outs() << "Running foo(7) = " << f(7) << '\n';
    */

    delete ee;
    llvm_shutdown();
    return 0;
}
