#include <dlfcn.h>
#include <iostream>
#include <string.h>

// how to use
// compile lib: using comand as follow
//   g++ -fPIC -shared simpletrace.cpp -o libsimpletrace.so -rdynamic -ldl
// compile src: using command as follow
//   CXX=g++ -O2 -std=c++11 -fPIC -finstrument-functions -export-dynamic
// run with trace:
//   LD_PRELOAD=./libsimpletrace.so ./test_random [parameter] | c++filt

extern bool trace_enable;
unsigned int level = 0;
unsigned int count = 0;
#define MAX_TRACE_COUNT 10000

extern "C" {
    void __cyg_profile_func_enter(void* func_address, void* call_site);
    void __cyg_profile_func_exit(void* func_address, void* call_site);
}

const char* addr2name(void* address) {
    Dl_info dli;
    if (0 != dladdr(address, &dli)) {
        return dli.dli_sname;
    }
    return 0;
}

void __cyg_profile_func_enter(void* func_address, void* call_site) {
    if(!trace_enable || count > MAX_TRACE_COUNT)
        return;

    const char* func_name = addr2name(func_address);
    if (func_name) {
        if ( strstr(func_name, "list") || strstr(func_name, "iterator"))
            return;
        std::cout << "enter: " << level << "," << func_name << std::endl;
        level++;
        count++;
    }
}

void __cyg_profile_func_exit(void* func_address, void* call_site) {
    if(!trace_enable || count > MAX_TRACE_COUNT)
        return;

    const char* func_name = addr2name(func_address);
    if (func_name) {
        if ( strstr(func_name, "list") || strstr(func_name, "iterator"))
            return;
        level--;
        count++;
        std::cout << "exit: " << level << "," << func_name << std::endl;
    }
}
