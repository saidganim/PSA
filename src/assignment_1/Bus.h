#ifndef BUS_MOD
#define BUS_MOD

#include <mutex>
#include <atomic>
#include <queue>
#include <systemc.h>

struct request {
    int id;
    int sourceid;
    int addr;
    int func;
};

enum Function
{
    FUNC_NOTHING,
    FUNC_READ,
    FUNC_WRITE,
    FUNC_INVALIDATE,
    FUNC_RESPONSE,
    FUNC_REQUESTED,
};

class Bus_if : public virtual sc_interface
{
  public:
    // These methods needed for CPUs to send requests
    virtual bool read(int, int) = 0;
    virtual bool write(int, int) = 0;
    virtual int wait_for_response(int, int) = 0;
    virtual struct request wait_for_any(int) = 0;
    virtual bool cache_to_cache(int, int, int, int) = 0;
    virtual bool cacheline_invalidate(int, int) = 0;
    // virtual void acquire_bus_lock() = 0;
    // virtual void release_bus_lock() = 0;

    // These methods needed for memory module to put high priority responses on the bus and get requests
    virtual void memory_response(int, int) = 0;
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
    sc_signal_rv<32> Port_SourceID; // new wire for assignment 3
                                    // needed to identify source module

    
    // sc_inout_rv<32> Port_BusAddr;
    // sc_inout_rv<4> Port_BusFunc;
    // sc_inout_rv<8> Port_ProcID;

  public:
    SC_CTOR(Bus){
        intended.store(false);
        c2c_intended.store(false);
        sensitive << Port_CLK.pos();
        // Port_ProcID(ProcID);
        // Port_BusFunc(BusFunc);
        // Port_BusAddr(BusAddr);
        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
        Port_ProcID.write("ZZZZZZZZ");
        Port_SourceID.write("ZZZZZZZZ");
        Port_BusFunc.write("ZZZZ");
        dont_initialize();
    }
    virtual bool read(int proc_id, int addr){
        if (c2c_intended.load() || intended.load() || (bus_mutex.try_lock() == false))
            return false;   // Cache module has to wait one cycle and try again.
                            // This mutexes may be not fair, but we use them in our module.
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": BUS received read" << endl;

        Port_BusAddr.write(addr);
        Port_BusFunc.write(FUNC_READ);
        Port_ProcID.write(proc_id);
        wait(Port_CLK.default_event());
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": BUS wrote read" << endl;
        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
        Port_ProcID.write("ZZZZZZZZ");
        Port_BusFunc.write("ZZZZ");
        bus_mutex.unlock();
        return true;
    };
    virtual bool write(int proc_id, int addr){
        
         if (c2c_intended.load() || intended.load() || bus_mutex.try_lock() == false)
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

    virtual int wait_for_response(int proc_id, int addr){
        int res = CACHEL_EXCLUSIVE;
    lbl_wait:
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": Cache of CPU <" << proc_id << "> waits for response on bus on addr " << addr << endl;
        wait(Port_CLK.value_changed_event());
        while(Port_BusAddr.read().to_int() != addr ||
        Port_ProcID.read().to_int() != proc_id || 
        Port_BusFunc.read().to_int() != FUNC_RESPONSE){
            // cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": Cache of CPU <" << proc_id << "> got response but not own <" << Port_ProcID.read().to_int() << ">" <<endl;
            wait(Port_CLK.value_changed_event());
        }
        if(Port_BusFunc.read().to_int() == FUNC_REQUESTED){
            // just notifying that other core also requested the cacheline
            // so just put status to shared, but wait for real reply from DRAM controller
            res = CACHEL_SHARED;
            goto lbl_wait;
        }
        if(Port_SourceID.read().to_int() != (int)DRAM_IDENTIFIER){
            // state of the cacheline should be shared
            return CACHEL_SHARED;
        }
        return res;
        // cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": Cache of CPU <" << proc_id << "> got response for its own <" << Port_ProcID.read().to_int() << "> at address " << Port_BusAddr.read().to_int() <<endl;

    }

    // Now this function returns WRITE and READ operations to let
    // the snooping process make cache-to-cache transfers
    // and perform local cachelines states changes. 
    virtual struct request wait_for_any(int proc_id){
        struct request res;
      label1:
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": Snooping Cache of CPU <" << proc_id << "> waits for requests on bus" << endl;
        wait(Port_BusFunc.value_changed_event());
        // if it's own request or it's not writing request - then skip it
        if((Port_BusFunc.read().to_int() != FUNC_INVALIDATE && 
        Port_BusFunc.read().to_int() != FUNC_WRITE && Port_BusFunc.read().to_int() != FUNC_READ) 
        || Port_ProcID.read().to_int() == proc_id) goto label1;

        // else handle request correctly
        // cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": Snooping Cache of CPU <" << proc_id << "> got request <" << Port_ProcID.read().to_int() << ">" <<endl;
        res.addr = Port_BusAddr.read().to_int();
        res.func = Port_BusFunc.read().to_int();
        res.id = Port_ProcID.read().to_int();
        return res;
    }
    
    virtual struct request get_next_request(){
    label1:
        // cout << sc_time_stamp() << ": MEMORY snooping is waiting for the next request" << endl;
        wait(Port_CLK.default_event());
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
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": MEMORY MAIN SENDS result to the bus from addr " << addr << endl;
        while(c2c_intended.load())wait(Port_CLK.default_event()); // sleep only one cycle, because of c2c communications are fast - one cycle
        intended = true;
        while(bus_mutex.try_lock() == false){intended = true; wait(Port_CLK.default_event());};
        Port_BusAddr.write(addr);
        Port_BusFunc.write(FUNC_RESPONSE);
        Port_ProcID.write(proc_id);
        Port_SourceID.write(DRAM_IDENTIFIER);
        // cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": MEMORY MAIN SENDS result of <" << proc_id << "> to the bus" << endl;
        wait(Port_CLK.default_event());
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": MEMORY MAIN SENT result of <" << proc_id << "> to the bus" << endl;

        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
        Port_ProcID.write("ZZZZZZZZ");
        Port_SourceID.write("ZZZZZZZZ");
        Port_BusFunc.write("ZZZZ");
        bus_mutex.unlock();
        intended = false;
    }

    virtual void memory_controller_wait(){
        wait(Port_BusFunc.default_event());
    }

    virtual bool cache_to_cache(int proc_id, int source_id, int addr, int clstate){
        c2c_intended = true;
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": CACHE TO CACHE <" << source_id << "> to the "<< proc_id << endl;
        while(bus_mutex.try_lock() == false){ wait(Port_CLK.default_event());};
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": CACHE TO CACHE <" << source_id << "> to the IS LOCKED"<< proc_id << endl;        
        Port_BusAddr.write(addr);
        Port_BusFunc.write(clstate == CACHEL_REQUESTED? FUNC_REQUESTED : FUNC_RESPONSE);
        Port_ProcID.write(proc_id);
        Port_SourceID.write(source_id); // this field identifies that response is not sent by the DRAM controller
        wait(Port_CLK.default_event());
        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
        Port_ProcID.write("ZZZZZZZZ");
        Port_SourceID.write("ZZZZZZZZ");
        Port_BusFunc.write("ZZZZ");
        bus_mutex.unlock();
        c2c_intended = false;
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": CACHE TO CACHE <" << source_id << "> to the "<< proc_id << " IS FINISHED " << endl;
        return true;
    }

    virtual bool cacheline_invalidate(int addr, int proc_id){
        // send cacheline invalidation request on to the bus.
        // this request shouldn't be prioritiezed. but it should be supported
        // with always checking the status of the cacheline, if it's invalid - it doesnt have permission to invalidate it
        // also we should keep in mind possible deadlocks here.
        if (c2c_intended.load() || intended.load() || bus_mutex.try_lock() == false)
            return false;
        cout << "CPU #" <<proc_id<<":" << sc_time_stamp() << ": BUS received write" << endl;
        Port_BusAddr.write(addr);
        Port_BusFunc.write(FUNC_INVALIDATE);
        Port_ProcID.write(proc_id);
        wait(Port_CLK.default_event());
        Port_BusAddr.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
        Port_ProcID.write("ZZZZZZZZ");
        Port_BusFunc.write("ZZZZ");
        bus_mutex.unlock();
        return true;
    };

    // virtual void acquire_bus_lock(){
    //     while(!bus_mutex.try_lock())wait(Port_CLK.default_event());
    // };

    // virtual void release_bus_lock(){
    //     bus_mutex.unlock();
    // };

   private:
    std::mutex bus_mutex; // this mutex acts as arbiter
    std::atomic_bool intended; // for high priority memory responses
    std::atomic_bool c2c_intended; // for the highest priority cache-to-cache responses
};

#endif