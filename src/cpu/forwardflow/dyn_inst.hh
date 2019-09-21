/*
 * Copyright (c) 2010, 2016 ARM Limited
 * Copyright (c) 2013 Advanced Micro Devices, Inc.
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

#ifndef __CPU_FF_DYN_INST_HH__
#define __CPU_FF_DYN_INST_HH__

#include <array>

#include "arch/isa_traits.hh"
#include "config/the_isa.hh"
#include "cpu/fanout_pred_features.hh"
#include "cpu/ff_base_dyn_inst.hh"
#include "cpu/forwardflow/cpu.hh"
#include "cpu/forwardflow/isa_specific.hh"
#include "cpu/inst_seq.hh"
#include "cpu/reg_class.hh"
#include "debug/FFExec.hh"
#include "debug/FFReg.hh"

class Packet;

namespace FF{

template <class Impl>
class BaseO3DynInst : public BaseDynInst<Impl>
{
  public:
    /** Typedef for the CPU. */
    typedef typename Impl::O3CPU O3CPU;

    /** Binary machine instruction type. */
    typedef TheISA::MachInst MachInst;
    /** Register types. */
    typedef TheISA::IntReg   IntReg;
    typedef TheISA::FloatReg FloatReg;
    typedef TheISA::FloatRegBits FloatRegBits;
    typedef TheISA::CCReg   CCReg;
    using VecRegContainer = TheISA::VecRegContainer;
    using VecElem = TheISA::VecElem;
    static constexpr auto NumVecElemPerVecReg = TheISA::NumVecElemPerVecReg;

    /** Misc register type. */
    typedef TheISA::MiscReg  MiscReg;

    enum {
        MaxInstSrcRegs = TheISA::MaxInstSrcRegs,        //< Max source regs
        MaxInstDestRegs = TheISA::MaxInstDestRegs       //< Max dest regs
    };

  public:
    /** BaseDynInst constructor given a binary instruction. */
    BaseO3DynInst(const StaticInstPtr &staticInst, const StaticInstPtr
            &macroop, TheISA::PCState pc, TheISA::PCState predPC,
            InstSeqNum seq_num, O3CPU *cpu);

    /** BaseDynInst constructor given a static inst pointer. */
    BaseO3DynInst(const StaticInstPtr &_staticInst,
                  const StaticInstPtr &_macroop);

    ~BaseO3DynInst();

    /** Executes the instruction.*/
    Fault execute();

    /** Initiates the access.  Only valid for memory operations. */
    Fault initiateAcc();

    /** Completes the access.  Only valid for memory operations. */
    Fault completeAcc(PacketPtr pkt);

  private:
    /** Initializes variables. */
    void initVars();

  protected:
    /** Explicitation of dependent names. */
    using BaseDynInst<Impl>::cpu;
    using BaseDynInst<Impl>::_srcInstPointer;
    using BaseDynInst<Impl>::_destRegIdx;

    /** Values to be written to the destination misc. registers. */
    std::array<MiscReg, TheISA::MaxMiscDestRegs> _destMiscRegVal;

    /** Indexes of the destination misc. registers. They are needed to defer
     * the write accesses to the misc. registers until the commit stage, when
     * the instruction is out of its speculative state.
     */
    std::array<short, TheISA::MaxMiscDestRegs> _destMiscRegIdx;

    /** Number of destination misc. registers. */
    uint8_t _numDestMiscRegs;


  public:
#if TRACING_ON
    /** Tick records used for the pipeline activity viewer. */
    Tick fetchTick;      // instruction fetch is completed.
    int32_t decodeTick;  // instruction enters decode phase
    int32_t renameTick;  // instruction enters rename phase
    int32_t dispatchTick;
    int32_t issueTick;
    int32_t commitTick;
    int32_t storeTick;
#endif
    int32_t completeTick;

    /** Reads a misc. register, including any side-effects the read
     * might have as defined by the architecture.
     */
    MiscReg readMiscReg(int misc_reg)
    {
        return this->cpu->readMiscReg(misc_reg, this->threadNumber);
    }

    /** Sets a misc. register, including any side-effects the write
     * might have as defined by the architecture.
     */
    void setMiscReg(int misc_reg, const MiscReg &val)
    {
        /** Writes to misc. registers are recorded and deferred until the
         * commit stage, when updateMiscRegs() is called. First, check if
         * the misc reg has been written before and update its value to be
         * committed instead of making a new entry. If not, make a new
         * entry and record the write.
         */
        for (int idx = 0; idx < _numDestMiscRegs; idx++) {
            if (_destMiscRegIdx[idx] == misc_reg) {
               _destMiscRegVal[idx] = val;
               return;
            }
        }

        assert(_numDestMiscRegs < TheISA::MaxMiscDestRegs);
        _destMiscRegIdx[_numDestMiscRegs] = misc_reg;
        _destMiscRegVal[_numDestMiscRegs] = val;
        _numDestMiscRegs++;
    }

    /** Reads a misc. register, including any side-effects the read
     * might have as defined by the architecture.
     */
    TheISA::MiscReg readMiscRegOperand(const StaticInst *si, int idx)
    {
        const RegId& reg = si->srcRegIdx(idx);
        assert(reg.isMiscReg());
        return this->cpu->readMiscReg(reg.index(), this->threadNumber);
    }

    /** Sets a misc. register, including any side-effects the write
     * might have as defined by the architecture.
     */
    void setMiscRegOperand(const StaticInst *si, int idx,
                                     const MiscReg &val)
    {
        const RegId& reg = si->destRegIdx(idx);
        assert(reg.isMiscReg());
        setMiscReg(reg.index(), val);
    }

    /** Called at the commit stage to update the misc. registers. */
    void updateMiscRegs()
    {
        // @todo: Pretty convoluted way to avoid squashing from happening when
        // using the TC during an instruction's execution (specifically for
        // instructions that have side-effects that use the TC).  Fix this.
        // See cpu/forwardflow/dyn_inst_impl.hh.
        bool no_squash_from_TC = this->thread->noSquashFromTC;
        this->thread->noSquashFromTC = true;

        for (int i = 0; i < _numDestMiscRegs; i++)
            this->cpu->setMiscReg(
                _destMiscRegIdx[i], _destMiscRegVal[i], this->threadNumber);

        this->thread->noSquashFromTC = no_squash_from_TC;
    }

    /** Calls hardware return from error interrupt. */
    Fault hwrei();
    /** Traps to handle specified fault. */
    void trap(const Fault &fault);
    bool simPalCheck(int palFunc);

    /** Emulates a syscall. */
    void syscall(int64_t callnum, Fault *fault);

  public:

    // The register accessor methods provide the index of the
    // instruction's operand (e.g., 0 or 1), not the architectural
    // register index, to simplify the implementation of register
    // renaming.  We find the architectural register index by indexing
    // into the instruction's own operand index table.  Note that a
    // raw pointer to the StaticInst is provided instead of a
    // ref-counted StaticInstPtr to redice overhead.  This is fine as
    // long as these methods don't copy the pointer into any long-term
    // storage (which is pretty hard to imagine they would have reason
    // to do).

    IntReg readIntRegOperand(const StaticInst *si, int idx);

    FloatReg readFloatRegOperand(const StaticInst *si, int idx);

    FloatRegBits readFloatRegOperandBits(const StaticInst *si, int idx);

    const VecRegContainer&
    readVecRegOperand(const StaticInst *si, int idx) const
    {
        panic("No Vec support in RV-ForwardFlow");
    }

    /**
     * Read destination vector register operand for modification.
     */
    VecRegContainer&
    getWritableVecRegOperand(const StaticInst *si, int idx)
    {
        panic("No Vec support in RV-ForwardFlow");
    }

    /** Vector Register Lane Interfaces. */
    /** @{ */
    /** Reads source vector 8bit operand. */
    ConstVecLane8
    readVec8BitLaneOperand(const StaticInst *si, int idx) const
    {
        panic("No Vec support in RV-ForwardFlow");
    }

    /** Reads source vector 16bit operand. */
    ConstVecLane16
    readVec16BitLaneOperand(const StaticInst *si, int idx) const
    {
        panic("No Vec support in RV-ForwardFlow");
    }

    /** Reads source vector 32bit operand. */
    ConstVecLane32
    readVec32BitLaneOperand(const StaticInst *si, int idx) const
    {
        panic("No Vec support in RV-ForwardFlow");
    }

    /** Reads source vector 64bit operand. */
    ConstVecLane64
    readVec64BitLaneOperand(const StaticInst *si, int idx) const
    {
        panic("No Vec support in RV-ForwardFlow");
    }

    /** Write a lane of the destination vector operand. */
    template <typename LD>
    void
    setVecLaneOperandT(const StaticInst *si, int idx, const LD& val)
    {
        panic("No Vec support in RV-ForwardFlow");
    }
    virtual void
    setVecLaneOperand(const StaticInst *si, int idx,
            const LaneData<LaneSize::Byte>& val)
    {
        panic("No Vec support in RV-ForwardFlow");
    }
    virtual void
    setVecLaneOperand(const StaticInst *si, int idx,
            const LaneData<LaneSize::TwoByte>& val)
    {
        panic("No Vec support in RV-ForwardFlow");
    }
    virtual void
    setVecLaneOperand(const StaticInst *si, int idx,
            const LaneData<LaneSize::FourByte>& val)
    {
        panic("No Vec support in RV-ForwardFlow");
    }
    virtual void
    setVecLaneOperand(const StaticInst *si, int idx,
            const LaneData<LaneSize::EightByte>& val)
    {
        panic("No Vec support in RV-ForwardFlow");
    }
    /** @} */

    VecElem readVecElemOperand(const StaticInst *si, int idx) const
    {
        panic("No Vec support in RV-ForwardFlow");
    }

    CCReg readCCRegOperand(const StaticInst *si, int idx)
    {
        panic("No CC support in RV-ForwardFlow");
    }

    /** @todo: Make results into arrays so they can handle multiple dest
     *  registers.
     */
    void setIntRegOperand(const StaticInst *si, int idx, IntReg val)
    {
        DPRINTF(FFExec, "Inst [%llu] %s setting dest int reg(%i) to %llu\n",
                this->seqNum, si->disassemble(this->instAddr()), idx, val);

        // this->cpu->setIntReg(this->_destRegIdx[idx], val);
        // let inst carry their results with themselves
        destValue.i = val;
        BaseDynInst<Impl>::setIntRegOperand(si, idx, val);
    }

    void setFloatRegOperand(const StaticInst *si, int idx, FloatReg val)
    {
        // this->cpu->setFloatReg(this->_destRegIdx[idx], val);
        DPRINTF(FFExec, "Inst [%llu] %s setting dest float reg(%i) to %f\n",
                this->seqNum, si->disassemble(this->instAddr()), idx, val);
        destValue.f = val;
        BaseDynInst<Impl>::setFloatRegOperand(si, idx, val);
    }

    void setFloatRegOperandBits(const StaticInst *si, int idx,
                                FloatRegBits val)
    {
        // this->cpu->setFloatRegBits(this->_destRegIdx[idx], val);
        DPRINTF(FFExec, "Inst [%llu] %s setting dest float reg(%i) to %llu\n",
                this->seqNum, si->disassemble(this->instAddr()), idx, val);
        destValue.i = val;
        BaseDynInst<Impl>::setFloatRegOperandBits(si, idx, val);
    }

    void
    setVecRegOperand(const StaticInst *si, int idx,
                     const VecRegContainer& val)
    {
        panic("No Vec support in RV-ForwardFlow");
    }

    void setVecElemOperand(const StaticInst *si, int idx,
                           const VecElem val)
    {
        panic("No Vec support in RV-ForwardFlow");
    }

    void setCCRegOperand(const StaticInst *si, int idx, CCReg val)
    {
        panic("No CC support in RV-ForwardFlow");
    }

#if THE_ISA == MIPS_ISA
    MiscReg readRegOtherThread(const RegId& misc_reg, ThreadID tid)
    {
        panic("MIPS MT not defined for O3 CPU.\n");
        return 0;
    }

    void setRegOtherThread(const RegId& misc_reg, MiscReg val, ThreadID tid)
    {
        panic("MIPS MT not defined for O3 CPU.\n");
    }
#endif

public:
    std::array<DQPointer, 4> pointers;

    std::array<bool, 4> hasOp;
    std::array<bool, 4> opReady;
    bool opFulfilled(unsigned);

    std::array<std::list<int>, 4> indirectRegIndices;

    std::array<RegId, 4> indirectRegIds;

    std::array<unsigned, 4> identicalTo;

    std::array<bool, 4> srcTakenWithInst;

    bool hasMemDep;
    bool memDepReady;
    bool memOpFulfilled();

    bool hasMiscDep;
    bool miscDepReady;
    bool miscOpFulfilled();

    bool hasOrderDep;
    bool orderDepReady;
    bool orderFulfilled();

    DQPointer dqPosition;

    FFRegValue getDestValue();

    SingleFUWrapper *sfuWrapper;

    bool fuGranted;

    bool destReforward;

    unsigned numBusyOps();

    void setSrcValue(unsigned idx, FFRegValue val);

    unsigned numChildren;

    bool predLargeFanout;

    unsigned predFanout;

    int predReshapeProfit;

    unsigned numForwardRest;

    unsigned numFollowingFw;

    bool forwarded;

    uint64_t ghr;

    FPFeatures fpFeat;

    DQPointer ancestorPointer;

    int reshapeContrib;

    int gainFromReshape;

    int nonCriticalFw;

    int negativeContrib;

    unsigned fwLevel;

    unsigned wkDelayedCycle{};
private:
    FFRegValue destValue;

    std::array<FFRegValue, 3> srcValues;

};

}

#endif // __CPU_FF_ALPHA_DYN_INST_HH__

