//
// Created by zyy on 19-6-11.
//

#include "cpu/forwardflow/fu_wrapper.hh"

#include <sstream>

#include "base/trace.hh"
#include "cpu/op_class.hh"
#include "debug/FUPipe.hh"
#include "debug/FUSched.hh"
#include "debug/FUW.hh"
#include "debug/FUW2.hh"
#include "debug/ObExec.hh"
#include "debug/ObFU.hh"
#include "debug/RSProbe1.hh"
#include "fu_wrapper.hh"
#include "params/FFFUPool.hh"
#include "params/FUDesc.hh"
#include "params/OpDesc.hh"
#include "sim/sim_object.hh"


namespace FF{

using namespace std;

template<class Impl>
bool FUWrapper<Impl>::canServe(const DynInstPtr &inst, InstSeqNum &waitee) {
    assert(inst);
    DPRINTF(FUW2, "FUW , opclass:%d\n", inst->opClass());
    auto has_capability = capabilityList[inst->opClass()];
    bool fu_free = true;

    if (has_capability) {
        const auto &wrapper = wrappers[inst->opClass()];
        fu_free =
            (wrapper->isLongLatency && !wrapper->isPipelined && !wrapper->hasPendingInst)
            || (wrapper->isSingleCycle && !wrapper->hasPendingInst)
            || (wrapper->isPipelined && !wrapper->writtenThisCycle);

        if (has_capability && !fu_free) {
            waitee = wrapper->seq;
        } else {
            waitee = 0;
        }
    }

    bool ret = has_capability && fu_free;
    if (ret) {
        DPRINTF(ObFU || Debug::ObExec, "Inst %s is accepted\n",
                inst->staticInst->disassemble(inst->instAddr()));
    } else {
        DPRINTF(ObFU || Debug::ObExec, "Inst %s is rejected because of %s\n",
                inst->staticInst->disassemble(inst->instAddr()),
                !has_capability ? "no capability!" :
                !fu_free ? "fu busy" : "Unknow reason!");
    }
    return ret;
}

template<class Impl>
bool FUWrapper<Impl>::consume(const FUWrapper::DynInstPtr &inst)
{
    auto lat = opLat[inst->opClass()];
    SingleFUWrapper *wrapper = wrappers[inst->opClass()];
    assert(lat == wrapper->latency);

    inst->opLat = lat;
    insts[inst->seqNum] = inst;
    inst->fuGranted = true;

    DPRINTF(FUW||Debug::RSProbe1,
            "w(%i, %i) Consuming inst[%d]\n", wrapperID, inst->opClass(), inst->seqNum);

    DQPointer &dest = inst->pointers[0];

    std::array<WKPointer, 2> to_wake;
    to_wake[SrcPtr].valid = false;
    to_wake[DestPtr].valid = false;

    if (dest.valid) {
        if (inst->isLoad() && inst->isNormalBypass()) {
            DPRINTFR(FUW, "Skip to wake up " ptrfmt ", (already been bypassed)\n",
                    extptr(dest));
            // to_wake[DestPtr] = dest;
            // to_wake[DestPtr].hasVal = true;
            // to_wake[DestPtr].val = inst->bypassVal;

        } else if (!(inst->isLoad() || inst->isStoreConditional() || inst->isForwarder())) {
            DPRINTFR(FUW, "to wake up " ptrfmt "\n", extptr(dest));
            to_wake[DestPtr] = dest;

            if (inst->isReadBarrier() || inst->isWriteBarrier()) {
                DPRINTFR(FUW, "without value because of it's barrier\n");
                to_wake[DestPtr].wkType = WKPointer::WKType::WKOrder;
                to_wake[DestPtr].hasVal = false;

            } else if (inst->isNormalStore()) {
                // done: advanced waking up successor as soon as the value reg available
                DPRINTFR(FUW, "canceled because of it's producing store\n");
                to_wake[DestPtr].valid = false;
                // passing store value to predicted consumer
                // to_wake[DestPtr].hasVal = true;
                // to_wake[DestPtr].val.i = inst->readStoreValue();
                // to_wake[DestPtr].wkType = WKPointer::WKType::WKBypass;
            }

        } else {
            DPRINTFR(FUW, "(let loads/SC/forwarder) forget to wake up " ptrfmt "\n",
                     extptr(dest));
        }
    }
    if (inst->numDestRegs() > 0) {
        if (!(inst->isLoad() || inst->isStoreConditional() || inst->isForwarder())) {
            to_wake[SrcPtr] = inst->dqPosition;
            if (!dest.valid && !inst->isForwarder()) {
                inst->destReforward = true;
                DPRINTFR(FUW, "to wake up itself "
                        " who has children but not dispatched yet\n");
            } else {
                DPRINTFR(FUW, "to wake up itself "
                        " in case that current child was squashed\n");
            }
        } else {
            DPRINTFR(FUW, "(let loads/SC/forwarder forget) to wake up " ptrfmt "\n",
                    extptr(dest));
        }
    } else {
        DPRINTFR(FUW, "with no ISA-defined dest\n");
    }

    // store wakeup info
    if (wrapper->isSingleCycle) {
        assert(!wrapper->hasPendingInst);

        wrapper->hasPendingInst = true;  // execute in one cycle
        wrapper->seq = inst->seqNum;

        wrapper->oneCyclePointer[SrcPtr] = to_wake[SrcPtr];
        wrapper->oneCyclePointer[DestPtr] = to_wake[DestPtr];

        DPRINTF(FUW, "Add inst[%i] into 1-cycle wrapper (%i, %i)\n",
                inst->seqNum, wrapperID, inst->opClass());

    } else if (wrapper->isPipelined) {

        if (!wrapper->active &&
                (wrapper->pipelineQueue.size() == wrapper->latency)) {
            DPRINTF(FUPipe, "wrapper(%i, %i) pop front to maintain size\n",
                    wrapperID, inst->opClass());
            wrapper->pipelineQueue.pop_front();
        }

        DPRINTF(FUPipe, "@ consume, wrapper(%i, %i) is pipelined, size:%u, will push\n",
                wrapperID, inst->opClass(), wrapper->pipelineQueue.size());
        wrapper->pipelineQueue.push_back({true, to_wake, inst->seqNum});
        wrapper->pipeInstCount += 1;

        assert(!wrapper->writtenThisCycle);
        wrapper->writtenThisCycle = true;

        DPRINTF(FUW, "Add inst[%i] into pipelined wrapper (%i, %i) with lat %u\n",
                inst->seqNum, wrapperID, inst->opClass(), wrapper->latency);
    } else {
        assert(wrapper->isLongLatency);
        if (wrapper->hasPendingInst) {
            DPRINTF(FUW, "w(%i, %i) hasPendingInst on consuming new inst\n",
                    wrapperID, inst->opClass());
            assert(!wrapper->hasPendingInst);
        }

        wrapper->hasPendingInst = true;
        wrapper->seq = inst->seqNum;
        wrapper->cycleLeft = lat;
        wrapper->longLatencyPointer = to_wake;

        DPRINTF(FUW, "Add inst[%i] into LL wrapper (%i, %i)\n",
                inst->seqNum, wrapperID, inst->opClass());
    }

    return true;
}

template<class Impl>
void FUWrapper<Impl>::tick() {
    panic("should not be called!\n");
}

template<class Impl>
int FUWrapper<Impl>::setInstWakeup(
        const WBPair &to_wakeup, InstSeqNum seq)
{
    int count = to_wakeup[0].valid || to_wakeup[1].valid;
    WBPos pos = wbQueue.begin(), e = wbQueue.end();
    if (count) {
        while (pos != e && pos->seq > seq) {
            pos++;
        }
        pos = wbQueue.emplace(pos, to_wakeup, seq);
    }
    execQueue.emplace_back(seq, pos);
    return count;
}

template<class Impl>
void FUWrapper<Impl>::setWakeupPostExec()
{
    if (!wbQueue.empty()) {
        toWakeup = wbQueue.front().wbPair;
        wbQueue.pop_front();

    } else {
        toWakeup[0].valid = false;
        toWakeup[1].valid = false;
    }

    if (Debug::FUW) {
        const auto &src = toWakeup[SrcPtr];
        const auto &dest = toWakeup[DestPtr];
        if (src.valid) {
            DPRINTF(FUW, "To wakeup source: " ptrfmt "\n",
                    extptr(src));

        } else {
            DPRINTF(FUW, "Don't wakeup source: " ptrfmt "\n",
                    extptr(src));
        }
        if (dest.valid) {
            DPRINTF(FUW, "To wakeup dest: " ptrfmt "\n",
                    extptr(dest));

        } else {
            DPRINTF(FUW, "Don't wakeup dest: " ptrfmt "\n",
                    extptr(dest));
        }
    }
}

template<class Impl>
void FUWrapper<Impl>::setWakeup() {
    for (auto &wrapper: wrappersVec) {
//        DPRINTF(FUW2, "waking in wrapper (%d, %d)\n", wrapperID, pair.first);

        // todo: has pending inst should be cleared every cycle
        if (wrapper.isSingleCycle) {
            if (wrapper.hasPendingInst) {
                DPRINTF(FUW, "w(%i, %i) Waking up children of one cycle inst[%d]\n",
                        wrapperID, wrapper.op, wrapper.seq);
                setInstWakeup(wrapper.oneCyclePointer, wrapper.seq);
            }
        } else if (wrapper.isLongLatency && !wrapper.isPipelined) {
            if (wrapper.hasPendingInst) {
                if (wrapper.cycleLeft == 1) {
                    DPRINTF(FUW, "w(%i, %i) Waking up children of LL inst[%d]\n",
                            wrapperID, wrapper.op, wrapper.seq);
                    wrapper.toNextCycle->hasPendingInst = false;
                    setInstWakeup(wrapper.longLatencyPointer, wrapper.seq);

                } else {
//                DPRINTF(FUW, "w(%i, %i) LL inst[%llu] has %i cycles left\n",
//                        wrapperID, pair.first, wrapper.seq, wrapper.cycleLeft);
                    assert(wrapper.cycleLeft > 1);
                    // keep
                    wrapper.toNextCycle->longLatencyPointer[0] = wrapper.longLatencyPointer[0];
                    wrapper.toNextCycle->longLatencyPointer[1] = wrapper.longLatencyPointer[1];
                    wrapper.toNextCycle->seq = wrapper.seq;
                    wrapper.toNextCycle->hasPendingInst = true;
                    // decrement
                    wrapper.toNextCycle->cycleLeft = wrapper.cycleLeft - 1;
                }
            }
        } else if (wrapper.isPipelined && wrapper.active) {
            if (wrapper.pipelineQueue.front().valid) {
                DPRINTF(FUW, "w(%i, %i) Waking up children of pipelined inst[%d]\n",
                        wrapperID, wrapper.op, wrapper.pipelineQueue.front().seq);
                setInstWakeup(wrapper.pipelineQueue.front().pointer,
                        wrapper.pipelineQueue.front().seq);
            }
        }
    }

}

template<class Impl>
void FUWrapper<Impl>::startCycle() {
    toWakeup[SrcPtr].valid = false;
    toWakeup[DestPtr].valid = false;
    toWakeup[SrcPtr].hasVal = false;
    toWakeup[DestPtr].hasVal = false;

    // read signals
    for (auto &wrapper: wrappersVec) {
        wrapper.writtenThisCycle = false;

        if (wrapper.isSingleCycle) {
            wrapper.hasPendingInst = false;

        } else if (wrapper.active && wrapper.isLongLatency && !wrapper.isPipelined) {
#define wrapperReadFromLast(field) \
        do { wrapper.field = wrapper.fromLastCycle->field;} while (0)

            wrapperReadFromLast(hasPendingInst);
            wrapperReadFromLast(seq);
            // wrapperReadFromLast(oneCyclePointer[SrcPtr]);
            // wrapperReadFromLast(oneCyclePointer[DestPtr]);
            wrapperReadFromLast(longLatencyPointer[SrcPtr]);
            wrapperReadFromLast(longLatencyPointer[DestPtr]);
            wrapperReadFromLast(cycleLeft);
#undef wrapperReadFromLast
        }
    }

    for (auto &wrapper: wrappersVec) {
        if (wrapper.active && wrapper.isLongLatency &&
                wrapper.hasPendingInst && !wrapper.isLSU) {

        } else if (wrapper.isPipelined && wrapper.active) {
            DPRINTF(FUPipe, "@ start, wrapper(%i, %i) is pipelined, size:%u, will pop\n",
                    wrapperID, wrapper.op, wrapper.pipelineQueue.size());
            assert(wrapper.pipelineQueue.size() == wrapper.latency);
            // pop to prepare for incoming pointers
            if (wrapper.pipelineQueue.front().valid) {
                DPRINTF(FUPipe, "Popping a valid inst\n");
                wrapper.pipeInstCount -= 1;
            }
            wrapper.pipelineQueue.pop_front();
            DPRINTF(FUPipe, "@ start, wrapper(%i, %i) size:%lu after pop\n",
                    wrapperID, wrapper.op, wrapper.pipelineQueue.size());
        }
    }
}

template<class Impl>
void FUWrapper<Impl>::endCycle() {
    for (auto &wrapper: wrappersVec) {
        // maintain pipelined bitset
        if (wrapper.checkActive()) {
            DPRINTF(FUW, "wrapper(%i, %i) switch to active: %i\n",
                    wrapperID, wrapper.op, wrapper.active);
        }
        if (!wrapper.active) {
            continue;
        }
        if (wrapper.isPipelined) {
            DPRINTF(FUPipe, "@ end, wrapper(%i, %i) written: %i, size: %lu\n",
                    wrapperID, wrapper.op, wrapper.writtenThisCycle,
                    wrapper.pipelineQueue.size());
            if (!wrapper.writtenThisCycle) {
                DPRINTF(FUPipe, "@ end, wrapper(%i, %i) is pipelined, size:%u, will push\n",
                        wrapperID, wrapper.op, wrapper.pipelineQueue.size());
                wrapper.pipelineQueue.push_back({false, {inv, inv}, 0});
            } else {
                DPRINTF(FUPipe, "@ end, wrapper(%i, %i) has been written, size:%u, will not push\n",
                        wrapperID, wrapper.op, wrapper.pipelineQueue.size());
            }
        }
    }
}

template<class Impl>
void FUWrapper<Impl>::init(const Params *p, unsigned gid, unsigned bank)
{
    numFU = 0;
    active = false;
    groupID = gid;

    wrapperID = bank;

    std::ostringstream s;
    s << "DQGroup" << groupID << ".fuWrapper" << bank;
    _name = s.str();

    //  Iterate through the list of FUDescData structures
    //
    const std::vector<FUDesc *> &paramList = p->FUList;
    FUDDiterator i = paramList.begin() + bank;

    //
    //  Don't bother with this if we're not going to create any FU's
    //
    if ((*i)->number) {
        assert((*i)->number == 1);
        //
        //  Create the FuncUnit object from this structure
        //   - add the capabilities listed in the FU's operation
        //     description
        //
        //  We create the first unit, then duplicate it as needed
        //
        FuncUnit *fu = new FuncUnit;

        wrappersVec.resize((*i)->opDescList.size());

        OPDDiterator j = (*i)->opDescList.begin();
        OPDDiterator end = (*i)->opDescList.end();
        unsigned index = 0;
        for (; j != end; ++j) {
            // indicate that this pool has this capability
            capabilityList.set((*j)->opClass);
            opLat[(*j)->opClass] = static_cast<unsigned int>((*j)->opLat);

            // Add each of the FU's that will have this capability to the
            // appropriate queue.
            wrappers[(*j)->opClass] = &wrappersVec[index];
            wrappers[(*j)->opClass]->init(
                    (*j)->pipelined && (!((*j)->opLat == 1)),
                    (*j)->opLat == 1,
                    (*j)->opLat > 1,
                    static_cast<unsigned int>((*j)->opLat),
                    20,
                    (*j)->opClass
            ); // todo: fix
            index++;
            // indicate that this FU has the capability
            fu->addCapability((*j)->opClass, (*j)->opLat, (*j)->pipelined);

            // todo: check here
            if (!(*j)->pipelined)
                canPipelined[(*j)->opClass] = false;
        }

        numFU++;

        //  Add the appropriate number of copies of this FU to the list
        std::ostringstream s;
        s << (*i)->name() + "(" << bank << ",0)";
        fu->name = s.str();
    }
}

template<class Impl>
FUWrapper<Impl>::FUWrapper()
{
    inv.valid = false;
}

template<class Impl>
void FUWrapper<Impl>::fillMyBitMap(std::vector<std::vector<bool>> &v,
        unsigned bank)
{
    for (unsigned i = 0; i < Num_OpClasses; i++) {
        v[i][bank] = capabilityList[i];
    }
}

template<class Impl>
void FUWrapper<Impl>::fillLatTable(std::unordered_map<OpClass, unsigned> &v)
{
    for (unsigned i = 0; i < Num_OpClasses; i++) {
        auto opc = static_cast<OpClass>(i);
        if (capabilityList[i] && !v.count(opc)) {
            v[opc] = opLat[i];
        }
    }
}

template<class Impl>
void FUWrapper<Impl>::executeInsts()
{
    auto it = execQueue.begin(), e = execQueue.end();
    while (it != e) {
        DPRINTF(FUW, "toExec is valid, execute now!\n");
        assert(insts.count(it->seq));
        assert(insts[it->seq]);

        auto &inst = insts[it->seq];
        DPRINTF(FUW, "Executing inst[%lu]\n", inst->seqNum);
        exec->executeInst(inst);

        if (inst->numDestRegs() && inst->isExecuted() &&
                !(inst->isLoad() || inst->isStoreConditional())) {

            if (it->wbPos != wbQueue.end()) {
                (*it->wbPos)[DestPtr].hasVal = true;
                (*it->wbPos)[DestPtr].val = inst->getDestValue();
                DPRINTF(FUW, "Setting dest ptr value to %lu\n",
                        inst->getDestValue().i);
            }
        }

        insts.erase(it->seq);
        it = execQueue.erase(it);
    }

    endCycle();
}

template<class Impl>
void FUWrapper<Impl>::setLSQ(typename Impl::CPUPol::LSQ *lsq)
{
    ldstQueue = lsq;
}

template<class Impl>
void FUWrapper<Impl>::setDQ(DQ *_dq)
{
    dq = _dq;
}

template<class Impl>
void FUWrapper<Impl>::setExec(Exec *_exec)
{
    exec = _exec;
}

template<class Impl>
void FUWrapper<Impl>::squash(InstSeqNum squash_seq)
{
    for (auto &fu: wrappersVec) {
        if (fu.isLongLatency && fu.hasPendingInst &&
                fu.seq > squash_seq) {
//            DPRINTF(FUW, "Squashing LL inst[%llu] in w(%i, %i)\n",
//                    fu.seq, wrapperID, pair.first);
            fu.toNextCycle->hasPendingInst = false;
            fu.toNextCycle->cycleLeft = 0;
            fu.toNextCycle->seq = 0;

            insts.erase(fu.seq);
        }

        if (fu.isPipelined) {
            unsigned cycle_left = 1;
            // note that head has already been read in this tick
            for (auto &element: fu.pipelineQueue) {
                if (cycle_left > 1 && element.valid && element.seq > squash_seq) {
//                    DPRINTF(FUW, "Squashing pipe inst[%llu] in w(%i, %i)\n",
//                            element.seq, wrapperID, pair.first);
                    element.pointer[SrcPtr].valid = false;
                    element.pointer[DestPtr].valid = false;
                    element.valid = false;
                    insts.erase(element.seq);

                }
                cycle_left++;
            }
        }

        // no need to handle single-cycle instructions, right?
    }

    auto it = wbQueue.begin(), e = wbQueue.end();
    while (it != e) {
        if (it->seq > squash_seq) {
            it = wbQueue.erase(it);
        } else {
            it++;
        }
    }
}

void
SingleFUWrapper::init(
        bool pipe, bool single_cycle, bool long_lat,
        unsigned _latency, unsigned max_pipe_lat, unsigned _op)
{
    WKPointer inv;
    inv.valid = false;

    seq = 0;
    isPipelined = pipe;
    isSingleCycle = single_cycle;
    isLongLatency = long_lat;
    latency = _latency;
    MaxPipeLatency = max_pipe_lat;
    for (unsigned i = 0; i < latency; i++) {
        pipelineQueue.push_back({false, {inv, inv}, 0});
    }
    toNextCycle = &timeReg;
    fromLastCycle = &timeReg;
    active = isSingleCycle;
    pipeInstCount = 0;
    op = _op;
}

bool SingleFUWrapper::checkActive()
{
//    if (!isSingleCycle) {
//        if (isLongLatency) {
//            active = hasPendingInst;
//        }
//        if (isPipelined) {
//            active = pipeInstCount > 0;
//        }
//    }

    // active = !isSingleCycle &&
    //          ((isLongLatency && hasPendingInst) || (isPipelined && pipeInstCount > 0));

    auto old = active;
    unsigned sub_cond_1 = ((unsigned) isLongLatency) & ~((unsigned) isPipelined);
    sub_cond_1 &= (unsigned) hasPendingInst;
    unsigned sub_cond_2 = ((unsigned) isPipelined) & (unsigned) (pipeInstCount > 0);
    active = (bool) ((sub_cond_1 | sub_cond_2) & 0x1);
    return active != old;
}

}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::FUWrapper<FFCPUImpl>;
