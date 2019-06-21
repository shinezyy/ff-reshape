//
// Created by zyy on 19-6-11.
//

#ifndef __FF_ARCH_REGFILE_HH__
#define __FF_ARCH_REGFILE_HH__

#include <tuple>
#include "dq_pointer.hh"

namespace FF {

using std::tuple;

template<class Impl>
class ArchState {
private:
    //Typedefs from Impl
    typedef typename Impl::CPUPol CPUPol;
    typedef typename Impl::DynInstPtr DynInstPtr;
    typedef typename Impl::O3CPU O3CPU;

    const unsigned WritePorts, ReadPorts;

    unsigned writes, reads;

    bool makeCheckPoint(DynInstPtr &inst);

    // data structures here:

    // arch reg file
    // newest value in RF?
    // arch reg pointers: last use and last def


public:
    bool commitInst(DynInstPtr &inst);

    // todo: update map to tell its parent or sibling where to forward
    PointerPair recordAndUpdateMap(DynInstPtr &inst);

    void clearCounters();

    bool checkRWLimit();

    bool checkpointsFull();

    void recoverCPT(DynInstPtr &inst);

};

}

#endif //__FF_ARCH_REGFILE_HH__
