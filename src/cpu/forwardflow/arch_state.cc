//
// Created by zyy on 19-6-11.
//

#include "cpu/forwardflow/arch_state.hh"

#include "base/trace.hh"
#include "cpu/thread_context.hh"
#include "debug/Rename.hh"
#include "params/DerivFFCPU.hh"

namespace FF
{

using namespace std;

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

        auto sb_index = make_pair(src_reg.classValue(), src_reg.index());

        if (scoreboard.count(sb_index) && scoreboard[sb_index]) {
            if (inst->isFloating()) {
                inst->srcValues[src_idx] = floatArchRF[src_reg.index()];
            } else if (inst->isInteger()) {
                inst->srcValues[src_idx] = intArchRF[src_reg.index()];
            } else {
                panic("Unexpected reg type\n");
            }
            inst->srcTakenWithInst[src_idx] = true;

            inst->markSrcRegReady(src_idx);
            pairs.push_back(invalid_pair);
        } else {
            assert(renameMap.count(src_reg));
            pairs.push_back({renameMap[src_reg], inst->dqPosition});
        }

        auto old = renameMap[src_reg];

        renameMap[src_reg] = inst->dqPosition;
        renameMap[src_reg].op = static_cast<unsigned int>(src_idx) + 1;
        auto new_ = renameMap[src_reg];

        DPRINTF(Rename, "Inst[%i] forward reg[%s %d]from (%d %d)(%d) "
                    "to (%d %d)(%d)\n",
                    inst->seqNum, src_reg.className(), src_reg.index(),
                    old.bank, old.index, old.op,
                    new_.bank, new_.index, new_.op);

    }

    const RegId& dest_reg = inst->destRegIdx(0);
    auto dest_idx = make_pair(dest_reg.classValue(), dest_reg.index());
    scoreboard[dest_idx] = false;
    reverseTable[dest_idx] = inst->dqPosition;
    renameMap[dest_reg] = inst->dqPosition;
    // defMap[dest_reg] = inst->dqPosition;
    auto &m = renameMap[dest_reg];
    DPRINTF(Rename, "Inst[%i] define reg[%s %d] int (%d %d)(%d)\n",
                    inst->seqNum, dest_reg.className(), dest_reg.index(),
                    m.bank, m.index, m.op);
    return pairs;
}

template<class Impl>
void ArchState<Impl>::setIntReg(int reg_idx, uint64_t val)
{
    scoreboard[make_pair(IntRegClass, reg_idx)] = true;
    intArchRF[reg_idx].i = val;
}

template<class Impl>
void ArchState<Impl>::setFloatReg(int reg_idx, double val)
{
    scoreboard[make_pair(FloatRegClass, reg_idx)] = true;
    floatArchRF[reg_idx].f = val;
}

template<class Impl>
void ArchState<Impl>::setFloatRegBits(int reg_idx, uint64_t val)
{
    scoreboard[make_pair(FloatRegClass, reg_idx)] = true;
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
pair<bool, FFRegValue> ArchState<Impl>::commitInst(DynInstPtr &inst)
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
    bool valid = true;
    FFRegValue val = FFRegValue();
    if (inst->numDestRegs() > 0) {
        // in RV it must be 1
        assert (inst->numDestRegs() == 1);

        val = inst->getDestValue();
        if (inst->isInteger()) {
            intArchRF[inst->staticInst->destRegIdx(0).index()] = val;
        } else if (inst->isInteger()) {
            floatArchRF[inst->staticInst->destRegIdx(0).index()] = val;
        } else {
            panic("not ready for other instructions!");
        }

        SBIndex idx = make_pair(inst->staticInst->destRegIdx(0).classValue(),
                inst->staticInst->destRegIdx(0).index());
        if (!scoreboard.count(idx)) {
            scoreboard[idx] = true;
        }
        if (!scoreboard[idx] && (reverseTable.count(idx) &&
                    reverseTable[idx] == inst->dqPosition)) {
            scoreboard[idx] = true;
        }
    } else {
        valid = false;
    }

    return make_pair(valid, val);
}

}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::ArchState<FFCPUImpl>;

