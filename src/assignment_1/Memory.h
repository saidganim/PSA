#ifndef MEMORY_MOD
#define MEMORY_MOD

#include "utils.h"
#include "iostream"
#include "Bus.h"

SC_MODULE(Memory)
{

public:
   enum Function
{
    FUNC_NOTHING,
    FUNC_READ,
    FUNC_WRITE,
    FUNC_INVALIDATE,
    FUNC_RESPONSE,
    FUNC_REQUESTED,
};

    enum RetCode
    {
        RET_READ_DONE,
        RET_WRITE_DONE,
    };
    sc_in<bool>     Port_CLK;
    sc_port<Bus_if> bus{"mem_to_bus"};
    // sc_inout_rv<32> Port_Data;

    SC_CTOR(Memory)
    {
        SC_THREAD(execute); //  performing memory accesses and sends responses
        SC_THREAD(snoop);   // performing snooping of the bus to collect requests
        sensitive << Port_CLK.pos();
        dont_initialize();
        m_data = new int[MEM_SIZE];
    }

    ~Memory()
    {
        delete[] m_data;
    }

private:
    int* m_data;
    std::queue<request> requests;

    void snoop(){
        while(true){
            requests.push(bus->get_next_request());
            cout << sc_time_stamp() << ": MEMORY snooping PUT REQ into queue " << requests.size() << endl;

            // wait(Port_CLK.default_event());
        }
    }
    void execute(){
        while (true)
        {
            cout << sc_time_stamp() << ": MEM main thread is waiting for requests" << endl;

            while(requests.empty()){
                cycles_to_wait = 99; // drop to maximum cycles, because of it's fresh memory access
                // cout << sc_time_stamp() << ": MEM main thread is waiting for requests ONE MORE CYCLE" << endl;
                bus->memory_controller_wait();
                // cout << sc_time_stamp() << ": MEM main THINKS that it got request" << endl;

                wait(Port_CLK.default_event());
            }
            cout << sc_time_stamp() << ": MEM main thread got request" << endl;

            struct request req = requests.front();
            requests.pop();


            Function f = (Memory::Function)req.func;
            if (f == FUNC_READ)
            {
                cout << sc_time_stamp() << ": MEM received read" << endl;
            }
            else
            {
                cout << sc_time_stamp() << ": MEM received write" << endl;
            }

            if (f == FUNC_READ){
                for(int i = 0; i < cycles_to_wait; ++i)wait(Port_CLK.default_event()); // This simulates memory read/write delay                
                // we trust to the cache module, so we always get aligned address
                // Port_Data.write( (addr < MEM_SIZE) ? m_data[addr] : 0 );
                // wait(Port_CLK.default_event());
                // 8 cycles of actual reading the cacheline
                // we don't use wider wire here, since it's more interesting :) 
                cout <<sc_time_stamp() << ": MEM sends read result" << endl;
                // for(int i = 1; i < 8; ++i){
                //     Port_Data.write( (addr + i < MEM_SIZE) ? m_data[addr + i] : 0 );
                //     wait(Port_CLK.default_event());
                // }
                // Port_Data.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
                 bus->memory_response(req.id, req.addr);
            } else {
                // wait(Port_CLK.default_event()); // one extra cycle to synchronize with CACHE output
                // for(int i = 0; i < 8; ++i){
                //     if ((addr + i) < MEM_SIZE)
                //     {
                //         m_data[addr + i] = Port_Data.read().to_int();
                //     }
                //     wait(Port_CLK.default_event());
                // }
                for(int i = 0; i < cycles_to_wait; ++i)wait(Port_CLK.default_event()); // This simulates memory read/write delay                
                cout << sc_time_stamp() << ": MEM has finished writing" << endl;                
                // Port_Done.write( RET_WRITE_DONE );
                bus->memory_response(req.id, req.addr);
            }
            cycles_to_wait = 10; // 10 cycles to wait for the next memory load/store if operations are queued
        }
    }
    private:
        int cycles_to_wait = 99; // to simulate pipeline in memory
};


#endif