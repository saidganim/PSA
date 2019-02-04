#ifndef CPU_MOD
#define CPU_MOD

#include "utils.h"
#include "Memory.h"

SC_MODULE(CPU)
{

public:
    sc_in<bool>                Port_CLK;
    sc_in<Memory::RetCode>   Port_MemDone;
    sc_out<Memory::Function> Port_MemFunc;
    sc_out<int>                Port_MemAddr;
    // sc_inout_rv<32>            Port_MemData;
    int id;

    CPU(sc_module_name name_, int id_): sc_module(name_), id(id_){
        SC_THREAD(execute);
        sensitive << Port_CLK.pos();
        dont_initialize();
    }
    SC_HAS_PROCESS(CPU);
private:
    void execute()
    {
        TraceFile::Entry    tr_data;
        Memory::Function  f;

        // Loop until end of tracefile
        while(!tracefile_ptr->eof())
        {
            // Get the next action for the processor in the trace
            if(!tracefile_ptr->next(id, tr_data))
            {
                cerr << "Error reading trace for CPU" << endl;
                break;
            }

            switch(tr_data.type)
            {
                case TraceFile::ENTRY_TYPE_READ:
                    f = Memory::FUNC_READ;
                    break;

                case TraceFile::ENTRY_TYPE_WRITE:
                    f = Memory::FUNC_WRITE;
                    break;

                case TraceFile::ENTRY_TYPE_NOP:
                    break;

                default:
                    cerr << "Error, got invalid data from Trace" << endl;
                    exit(0);
            }

            if(tr_data.type != TraceFile::ENTRY_TYPE_NOP)
            {
                Port_MemAddr.write(tr_data.addr);
                Port_MemFunc.write(f);

                if (f == Memory::FUNC_WRITE)
                {
                    cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CPU sends write" << endl;

                    // Port_MemData.write(data);
                    wait(Port_CLK.default_event());
                    // Port_MemData.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
                }
                else
                {
                    cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CPU sends read [" << tr_data.addr << "]" << endl;
                }

                wait(Port_MemDone.value_changed_event());

                if (f == Memory::FUNC_READ)
                {
                    cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CPU reads: " << endl;
                }
            }
            else
            {
                cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CPU executes NOP" << endl;
            }
            // Advance one cycle in simulated time
            wait(Port_CLK.default_event());
        }

        // Finished the Tracefile, now stop the simulation
        sc_stop();
    }
};


#endif