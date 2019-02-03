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
#define SC_INCLUDE_DYNAMIC_PROCESSES
#include <systemc>
#include <iostream>
#include <string.h>
#include "psa.h"
#include <math.h>
#include "CPU.h"
#include "Memory.h"
#include "Cache.h"
#include "Bus.h"
#include "utils.h"


using namespace std;
using namespace sc_core; // This pollutes namespace, better: only import what you need.

std::atomic<unsigned int> _main_memory_access_rate;
std::atomic<unsigned int > _time_for_bus_acquisition;
int sc_main(int argc, char* argv[])
{
    timespec time1, time2;
    try
    {
        // Get the tracefile argument and create Tracefile object
        // This function sets tracefile_ptr and num_cpus
        init_tracefile(&argc, &argv);
        int CPUNUM = atoi(argv[0]);
        _main_memory_access_rate.store(0);
        _time_for_bus_acquisition.store(0);
        // Initialize statistics counters
        stats_init();
        sc_report_handler::set_actions (SC_ID_VECTOR_CONTAINS_LOGIC_VALUE_,
                                SC_DO_NOTHING);
        Bus bus("bus");
        sc_clock clk;
        Memory* mem =  new Memory{"main_memory"};
        mem->bus(bus);
        for(int i = 0; i < CPUNUM; ++i){
             // Instantiate Modules
            CPU*    cpu =  new CPU{"cpu", i};
            SingleCache* cache = new SingleCache{"cache"};
            cache->id = i;

            // Signals CPU_TO_CACHE
            sc_buffer<Memory::Function> *sigCacheFunc = new sc_buffer<Memory::Function>;
            sc_buffer<Memory::RetCode>  *sigCacheDone = new sc_buffer<Memory::RetCode>;
            sc_signal<int>              *sigCacheAddr = new sc_signal<int>;
            // sc_signal_rv<32>            sigCacheData;

            // Signals CACHE_TO_MEM
            // sc_buffer<Memory::Function> sigMemFunc;
            // sc_buffer<Memory::RetCode>  sigMemDone;
            // sc_signal<int>              sigMemAddr;
            // sc_signal_rv<32>            sigMemData;
            // The clock that will drive the CPU and Memory

            // Connecting module ports with signals
            // mem.Port_Func(sigMemFunc);
            // mem.Port_Addr(sigMemAddr);
            // // mem.Port_Data(sigMemData);
            // mem.Port_Done(sigMemDone);
            cache->bus(bus);
            // cache.Port_Func_MEM(sigMemFunc);
            // cache.Port_Addr_MEM(sigMemAddr);
            // // cache.Port_Data_MEM(sigMemData);
            // cache.Port_Done_MEM(sigMemDone);
            
            cache->Port_Func(*sigCacheFunc);
            cache->Port_Addr(*sigCacheAddr);
            // cache.Port_Data(sigCacheData);
            cache->Port_Done(*sigCacheDone);

            cpu->Port_MemFunc(*sigCacheFunc);
            cpu->Port_MemAddr(*sigCacheAddr);
            // cpu.Port_MemData(sigCacheData);
            cpu->Port_MemDone(*sigCacheDone);

            cpu->Port_CLK(clk);
            cache->Port_CLK(clk);

        }
            mem->Port_CLK(clk);
            bus.Port_CLK(clk);
       
        cout << "Running (press CTRL+C to interrupt)... " << endl;
        // Start Simulation
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time1);
        sc_start();
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time2);
        unsigned int result = (time2.tv_sec - time1.tv_sec) * 1e6 + (time2.tv_nsec - time1.tv_nsec) / 1e3;

        // Print statistics after simulation finished
        stats_print();
        printf("Main memory access rate = %u\n", _main_memory_access_rate.load());
        printf("Average time for bus acquisition %u ms\n", _time_for_bus_acquisition.load() / _main_memory_access_rate.load());
        printf("Total execution time %u ms\n", result);
    }

    catch (exception& e){
        cerr << e.what() << endl;
    }
    return 0;
}
