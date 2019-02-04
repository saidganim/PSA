#ifndef CACHE_MOD
#define CACHE_MOD

#include "utils.h"
#include <math.h>
#include "Bus.h"
#include <sched.h>

extern std::atomic<unsigned int> _main_memory_access_rate;
extern std::atomic<unsigned int > _time_for_bus_acquisition;


class SingleCache : public sc_module {
    public:    
    struct cache_record_t{
        int counter;    // need to implement LRU logic
        int state;    // MOESI, but lets keep 4 bits for this field
        int tag: 24;    // we assumte that we have 32bit system, so we have 4KB pages.
                        // Since cacheline size is 32-bytes- we need only 5[0-4] bits for inner offset,
                        // we could use bits [11-5] to encode index, since we need only 3 bits,
                        // we will use only [7-5] bits to encode index of address.
                        // (those bits will be the same for virtual and physical addresses)
                        // the rest will go for the tag.
        int data[8];    // no comments...
    };
    int id;

    // From CPU
    sc_in<bool>     Port_CLK;
    sc_in<Memory::Function> Port_Func;
    sc_in<int>      Port_Addr;
    sc_out<Memory::RetCode> Port_Done;
    // sc_inout_rv<32> Port_Data;

    // To Bus module
    sc_port<Bus_if> bus{"cache_to_bus"};
    // sc_inout_rv<32> Port_Data_MEM; // -- not being used anymore for simple modeling

    SC_CTOR(SingleCache)
    {
        SC_THREAD(snooping);
        SC_THREAD(execute);
        sensitive << Port_CLK.pos();
        dont_initialize();
        cachelines = new struct cache_record_t[CACHE_SIZE];
        memset(cachelines, 0x0, sizeof(struct cache_record_t) * CACHE_SIZE);
    }

    ~SingleCache()
    {
        delete[] cachelines;
    }

private:
    struct cache_record_t* cachelines;

    int addr_to_index(int addr){ // This function is just for the mappingaddr to cache set
        return (((addr & CACHEINDEX_MASK) >> CACHEINDEX_SHIFT) * CACHE_SET_SIZE) % CACHE_SIZE;
    }

    void snooping(){
        // this thread actually performs snooping on interconnection bus and invalids cacheline if write was sent
        while(true){
            struct request req = bus->wait_for_any(id);
            int index = addr_to_index(req.addr);
            int rindex = -1;
            for(int i = index; i < index + CACHE_SET_SIZE; ++i){
                if((cachelines[i].state != CACHEL_INVALID) && 
                (cachelines[i].tag == ((req.addr & CACHETAG_MASK) >> CACHETAG_SHIFT))){
                    // cacheline is presented
                    rindex = i;
                } 
            }
            if(rindex > -1){

                if(req.func == Memory::FUNC_WRITE ||
                    req.func == Memory::FUNC_INVALIDATE ){
                        // Invalidate cacheline.
                        cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CACHE received invalidated for " << req.addr << endl;
                        cachelines[rindex].state = CACHEL_INVALID; // INVALID
                } else if(req.func == Memory::FUNC_READ){
                    if(cachelines[rindex].state == CACHEL_REQUESTED)
                        cachelines[rindex].state = CACHEL_SHARED;
                    // we have to send response to the requestor ...    
                    sc_spawn(sc_bind(&Bus::cache_to_cache, dynamic_cast<Bus*>(bus.get_interface()), req.id, id, req.addr, &cachelines[rindex].state)); //  this thread has to be issued in parallel
                }
            }
        }
    }


    void execute()
    {
        timespec time1, time2;
        while (true)
        {
            wait(Port_Func.value_changed_event());

            Memory::Function f = Port_Func.read();
            int addr   = Port_Addr.read();
            if (f == Memory::FUNC_WRITE)
            {
                cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CACHE received write" << endl;
            }
            else
            {
                cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CACHE received read" << endl;
            }

            
            // First lets check if data is cached...
            int index;
            int rindex;
            int min_id;
            int min_val;
            int wbaddr;

            index = addr_to_index(addr); //  We have 8 entries per set
        check_the_cachelinestat:
            rindex = -1;
            min_id = index;
            min_val = MAX_COUNTER + 2;
            for(int i = index; i < index + CACHE_SET_SIZE; ++i){
                if((cachelines[i].state != CACHEL_INVALID) && (cachelines[i].tag == ((addr & CACHETAG_MASK) >> CACHETAG_SHIFT))){
                    rindex = i;
                } else {
                    // saving the minimum counter in advance
                    if((cachelines[i].state == CACHEL_INVALID)){
                        min_id = i;
                        min_val = -1; // to be sure...
                    } else if(cachelines[i].counter < min_val) {
                        min_val = cachelines[i].counter; 
                        min_id = i;
                    }
                    cachelines[i].counter = std::max(0x0, cachelines[i].counter - 1);
                }
            }
            wait(Port_CLK.default_event()); // simulating one cycle of cache hit/miss...
            if(rindex > -1){
                // Data is cached
                cachelines[rindex].counter = MAX_COUNTER;
                if(f == Memory::FUNC_WRITE){   
                    // first have to check that cache is not invalidated by somebody else
                    // I use goto here, but it's reasonable(ask kernel hackers)
                    // If current thread coudln't acquire the bus lock - it has to recheck that
                    // cacheline status is in corect state(NOT INVALID), only then it can try to acquire bus lock again,
                    // otherwise - have to request new copy of cacheline from the memory. The problem of deadlock is presented here.
                    // that's why we need to acquire lock during writing(invalidate-then-write), otherwise two processes could
                    // invalidate each other forever.

                    // here we need only send invalidate message, that's it, then our cache will be in MODIFIED state
                    while(!bus->cacheline_invalidate(addr, id)){
                        wait(Port_CLK.default_event()); // wait for a one cycle
                        wait(Port_CLK.negedge_event()); // need to wait half of the cycle to let cacheline be invalidated.

                        if(cachelines[rindex].state == CACHEL_INVALID) {
                        // this is the most ugly piece of code in my solution
                        // but there is no way to avoid it, sorry :)
                            wait(Port_CLK.default_event());
                            goto check_the_cachelinestat;
                        }
                    };
                    // cacheline invalidated signal is sent.
                    // now lets change state of cacheline to modified.
                    cachelines[rindex].state = CACHEL_MODIFIED;

                    cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CACHE WRITE HIT " << endl;
                    stats_writehit(id);
                    // data = Port_Data.read().to_int();
                    // cachelines[rindex].data[addr & 0x1f / 4] = data;
                    // cachelines[rindex].state |= CACHEL_DIRTY;
                    Port_Done.write(Memory::RET_WRITE_DONE);
                    wait(Port_CLK.default_event());// simulating one cycle of write to the cache...
                } else {
                    wait(Port_CLK.negedge_event());
                    if(cachelines[rindex].state == CACHEL_INVALID) {
                    // this is needed because of in one cycle of simulation hit/miss
                    // somebody could invalidate the cacheline :(
                        wait(Port_CLK.default_event());
                        goto check_the_cachelinestat;
                    }
                    cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CACHE READ HIT " << endl;                    
                    stats_readhit(id);
                    // Port_Data.write((addr < MEM_SIZE) ? cachelines[rindex].data[addr & 0x1f / 4] : 0);
                    Port_Done.write(Memory::RET_READ_DONE);
                    wait(Port_CLK.default_event());// simulating one cycle of reading from the cache...
                }
                // Port_Data.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
            } else{
                // Data is not cached, need to request from the memory
                if(f == Memory::FUNC_WRITE)
                    cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CACHE WRITE MISS " << endl;
                else
                    cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CACHE READ MISS " << endl;
                // but first have to write-back one cacheline if there is no empty one and cacheline is dirty
                if(cachelines[min_id].state == CACHEL_MODIFIED || cachelines[min_id].state == CACHEL_OWNED){
                    cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CACHE WRITING-BACK CACHELINE" << endl;
                    wbaddr = cachelines[min_id].tag << CACHETAG_SHIFT | (addr & ~CACHETAG_MASK & ~0b11111);
                    for(int ii = 0; ii < 20; ++ii)sched_yield();                    
                    _main_memory_access_rate.fetch_add(1, std::memory_order_relaxed);                    
                    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time1);
                    while(!bus->write(id, wbaddr)){
                        // wait(Port_CLK.default_event()); 
                        wait(Port_CLK.default_event());
                        if(!(cachelines[min_id].state == CACHEL_MODIFIED || cachelines[min_id].state == CACHEL_OWNED))
                            goto _post_writeback; // no need to make writeback - it's already done modification by somebody else there
                    } // trying to send request to the bus on every cycle
                    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time2);
                    unsigned int result = (time2.tv_sec - time1.tv_sec) * 1e6 + (time2.tv_nsec - time1.tv_nsec) / 1e3;
                    _time_for_bus_acquisition.fetch_add(result, std::memory_order_relaxed);
                    bus->wait_for_response(id, wbaddr); // waiting when request will be responded by memory through the bus 
                }
            _post_writeback:

                // then do actual reading from memory to cache
                cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CACHE sends read" << endl;
                for(int ii = 0; ii < 20; ++ii)sched_yield();
                _main_memory_access_rate.fetch_add(1, std::memory_order_relaxed);
                cachelines[min_id].state = CACHEL_REQUESTED;
                clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time1);
                while(!bus->read(id, addr & ~0b11111))wait(Port_CLK.default_event());
                clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time2);
                unsigned int result = (time2.tv_sec - time1.tv_sec) * 1e6 + (time2.tv_nsec - time1.tv_nsec) / 1e3;
                _time_for_bus_acquisition.fetch_add(result, std::memory_order_relaxed);
                // if somebody requested this cacheline, then shared. Not exclusive.
                cachelines[min_id].state = cachelines[min_id].state == CACHEL_REQUESTED? bus->wait_for_response(id, addr & ~0b11111) : cachelines[min_id].state;
                cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CACHE reads cacheline" << endl;
                cachelines[min_id].tag = (addr & CACHETAG_MASK) >> CACHETAG_SHIFT;
                cachelines[min_id].counter = MAX_COUNTER;
                // Write-back phase is finished
                if (f == Memory::FUNC_WRITE)
                {
                    // invalidating the cacheline and changing the status to modified
                    if(cachelines[min_id].state == CACHEL_SHARED)
                        while(!bus->cacheline_invalidate(addr, id)){
                            wait(Port_CLK.default_event()); // wait for a one cycle
                            wait(Port_CLK.negedge_event()); // need to wait half of the cycle to let cacheline be invalidated.
                            if(cachelines[min_id].state == CACHEL_INVALID) {
                            // this is the most ugly piece of code in my solution
                            // but there is no way to avoid it, sorry :)
                                wait(Port_CLK.default_event());
                                goto _post_writeback;
                            }
                        };

                    stats_writemiss(id);
                    // cachelines[min_id].data[addr & 0b11111] = Port_Data.read().to_int();
                    cachelines[min_id].state = CACHEL_MODIFIED;
                    cout << "CPU #" <<id<<":" << sc_time_stamp() << ": CACHE performs write-through" << endl;                   
                    Port_Done.write(Memory::RET_WRITE_DONE);
                    wait(Port_CLK.default_event());
                } else {
                    stats_readmiss(id);
                    // Port_Data.write(cachelines[min_id].data[addr & 0b11111]);
                    // Port_Data.write(1234);
                    Port_Done.write(Memory::RET_READ_DONE);
                    wait(Port_CLK.default_event());
                    // Port_Data.write("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
                }
            }
        }
    }   
};

#endif