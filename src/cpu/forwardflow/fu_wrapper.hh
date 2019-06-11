//
// Created by zyy on 19-6-11.
//

#ifndef __FF_FU_WRAPPER_HH__
#define __FF_FU_WRAPPER_HH__

#include <cpu/func_unit.hh>

class FUWrapper {
    FuncUnit fu;
public:

    bool canServeInst(DynInstPtr &inst);

    bool checkCapability(DynInstPtr &inst);

    bool isOneCycle(inst);

    bool transferPointer();
    bool transferValue();

    bool tick();
    // tick will
    // clear this cycle
    // check status of buffers
    // assert no output hazard
    // check input and schedule, response to requests
    // "execute" tasks scheduled in last cycle
    //
    // write back ptr (wake up), write back value
    // (use routes acquired by last cycle) (two actions can be swapped)

    bool pointerTransferred;
    bool valueTransferred;

    void clearThisCycle();

    void setPtrQueue(Queue);

    void setValueQueue(Queue);
private:
    bool hasLongLatOpReadyNextCycle();

    bool writebackPortBusyNextCycle();

    std::vector<Value> buffer(num_fu);
};

#endif //__FF_FU_WRAPPER_HH__
