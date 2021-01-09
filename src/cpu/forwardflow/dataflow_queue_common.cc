//
// Created by zyy on 2020/1/15.
//

#include "cpu/forwardflow/isa_specific.hh"
#include "dataflow_queue_common.hh"
#include "debug/DQGOF.hh"
#include "ready_inst_queue.hh"

namespace FF
{

boost::dynamic_bitset<> DQCommon::uint2Bits(unsigned from)
{
    auto res = boost::dynamic_bitset<>(addrWidth);
    for (unsigned i = 0; i < addrWidth; i++, from >>= 1) {
        res[i] = from & 1;
    }
    return res;
}

DQPointer DQCommon::uint2Pointer(unsigned u) const
{
    unsigned group_id = u / groupSize;
    unsigned group_offset = u % groupSize;
    unsigned index = group_offset / nBanks;
    unsigned bank_id = group_offset % nBanks; //index
    return DQPointer{true, group_id, bank_id, index, 0};

}

unsigned DQCommon::pointer2uint(const BasePointer &ptr) const
{
    return ptr.group * groupSize + ptr.bank + ptr.index * nBanks;
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
        termMax(params->TermMax),
        nOps(params->numOperands)
{
}

void DQCommon::notImplemented()
{
    panic("Not implemented!\n");
}

unsigned
DQCommon::computeDist(const BasePointer &y, const BasePointer &o)
{
    unsigned yu = pointer2uint(y);
    unsigned ou = pointer2uint(o);
    if (ou > yu) {
        yu += dqSize;
    }
    return yu - ou;
}

}
