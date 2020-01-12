//
// Created by zyy on 2020/1/15.
//

#include "cpu/forwardflow/isa_specific.hh"
#include "dataflow_queue_common.hh"
#include "ready_inst_queue.hh"

namespace FF
{

boost::dynamic_bitset<> DQCommon::uint2Bits(unsigned from)
{
    auto res = boost::dynamic_bitset<>(dqSize);
    for (unsigned i = 0; i < addrWidth; i++, from >>= 1) {
        res[i] = from & 1;
    }
    return res;
}

DQPointer DQCommon::uint2Pointer(unsigned u) const
{
    unsigned group_id = u / groupSize;
    unsigned group_offset = u % groupSize;
    unsigned bank_id = u / bankSize;
    unsigned bank_offset = group_offset % bankSize; //index
    return DQPointer{true, group_id, bank_id, bank_offset, 0};

}

unsigned DQCommon::pointer2uint(const DQPointer &ptr) const
{
    return ptr.group * groupSize + ptr.bank * bankSize + ptr.index;
}

unsigned DQCommon::pointer2uint(const WKPointer &ptr) const
{
    return ptr.group * groupSize + ptr.bank * bankSize + ptr.index;
}

DQCommon::DQCommon(DerivFFCPUParams *params)
        :
        nullDQPointer{false, 0, 0, 0, 0},
        nullWKPointer(WKPointer()),
        bankSize(params->DQDepth),

        nBanks(params->numDQBanks),
        groupSize(nBanks * bankSize),
        nGroups(params->numDQGroups),
        dqSize(nGroups * groupSize),
        addrWidth(ceilLog2(dqSize)),
        termMax(params->TermMax)
{
}

void DQCommon::notImplemented()
{
    panic("Not implemented!\n");
}


}
