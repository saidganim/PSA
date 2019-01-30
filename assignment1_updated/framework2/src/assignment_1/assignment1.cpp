/*
 * File: assignment1.cpp
 *
 * Framework to implement Task 1 of the Advances in Computer Architecture lab
 * session. This uses the framework library to interface with tracefiles which
 * will drive the read/write requests
 *
 * Author(s): Michiel W. van Tol, Mike Lankamp, Jony Zhang,
 *            Konstantinos Bousias
 * Copyright (C) 2005-2017 by Computer Systems Architecture group,
 *                            University of Amsterdam
 *
 */

#include <systemc>
#include <iostream>
#include <string.h>
#include "psa.h"
#include <math.h>
#include "bus_slave_if.h"
#include "CPU.h"
#include "Memory.h"
#include "Cache.h"
#include "utils.h"


using namespace std;
using namespace sc_core; // This pollutes namespace, better: only import what you need.

int sc_main(int argc, char* argv[])
{
    try
    {
        // Get the tracefile argument and create Tracefile object
        // This function sets tracefile_ptr and num_cpus
        init_tracefile(&argc, &argv);

        // Initialize statistics counters
        stats_init();

        // Instantiate Modules
        Memory mem("main_memory");
        CPU    cpu("cpu");
        SingleCache cache("cache");

        // Signals CPU_TO_CACHE
        sc_buffer<Memory::Function> sigCacheFunc;
        sc_buffer<Memory::RetCode>  sigCacheDone;
        sc_signal<int>              sigCacheAddr;
        sc_signal_rv<32>            sigCacheData;

        // Signals CACHE_TO_MEM
        sc_buffer<Memory::Function> sigMemFunc;
        sc_buffer<Memory::RetCode>  sigMemDone;
        sc_signal<int>              sigMemAddr;
        sc_signal_rv<32>            sigMemData;

        // The clock that will drive the CPU and Memory
        sc_clock clk;

        // Connecting module ports with signals
        mem.Port_Func(sigMemFunc);
        mem.Port_Addr(sigMemAddr);
        mem.Port_Data(sigMemData);
        mem.Port_Done(sigMemDone);

        cache.Port_Func_MEM(sigMemFunc);
        cache.Port_Addr_MEM(sigMemAddr);
        cache.Port_Data_MEM(sigMemData);
        cache.Port_Done_MEM(sigMemDone);
        
        cache.Port_Func(sigCacheFunc);
        cache.Port_Addr(sigCacheAddr);
        cache.Port_Data(sigCacheData);
        cache.Port_Done(sigCacheDone);

        cpu.Port_MemFunc(sigCacheFunc);
        cpu.Port_MemAddr(sigCacheAddr);
        cpu.Port_MemData(sigCacheData);
        cpu.Port_MemDone(sigCacheDone);

        mem.Port_CLK(clk);
        cpu.Port_CLK(clk);
        cache.Port_CLK(clk);

        cout << "Running (press CTRL+C to interrupt)... " << endl;
        // Start Simulation
        sc_start();
        // Print statistics after simulation finished
        stats_print();
    }

    catch (exception& e){
        cerr << e.what() << endl;
    }
    return 0;
}
