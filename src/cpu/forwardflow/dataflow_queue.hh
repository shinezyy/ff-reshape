//
// Created by zyy on 19-6-10.
//

#ifndef __FF_DATAFLOW_QUEUE_HH__
#define __FF_DATAFLOW_QUEUE_HH__

#include <cstdint>

struct DerivFFCPUParams;

namespace FF{

template <class Impl>
class DataflowQueue
{
public:
    typedef typename Impl::O3CPU O3CPU;
    typedef typename Impl::DynInstPtr DynInstPtr;

    typedef typename Impl::CPUPol::IEW IEW;
    typedef typename Impl::CPUPol::MemDepUnit MemDepUnit;
    typedef typename Impl::CPUPol::TimeStruct TimeStruct;

private:
    const uint32_t dispatchWidth;
    const uint32_t writebackWidth;
    const uint32_t issueWidth;
};

}

#endif //__FF_DATAFLOW_QUEUE_HH__
