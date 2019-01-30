#ifndef BUS_MOD
#define BUS_MOD

#include <mutex>
#include <atomic>
#include <queue>
#include <systemc.h>

struct request {
    int id;
    int addr;
    int func;
};

enum Function
{
    FUNC_NOTHING,
    FUNC_READ,
    FUNC_WRITE,
    FUNC_RESPONSE,
};

class Bus_if : public virtual sc_interface
{
  public:
    // These methods needed for CPUs to send requests
    virtual bool read(int proc_id, int addr) = 0;
    virtual bool write(int proc_id, int addr) = 0;
    virtual void wait_for_response(int proc_id) = 0;
    virtual struct request wait_for_any(int proc_id) = 0;

    // These methods needed for memory module to put high priority responses on the bus and get requests
    virtual void memory_response(int proc_id, int addr) = 0;
    virtual struct request get_next_request() = 0;
    virtual void memory_controller_wait() = 0;
};
class Bus : public Bus_if, public sc_module
{
  public:
    sc_in<bool> Port_CLK;
    sc_signal_rv<32> Port_BusAddr;
    sc_signal_rv<32> Port_BusFunc;
    sc_signal_rv<32> Port_ProcID;

    
    // sc_inout_rv<32> Port_BusAddr;
    // sc_inout_rv<4> Port_BusFunc;
    // sc_inout_rv<8> Port_ProcID;

  public:
    SC_CTOR(Bus){
        intended.store(false);
        sensitive << Port_CLK.pos();
        // Port_ProcID(ProcID);
        // Port_BusFunc(BusFunc);
        // Port_BusAddr(BusAddr);
        dont_initialize();
    }
    virtual bool read(int proc_id, int addr){
        if (intended.load() || bus_mutex.try_lock() == false)
            return false;   // Cache module has to wait one cycle and try again.
                            // This mutexes may be not fair, but we use them in our module.
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": BUS received read" << endl;

        Port_BusAddr.write(addr);
        Port_BusFunc.write(FUNC_READ);
        Port_ProcID.write(proc_id);
        wait(Port_CLK.default_event());
        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
        Port_ProcID.write("ZZZZZZZZ");
        Port_BusFunc.write("ZZZZ");
        bus_mutex.unlock();
        return true;
    };
    virtual bool write(int proc_id, int addr){
        
         if (intended.load() || bus_mutex.try_lock() == false)
            return false;
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": BUS received write" << endl;
        Port_BusAddr.write(addr);
        Port_BusFunc.write(FUNC_WRITE);
        Port_ProcID.write(proc_id);
        wait(Port_CLK.default_event());
        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
        Port_ProcID.write("ZZZZZZZZ");
        Port_BusFunc.write("ZZZZ");
        bus_mutex.unlock();
        return true;
    }

    virtual void wait_for_response(int proc_id){
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": Cache of CPU <" << proc_id << "> waits for response on bus" << endl;
        wait(Port_BusFunc.value_changed_event());
        while(Port_ProcID.read().to_int() != proc_id || Port_BusFunc.read().to_int() != FUNC_RESPONSE){
            // cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": Cache of CPU <" << proc_id << "> got response but not own <" << Port_ProcID.read().to_int() << ">" <<endl;

            wait(Port_BusFunc.value_changed_event());
        }
        // cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": Cache of CPU <" << proc_id << "> got response for its own <" << Port_ProcID.read().to_int() << "> at address " << Port_BusAddr.read().to_int() <<endl;

    }

    virtual struct request wait_for_any(int proc_id){
        struct request res;
      label1:
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": Snooping Cache of CPU <" << proc_id << "> waits for requests on bus" << endl;
        wait(Port_BusFunc.value_changed_event());
        // if it's own request or it's not writing request - then skip it
        if(Port_BusFunc.read().to_int() != FUNC_WRITE || Port_ProcID.read().to_int() == proc_id) goto label1;
        // else handle request correctly
        // cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": Snooping Cache of CPU <" << proc_id << "> got request <" << Port_ProcID.read().to_int() << ">" <<endl;
        res.addr = Port_BusAddr.read().to_int();
        res.func = Port_BusFunc.read().to_int();
        res.id = Port_ProcID.read().to_int();
        return res;
    }
    
    virtual struct request get_next_request(){
    label1:
        cout << sc_time_stamp() << ": MEMORY snooping is waiting for the next request" << endl;
        wait(Port_BusFunc.default_event());
        // cout << sc_time_stamp() << ": MEMORY snooping thinks it got request" << endl;
        if(Port_BusFunc.read().to_int() == FUNC_RESPONSE || Port_BusFunc.read().to_int() == FUNC_NOTHING)goto label1;
        struct request res;
        res.id = Port_ProcID.read().to_int();
        res.addr = Port_BusAddr.read().to_int();
        res.func = Port_BusFunc.read().to_int();
        cout << sc_time_stamp() << ": MEMORY snooping got request from <"<<res.id<<"> with func " << (res.func == 2?"WRITE" : "READ") << " at address " << res.addr << endl;
        return res;
    }

    virtual void memory_response(int proc_id, int addr){
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": MEMORY MAIN SENDS result to the bus" << endl;
        intended = true;
        while(bus_mutex.try_lock() == false){wait(Port_CLK.default_event());};
        Port_BusAddr.write(addr);
        Port_BusFunc.write(FUNC_RESPONSE);
        Port_ProcID.write(proc_id);
        // cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": MEMORY MAIN SENDS result of <" << proc_id << "> to the bus" << endl;
        wait(Port_CLK.default_event());
        // cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": MEMORY MAIN SENT result of <" << proc_id << "> to the bus" << endl;

        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
        Port_ProcID.write("ZZZZZZZZ");
        Port_BusFunc.write("ZZZZ");
        bus_mutex.unlock();
        intended = false;
    }

    virtual void memory_controller_wait(){
        wait(Port_BusFunc.default_event());
    }

   private:
    std::mutex bus_mutex; // this mutex acts as arbiter
    std::atomic_bool intended; // for high priority memory responses
};

#endif