/*
 * Copyright (c) 2010-2011 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2004-2006 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Kevin Lim
 */

#ifndef __CPU_FF_DYN_INST_IMPL_HH__
#define __CPU_FF_DYN_INST_IMPL_HH__

#include "cpu/forwardflow/dyn_inst.hh"
#include "debug/O3PipeView.hh"
#include "dyn_inst.hh"
#include "sim/full_system.hh"

namespace FF{


template <class Impl>
BaseO3DynInst<Impl>::BaseO3DynInst(const StaticInstPtr &staticInst,
                                   const StaticInstPtr &macroop,
                                   TheISA::PCState pc, TheISA::PCState predPC,
                                   InstSeqNum seq_num, O3CPU *cpu)
    : BaseDynInst<Impl>(staticInst, macroop, pc, predPC, seq_num, cpu)
{
    initVars();
}

template <class Impl>
BaseO3DynInst<Impl>::BaseO3DynInst(const StaticInstPtr &_staticInst,
                                   const StaticInstPtr &_macroop)
    : BaseDynInst<Impl>(_staticInst, _macroop)
{
    initVars();
}

template <class Impl>BaseO3DynInst<Impl>::~BaseO3DynInst()
{
#if TRACING_ON
    if (DTRACE(O3PipeView)) {
        Tick fetch = this->fetchTick;
        // fetchTick can be -1 if the instruction fetched outside the trace window.
        if (fetch != -1) {
            Tick val;
            // Print info needed by the pipeline activity viewer.
            DPRINTFR(O3PipeView, "O3PipeView:fetch:%llu:0x%08llx:%d:%llu:%s\n",
                     fetch,
                     this->instAddr(),
                     this->microPC(),
                     this->seqNum,
                     this->staticInst->disassemble(this->instAddr()));

            val = (this->decodeTick == -1) ? 0 : fetch + this->decodeTick;
            DPRINTFR(O3PipeView, "O3PipeView:decode:%llu\n", val);
            val = (this->renameTick == -1) ? 0 : fetch + this->renameTick;
            DPRINTFR(O3PipeView, "O3PipeView:rename:%llu\n", val);
            val = (this->dispatchTick == -1) ? 0 : fetch + this->dispatchTick;
            DPRINTFR(O3PipeView, "O3PipeView:dispatch:%llu\n", val);
            val = (this->issueTick == -1) ? 0 : fetch + this->issueTick;
            DPRINTFR(O3PipeView, "O3PipeView:issue:%llu\n", val);
            val = (this->completeTick == -1) ? 0 : fetch + this->completeTick;
            DPRINTFR(O3PipeView, "O3PipeView:complete:%llu\n", val);
            val = (this->commitTick == -1) ? 0 : fetch + this->commitTick;

            Tick valS = (this->storeTick == -1) ? 0 : fetch + this->storeTick;
            DPRINTFR(O3PipeView, "O3PipeView:retire:%llu:store:%llu\n", val, valS);
        }
    }
#endif
};


template <class Impl>
void
BaseO3DynInst<Impl>::initVars()
{
    this->_readySrcRegIdx.reset();

    _numDestMiscRegs = 0;

#if TRACING_ON
    // Value -1 indicates that particular phase
    // hasn't happened (yet).
    fetchTick = -1;
    decodeTick = -1;
    renameTick = -1;
    dispatchTick = -1;
    issueTick = -1;
    completeTick = -1;
    commitTick = -1;
    storeTick = -1;
#endif

    for (auto &ptr: pointers) {
        ptr.valid = false;
    }
    dqPosition.valid = false;
    ancestorPointer.valid = false;

    std::fill(hasOp.begin(), hasOp.end(), false);
    std::fill(opReady.begin(), opReady.end(), false);
    std::fill(identicalTo.begin(), identicalTo.end(), 0);
    fuGranted = false;
    inReadyQueue = false;
    std::fill(srcTakenWithInst.begin(), srcTakenWithInst.end(), false);

    hasMemDep = false;
    hasMiscDep = false;
    hasOrderDep = false;

    miscDepReady = false;
    memDepReady = false;
    orderDepReady = false;

    sfuWrapper = nullptr;

    destReforward = false;

    numChildren = 0;

    predLargeFanout = false;
    predFanout = 0;

    numForwardRest = 0;
    numFollowingFw = 0;

    forwarded = false;

    reshapeContrib = 0;

    gainFromReshape = 0;

    nonCriticalFw = 0;

    negativeContrib = 0;

    receivedDest = false;

    readyTick = 0;

    if (this->isLoad()) {
        memPredHistory = std::make_unique<MemPredHistory>();
    }
}

template <class Impl>
Fault
BaseO3DynInst<Impl>::execute()
{
    // @todo: Pretty convoluted way to avoid squashing from happening
    // when using the TC during an instruction's execution
    // (specifically for instructions that have side-effects that use
    // the TC).  Fix this.
    bool no_squash_from_TC = this->thread->noSquashFromTC;
    this->thread->noSquashFromTC = true;

    this->fault = this->staticInst->execute(this, this->traceData);

    this->thread->noSquashFromTC = no_squash_from_TC;

    return this->fault;
}

template <class Impl>
Fault
BaseO3DynInst<Impl>::initiateAcc()
{
    // @todo: Pretty convoluted way to avoid squashing from happening
    // when using the TC during an instruction's execution
    // (specifically for instructions that have side-effects that use
    // the TC).  Fix this.
    bool no_squash_from_TC = this->thread->noSquashFromTC;
    this->thread->noSquashFromTC = true;

    this->fault = this->staticInst->initiateAcc(this, this->traceData);

    this->thread->noSquashFromTC = no_squash_from_TC;

    return this->fault;
}

template <class Impl>
Fault
BaseO3DynInst<Impl>::completeAcc(PacketPtr pkt)
{
    // @todo: Pretty convoluted way to avoid squashing from happening
    // when using the TC during an instruction's execution
    // (specifically for instructions that have side-effects that use
    // the TC).  Fix this.
    bool no_squash_from_TC = this->thread->noSquashFromTC;
    this->thread->noSquashFromTC = true;

    if (this->cpu->checker) {
        if (this->isStoreConditional()) {
            this->reqToVerify->setExtraData(pkt->req->getExtraData());
        }
    }

    this->fault = this->staticInst->completeAcc(pkt, this, this->traceData);

    this->thread->noSquashFromTC = no_squash_from_TC;

    return this->fault;
}

template <class Impl>
Fault
BaseO3DynInst<Impl>::hwrei()
{
    // FIXME: XXX check for interrupts? XXX
    return NoFault;
}

template <class Impl>
void
BaseO3DynInst<Impl>::trap(const Fault &fault)
{
    this->cpu->trap(fault, this->threadNumber, this->staticInst);
}

template <class Impl>
bool
BaseO3DynInst<Impl>::simPalCheck(int palFunc)
{
    panic("simPalCheck called, but PAL only exists in Alpha!\n");
}

template<class Impl>
bool BaseO3DynInst<Impl>::opFulfilled(unsigned i)
{
    return !hasOp[i] || opReady[i];
}

template<class Impl>
bool BaseO3DynInst<Impl>::memOpFulfilled()
{
    return !hasMemDep || memDepReady;
}

template<class Impl>
bool BaseO3DynInst<Impl>::orderFulfilled()
{
    return !hasOrderDep || orderDepReady;
}

template<class Impl>
bool BaseO3DynInst<Impl>::miscOpFulfilled()
{
    return !hasMiscDep || miscDepReady;
}

template<class Impl>
FFRegValue BaseO3DynInst<Impl>::getDestValue()
{
    if (this->isExecuted()) {
        return destValue;
    } else if (this->isForwarder()) {
        return srcValues[0];
    }
    panic("requesting dest value of inst [%llu] when it has not been executed", this->seqNum);
}

template<class Impl>
unsigned BaseO3DynInst<Impl>::numBusyOps()
{
    unsigned busy = 0;
    for (unsigned op = 1; op <= 3; op++) {
        busy += !opFulfilled(op);  // a little tricky..
    }
    busy += !miscOpFulfilled();
    busy += !orderFulfilled();
    return busy;
}

template<class Impl>
void BaseO3DynInst<Impl>::setSrcValue(unsigned idx, FFRegValue val)
{
    srcValues[idx] = val;
}

template<class Impl>
FFRegValue BaseO3DynInst<Impl>::getOpValue(unsigned op)
{
    return srcValues[indirectRegIndices.at(op).front()];
}

template<class Impl>
RegVal
BaseO3DynInst<Impl>::readFloatRegOperandBits(const StaticInst *si, int idx)
{
    assert(idx < 3);
    // assert(opReady[idx + 1] || srcTakenWithInst[idx + 1]);
    DPRINTFR(FFExec, "Reading float (bits) op %i: %llu\n", idx, srcValues[idx].i);
    return srcValues[idx].i;
}

template<class Impl>
RegVal
BaseO3DynInst<Impl>::readIntRegOperand(const StaticInst *si, int idx)
{
    assert(idx < 3);
    // assert(opReady[idx + 1] || srcTakenWithInst[idx + 1]);
    DPRINTFR(FFExec, "Reading int op %i: %llu\n", idx, srcValues[idx].i);
    return srcValues[idx].i;
}

template<class Impl>
bool
BaseO3DynInst<Impl>::forwardOpReady()
{
    assert(forwardOp <= Impl::MaxOps);
    return opReady[forwardOp];
}

template<class Impl>
unsigned
BaseO3DynInst<Impl>::findSpareSourceOp()
{
    for (unsigned i = 1; i < 4; i++) {
        if (!hasOp[i]) {
            return i;
        }
    }
    return 0;
}

template<class Impl>
BasePointer
BaseO3DynInst<Impl>::findSpareSourcePointer()
{
    auto pointer = dqPosition;
    pointer.op = findSpareSourceOp();
    return pointer;
}

template<class Impl>
uint64_t
BaseO3DynInst<Impl>::readStoreValue()
{
    assert(isNormalStore());
    uint64_t v;
    if (opReady[2]) {// 2 is src reg of store
        v = readIntRegOperand(nullptr, 1); // 1 is src reg of store
    } else {
        assert(opReady[1]);
        assert(indirectRegIndices.at(1).size() == 2); // identical register
        v = readIntRegOperand(nullptr, 0); // 0 is both base and src reg of store
    }
    assert(0 < this->memSize && this->memSize <= 8);
    switch(this->memSize) {
        case 1: {
                    uint8_t narrow = v;
                    v = narrow;
                    break;
                }
        case 2: {
                    uint16_t narrow = v;
                    v = narrow;
                    break;
                }
        case 4: {
                    uint32_t narrow = v;
                    v = narrow;
                    break;
                }
        case 8: {
                    break;
                }
        default:
            panic("Unexpected memSize: %u\n", this->memSize);
    }
    return v;
}

template<class Impl>
bool BaseO3DynInst<Impl>::storeValueBecomeReadyOn(unsigned int op)
{
    assert(isNormalStore());
    if (hasOp[2] && op == 2) {
        return true;
    } else if (indirectRegIndices.at(1).size() == 2 && hasOp[1] && op == 1) {
        return true;
    }
    return false;
}

template<class Impl>
bool BaseO3DynInst<Impl>::storeValueReady()
{
    assert(isNormalStore());
    if (opReady[2]) {
        return true;
    } else if (indirectRegIndices.at(1).size() == 2 && opReady[1]) {
        // identical register
        return true;
    }
    return false;
}

}

#endif//__CPU_FF_DYN_INST_IMPL_HH__
