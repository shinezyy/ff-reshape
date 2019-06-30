//
// Created by zyy on 19-6-11.
//

#include "cpu/forwardflow/arch_state.hh"

#include "cpu/thread_context.hh"
#include "debug/Rename.hh"
#include "params/DerivFFCPU.hh"

namespace FF
{

template<class Impl>
std::list<PointerPair> ArchState<Impl>::recordAndUpdateMap(DynInstPtr &inst)
{
    ThreadContext *tc = inst->tcBase();
    auto &map = renameMap;
    unsigned num_src_regs = inst->numSrcRegs();

    // Get the architectual register numbers from the source and
    // operands, and redirect them to the right physical register.
    std::list<PointerPair> pairs;

    PointerPair invalid_pair{};
    invalid_pair.payload.valid = false;
    invalid_pair.dest.valid = false;

    for (int src_idx = 0; src_idx < num_src_regs; src_idx++) {
        const RegId& src_reg = inst->srcRegIdx(src_idx);
        DQPointer renamed_reg = map[tc->flattenRegId(src_reg)];

        DPRINTF(Rename, "Looking up %s arch reg %i"
                ", got pointer [%i,%i]\n",
                src_reg.className(), src_reg.index(),
                renamed_reg.bank, renamed_reg.index);

        inst->renameSrcReg(src_idx, renamed_reg);

        // See if the register is ready or not.
        if (scoreboard[src_reg]) {
            inst->markSrcRegReady(src_idx);
            pairs.push_back(invalid_pair);
        } else {
            assert(renameMap.count(src_reg));
            pairs.push_back({renameMap[src_reg], inst->dqPosition});
        }
    }

    const RegId& dest_reg = inst->destRegIdx(0);
    scoreboard[dest_reg] = false;
    renameMap[dest_reg] = inst->dqPosition;

    return pairs;
}

template<class Impl>
void ArchState<Impl>::setIntReg(int reg_idx, uint64_t val)
{
    intArchRF[reg_idx].i = val;
}

template<class Impl>
void ArchState<Impl>::setFloatReg(int reg_idx, double val)
{
    floatArchRF[reg_idx].f = val;
}

template<class Impl>
void ArchState<Impl>::setFloatRegBits(int reg_idx, uint64_t val)
{
    floatArchRF[reg_idx].i = val;
}

template<class Impl>
uint64_t ArchState<Impl>::readIntReg(int reg_idx)
{
    assert(intArchRF.count(reg_idx));
    return intArchRF[reg_idx].i;
}

template<class Impl>
double ArchState<Impl>::readFloatReg(int reg_idx)
{
    assert(floatArchRF.count(reg_idx));
    return floatArchRF[reg_idx].f;
}

template<class Impl>
uint64_t ArchState<Impl>::readFloatRegBits(int reg_idx)
{
    assert(floatArchRF.count(reg_idx));
    return floatArchRF[reg_idx].i;
}

template<class Impl>
bool ArchState<Impl>::makeCheckPoint(DynInstPtr &inst)
{
    assert(!cpts.count(inst->seqNum));
    cpts[inst->seqNum] = {renameMap, scoreboard};
    return true;
}

template<class Impl>
void ArchState<Impl>::recoverCPT(DynInstPtr &inst)
{
    recoverCPT(inst->seqNum);
}

template<class Impl>
void ArchState<Impl>::recoverCPT(InstSeqNum &num)
{
    assert(cpts.count(num));
    Checkpoint &cpt = cpts[num];
    renameMap = cpt.renameMap;
    scoreboard = cpt.scoreboard;

    auto it = cpts.begin();
    while (it != cpts.end()) {
        if (it->first >= num) {
            it = cpts.erase(it);
        } else {
            it++;
        }
    }
}

template<class Impl>
ArchState<Impl>::ArchState(DerivFFCPUParams *params)
: MaxCheckpoints(params->MaxCheckpoints)
{
}

template<class Impl>
bool ArchState<Impl>::checkpointsFull()
{
    return cpts.size() >= MaxCheckpoints;
}

template<class Impl>
bool ArchState<Impl>::commitInst(DynInstPtr &inst)
{
    InstSeqNum num = inst->seqNum;

    // clear older checkpoints
    auto it = cpts.begin();
    while (it != cpts.end()) {
        if (it->first < num) {
            it = cpts.erase(it);
        } else {
            it++;
        }
    }
    FFRegValue val = inst->getDestValue();
    if (inst->isInteger()) {
        intArchRF[inst->staticInst->destRegIdx(0).index()] = val;
    } else if (inst->isInteger()) {
        floatArchRF[inst->staticInst->destRegIdx(0).index()] = val;
    } else {
        panic("not ready for other instructions!");
    }
    return true;
}

}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::ArchState<FFCPUImpl>;

