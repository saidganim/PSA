#ifndef BUS_MOD
#define BUS_MOD
class Bus_if : public virtual sc_interface
{
  public:
    virtual bool read(int addr) = 0;
    virtual bool write(int addr, int data) = 0;
};
class Bus : public Bus_if, public sc_module
{
  public:
    // p o r t s
    sc_in<bool>
        Port_CLK;
    sc_signal_rv<32> Port_BusAddr;

  public:
    SC_CTOR(Bus){}
    virtual bool read(int addr){
        Port_BusAddr.write(addr);
        return true;
    };
    virtual bool write(int addr, int data)
    {
        // H a n d l e c o n t e n t i o n i f any
        Port_BusAddr.write(addr);
        // Data d o e s n o t h a v e t o be h a n d l e d i n t h e s i m u l a t i o n
        return true;
    }
}
#endif