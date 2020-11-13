#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/SourceMgr.h>
#include "llvm/Support/raw_ostream.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Support/ManagedStatic.h"

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
};

void *thread_routine(void *args) {
    const Params *params = reinterpret_cast<Params *>(args);
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
    auto module = parseIRFile(BITCODE_FILE, error, context);
    if (module)
    {
        outs() << "Module generated\n";
        //module->print(llvm::errs(), nullptr);
    } else {
        outs() << "ERROR\n";
        error.print("PROGNAMEHERE", outs());
        outs().flush();
        abort();
    }

    outs() << "## Starting compilation ##\n\n";

    /* Timer */
    std::chrono::system_clock::time_point start_time_point;
    start_time_point = std::chrono::system_clock::now();

    EngineBuilder engineBuilder(std::move(module));
    engineBuilder.setEngineKind(EngineKind::JIT);
    ExecutionEngine* ee = engineBuilder.create();

    pthread_t child_thread;
    Params *thread_params = new Params { ee };
    if (pthread_create(&child_thread, nullptr, thread_routine, reinterpret_cast<void *>(thread_params)) != 0) {
        outs() << "Error while creating a thread\n";
        abort();
    }

    // Loop while the thread has not terminated
    uint64_t i = 0;
    int result;
    while ((result = pthread_tryjoin_np(child_thread, nullptr)) == EBUSY) {
        ++i;
    }
    if (result != 0) {
        outs() << "Error while joining with a thread\n";
    }

    /*
    if (pthread_join(child_thread, nullptr) != 0) {
        outs() << "Error while joining a thread\n";
        abort();
    }
    */

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
