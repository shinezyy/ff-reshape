//
// Created by zyy on 2020/1/18.
//

#include "debug/DQWake.hh"
#include "ready_inst_queue.hh"

namespace FF
{

template<class Impl>
ReadyInstsQueue<Impl>::ReadyInstsQueue(DerivFFCPUParams *params, const std::string& parent_name)
        :   maxReadyQueueSize(params->MaxReadyQueueSize),
            preScheduledQueues(nOpGroups),
            _name(parent_name + ".ReadyInstQueue")
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
ReadyInstsQueue<Impl>::insert(std::list<DynInstPtr> &q, DynInstPtr inst)
{
    if (!age) {
        q.push_back(inst);
    } else {
        auto pos = q.begin(), e = q.end();
        while (pos != e && (*pos)->seqNum > inst->seqNum) {
            pos++;
        }
        q.insert(pos, inst);
    }
}

template<class Impl>
void
ReadyInstsQueue<Impl>::insertInst(OpGroups group, DynInstPtr &inst)
{
    insert(preScheduledQueues[group], inst);
}

template<class Impl>
void
ReadyInstsQueue<Impl>::insertEmpirically(DynInstPtr &inst)
{
    if (preScheduledQueues[OpGroups::MultDiv].size() <
        preScheduledQueues[OpGroups::FPAdd].size()) {
        insert(preScheduledQueues[OpGroups::MultDiv], inst);
        DPRINTF(DQWake, "Inst[%lu] inserted into MD queue empirically\n", inst->seqNum);

    } else {
        insert(preScheduledQueues[OpGroups::FPAdd], inst);
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

#include "cpu/forwardflow/isa_specific.hh"

template class FF::ReadyInstsQueue<FFCPUImpl>;
