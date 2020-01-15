//
// Created by zyy on 2020/1/15.
//

#include "cpu/forwardflow/isa_specific.hh"
#include "dataflow_queue_common.hh"

namespace FF {
DQCommon dqCommon;

boost::dynamic_bitset<> DQCommon::uint2Bits(unsigned from) {
    auto res = boost::dynamic_bitset<>(dqSize);
    for (unsigned i = 0; i < addrWidth; i++, from >>= 1) {
        res[i] = from & 1;
    }
    return res;
}

DQPointer DQCommon::uint2Pointer(unsigned u) const {
    unsigned group_id = u / groupSize;
    unsigned group_offset = u % groupSize;
    unsigned bank_id = u / bankSize;
    unsigned bank_offset = group_offset % bankSize; //index
    return DQPointer{true, group_id, bank_id, bank_offset, 0};
}

unsigned DQCommon::pointer2uint(const DQPointer &ptr) const {
    return ptr.group * groupSize + ptr.bank * bankSize + ptr.index;
}

unsigned DQCommon::pointer2uint(const WKPointer &ptr) const {
    return ptr.group * groupSize + ptr.bank * bankSize + ptr.index;
}

void DQCommon::init(DerivFFCPUParams *params)
{
    bankSize = params->DQDepth;

    nBanks = params->numDQBanks;
    groupSize = nBanks * bankSize;

    nGroups = params->numDQGroups;
    dqSize = nGroups * groupSize;

    addrWidth = ceilLog2(dqSize);
}


template<class Impl>
ReadyInstsQueue<Impl>::ReadyInstsQueue(DerivFFCPUParams *params)
        :   maxReadyQueueSize(params->MaxReadyQueueSize),
            preScheduledQueues(nOpGroups)
{
}

template<class Impl>
void
ReadyInstsQueue<Impl>::squash(InstSeqNum seq)
{
    for (auto &q: preScheduledQueues) {
        const auto end = q.end();
        auto it = q.begin();
        while (it != end) {
            if ((*it)->seqNum > seq) {
                it = q.erase(it);
            } else {
                it++;
            }
        }
    }
}

template<class Impl>
typename Impl::DynInstPtr
ReadyInstsQueue<Impl>::getInst(OpGroups group)
{
    assert(group < preScheduledQueues.size());
    if (!preScheduledQueues[group].empty()) {
        return preScheduledQueues[group].front();
    } else {
        return nullptr;
    }
}

template<class Impl>
void
ReadyInstsQueue<Impl>::insertInst(OpGroups group, DynInstPtr &inst)
{
    preScheduledQueues[group].push_back(inst);
}

template<class Impl>
void
ReadyInstsQueue<Impl>::insertEmpirically(DynInstPtr &inst)
{
    if (preScheduledQueues[OpGroups::MultDiv].size() <
        preScheduledQueues[OpGroups::FPAdd].size()) {
        preScheduledQueues[OpGroups::MultDiv].push_back(inst);
        DPRINTF(DQWake, "Inst[%lu] inserted into MD queue empirically\n", inst->seqNum);

    } else {
        preScheduledQueues[OpGroups::FPAdd].push_back(inst);
        DPRINTF(DQWake, "Inst[%lu] inserted into FPAdd queue empirically\n", inst->seqNum);
    }
}

template<class Impl>
bool
ReadyInstsQueue<Impl>::isFull()
{
    return isFull(OpGroups::FPAdd) || isFull(OpGroups::MultDiv);
}

template<class Impl>
bool
ReadyInstsQueue<Impl>::isFull(OpGroups group)
{
    assert(group < preScheduledQueues.size());
    return preScheduledQueues[group].size() >= maxReadyQueueSize;
}

}
