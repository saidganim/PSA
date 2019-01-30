#ifndef MEMORY_MOD
#define MEMORY_MOD

#include "utils.h"

SC_MODULE(Memory)
{

public:
    enum Function
    {
        FUNC_READ,
        FUNC_WRITE,
    };

    enum RetCode
    {
        RET_READ_DONE,
        RET_WRITE_DONE,
    };

    sc_in<bool>     Port_CLK;
    sc_in<Function> Port_Func;
    sc_in<int>      Port_Addr;
    sc_out<RetCode> Port_Done;
    sc_inout_rv<32> Port_Data;

    SC_CTOR(Memory)
    {
        SC_THREAD(execute);
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

    void execute(){
        while (true)
        {
            wait(Port_Func.value_changed_event());

            Function f = Port_Func.read();
            int addr   = Port_Addr.read();
            if (f == FUNC_WRITE)
            {
                cout << sc_time_stamp() << ": MEM received write" << endl;
            }
            else
            {
                cout << sc_time_stamp() << ": MEM received read" << endl;
            }

            if (f == FUNC_READ){
                wait(89); // This simulates memory read/write delay
                // we trust to the cache module, so we always get aligned address
                Port_Data.write( (addr < MEM_SIZE) ? m_data[addr] : 0 );
                Port_Done.write( RET_READ_DONE );
                wait();
                // 8 cycles of actual reading the cacheline
                // we don't use wider wire here, since it's more interesting :) 
                cout << sc_time_stamp() << ": MEM sends read result" << endl;
                for(int i = 1; i < 8; ++i){
                    Port_Data.write( (addr + i < MEM_SIZE) ? m_data[addr + i] : 0 );
                    wait();
                }
                Port_Data.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
            } else {
                wait(); // one extra cycle to synchronize with CACHE output
                for(int i = 0; i < 8; ++i){
                    if ((addr + i) < MEM_SIZE)
                    {
                        m_data[addr + i] = Port_Data.read().to_int();
                    }
                    wait();
                }
                wait(89); // This simulates memory read/write delay
                cout << sc_time_stamp() << ": MEM has finished writing" << endl;                
                Port_Done.write( RET_WRITE_DONE );
            }
        }
    }
};


#endif