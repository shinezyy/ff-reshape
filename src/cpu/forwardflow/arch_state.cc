//
// Created by zyy on 19-6-11.
//

#include "cpu/forwardflow/arch_state.hh"
#include "base/trace.hh"
#include "cpu/thread_context.hh"
#include "debug/FFCommit.hh"
#include "debug/FFSquash.hh"
#include "debug/Rename.hh"
#include "params/DerivFFCPU.hh"

namespace FF
{

using namespace std;

template<class Impl>
std::list<PointerPair> ArchState<Impl>::recordAndUpdateMap(DynInstPtr &inst)
{
    unsigned num_src_regs = inst->numSrcRegs();

    // Get the architectual register numbers from the source and
    // operands, and redirect them to the right physical register.
    std::list<PointerPair> pairs;

    PointerPair invalid_pair{};
    invalid_pair.payload.valid = false;
    invalid_pair.dest.valid = false;

    for (unsigned src_idx = 0; src_idx < num_src_regs; src_idx++) {
        const RegId& src_reg = inst->srcRegIdx(src_idx);
        inst->hasOp[src_idx + 1] = true;

        if (src_reg.isZeroReg()) {
            DPRINTF(Rename, "Skip zero reg\n");
            FFRegValue v;
            v.i = 0;
            inst->srcValues[src_idx] = v;
            inst->srcTakenWithInst[src_idx] = true;
            inst->opReady[src_idx + 1] = true;
            pairs.push_back(invalid_pair);
            continue;
        }

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
            // inst->markSrcRegReady(src_idx);
            pairs.push_back(invalid_pair);

            DPRINTF(Rename,"Inst[%llu] read reg[%s %d] directly from arch RF\n",
                    inst->seqNum, src_reg.className(), src_reg.index());

            inst->opReady[src_idx + 1] = true;

        } else {
            assert(parentMap.count(src_reg)); // to parents only
            assert(renameMap.count(src_reg)); // to parents or siblings
            DQPointer renamed_ptr = parentMap[src_reg];

            DPRINTF(Rename, "Looking up %s arch reg %i"
                    ", got pointer (%i %i)\n",
                    src_reg.className(), src_reg.index(),
                    renamed_ptr.bank, renamed_ptr.index);

            inst->renameSrcReg(src_idx, renamed_ptr);

            diewc->setOldestFw(renameMap[src_reg]);

            auto old = renameMap[src_reg];

            renameMap[src_reg] = inst->dqPosition;
            renameMap[src_reg].op = static_cast<unsigned int>(src_idx) + 1;
            auto new_ = renameMap[src_reg];

            pairs.push_back({old, new_});

            DPRINTF(Rename, "Inst[%i] forward reg[%s %d]from (%d %d)(%d) "
                    "to (%d %d)(%d)\n",
                    inst->seqNum, src_reg.className(), src_reg.index(),
                    old.bank, old.index, old.op,
                    new_.bank, new_.index, new_.op);
        }

    }

    const RegId& dest_reg = inst->destRegIdx(0);
    if (dest_reg.isZeroReg()) {
        DPRINTF(Rename, "Skip zero dest reg\n");
        inst->hasOp[0] = false;

    } else {
        auto dest_idx = make_pair(dest_reg.classValue(), dest_reg.index());
        scoreboard[dest_idx] = false;
        reverseTable[dest_idx] = inst->dqPosition;
        parentMap[dest_reg] = inst->dqPosition;
        renameMap[dest_reg] = inst->dqPosition;
        auto &m = renameMap[dest_reg];
        DPRINTF(Rename, "Inst[%i] define reg[%s %d] int (%d %d)(%d)\n",
                inst->seqNum, dest_reg.className(), dest_reg.index(),
                m.bank, m.index, m.op);
    }

    if (inst->isControl()) {
        takeCheckpoint(inst);
    }

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
bool ArchState<Impl>::takeCheckpoint(DynInstPtr &inst)
{
    DPRINTF(FFSquash, "Take checkpoint on inst[%llu] %s pc:%s\n",
            inst->seqNum, inst->staticInst->disassemble(inst->instAddr()),
            inst->pcState());
    assert(!cpts.count(inst->seqNum));
    cpts[inst->seqNum] = {renameMap, parentMap, scoreboard, reverseTable};
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
    DPRINTF(FFSquash, "Recover checkpoint from inst[%llu]\n", num);
    assert(cpts.count(num));
    Checkpoint &cpt = cpts[num];
    renameMap = cpt.renameMap;
    parentMap = cpt.parentMap;
    scoreboard = cpt.scoreboard;
    reverseTable = cpt.reverseTable;

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

        const RegId &dest = inst->staticInst->destRegIdx(0);
        val = inst->getDestValue();
        if (inst->isInteger()) {
            intArchRF[dest.index()] = val;
        } else if (inst->isInteger()) {
            floatArchRF[dest.index()] = val;
        } else {
            panic("not ready for other instructions!");
        }

        DPRINTF(FFCommit, "CommitSB in current arch state\n");
        commitInstInSB(inst, scoreboard, reverseTable, dest);
        for (auto &pair: cpts) {
            if (pair.first >= inst->seqNum) {
                DPRINTF(FFCommit, "CommitSB in checkpoint[%llu]\n", pair.first);
                commitInstInSB(inst, pair.second.scoreboard,
                        pair.second.reverseTable, dest);
            }
        }

    } else {
        valid = false;
    }

    return make_pair(valid, val);
}

template<class Impl>
void ArchState<Impl>::setDIEWC(DIEWC *_diewc)
{
    diewc = _diewc;
}

template<class Impl>
void
ArchState<Impl>::commitInstInSB(
        DynInstPtr &inst, Scoreboard &sb, ReverseTable &rt, const RegId &dest)
{
    SBIndex idx = make_pair(dest.classValue(), dest.index());
    if (!sb.count(idx)) {
        DPRINTF(FFCommit,"Set reg (%s %i) for the first time\n",
                dest.className(), dest.index());
        sb[idx] = true;
    } else {
        if (!sb[idx] && (rt.count(idx) &&
                    rt[idx] == inst->dqPosition)) {
            DPRINTF(FFCommit,"Set reg (%s %i) cleared by inst[%llu] (%i %i)\n",
                    dest.className(), dest.index(), inst->seqNum,
                    inst->dqPosition.bank, inst->dqPosition.index);
            sb[idx] = true;
        } else {
            if (!rt.count(idx)) {
                DPRINTF(FFCommit,"Reg (%s %i) remains to busy because it"
                        " was not cleared by other instructions???", // funny
                        dest.className(), dest.index());
            } else {
                DPRINTF(FFCommit,"Reg (%s %i) remains to busy"
                        " becase its was cleared by (%i %i),"
                        " but committing inst[%llu] is @(%i %i)\n",
                        dest.className(), dest.index(),
                        rt[idx].bank,rt[idx].index,
                        inst->seqNum, inst->dqPosition.bank, inst->dqPosition.index);

            }

        }
    }
}

}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::ArchState<FFCPUImpl>;

