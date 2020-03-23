//
// Created by zyy on 19-6-11.
//
#include "cpu/forwardflow/arch_state.hh"

#include "base/trace.hh"
#include "cpu/thread_context.hh"
#include "debug/FFCommit.hh"
#include "debug/FFSquash.hh"
#include "debug/FanoutPred.hh"
#include "debug/RSProbe1.hh"
#include "debug/ReadyHint.hh"
#include "debug/Rename.hh"
#include "debug/Reshape.hh"
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

    CombRename++;

    for (unsigned src_idx = 0; src_idx < num_src_regs; src_idx++) {
        unsigned identical = 0;
        for (unsigned src_idx_x = 0; src_idx_x < src_idx; src_idx_x++) {
            if (inst->srcRegIdx(src_idx_x) == inst->srcRegIdx(src_idx)) {
                identical = src_idx_x + 1;
                break;
            }
        }

        if (identical) {
            DPRINTF(Rename, "Skipped to forward because src reg %i is identical to"
                    "src reg %u\n", src_idx, identical - 1);
            inst->identicalTo[src_idx + 1] = identical;

            inst->indirectRegIndices.at(identical).push_back(src_idx);
            DPRINTF(Rename, "phy op %i point to src reg %i\n", src_idx + 1, identical - 1);

        } else {
            inst->indirectRegIndices.at(src_idx + 1).push_back(src_idx);
            DPRINTF(Rename, "phy op %i point to src reg %i\n", src_idx + 1, src_idx);
        }
    }

    for (unsigned phy_op = 1; phy_op < Impl::MaxOps; phy_op++) {
        auto &indirect_index = inst->indirectRegIndices.at(phy_op);
        if (indirect_index.size() > 0) {
            DPRINTF(Reshape, "Before randomization, phy op %i point to src reg %i\n",
                    phy_op, indirect_index.front());
        }
    }

    if (decoupleOpPosition) {
        randomizeOp(inst);
    }

    int num_src_ops = 0, num_busy_ops = 0;
    for (unsigned phy_op = 1; phy_op < Impl::MaxOps; phy_op++) {
        auto &indirect_index = inst->indirectRegIndices.at(phy_op);
        if (indirect_index.size() > 0) {

            num_src_ops++;
            inst->indirectRegIds.at(phy_op) =
                inst->srcRegIdx(indirect_index.front());

            DPRINTF(Reshape, "After randomization, phy op %i point to src reg %i\n",
                    phy_op, indirect_index.front());
            if (inst->isForwarder()) {
                inst->forwardOp = phy_op;
                DPRINTF(Reshape, "Set forward op to %i\n", phy_op);
            }
        }
    }

    for (unsigned phy_op = 1; phy_op < Impl::MaxOps; phy_op++) {
        auto &indirect_indices = inst->indirectRegIndices.at(phy_op);

        if (indirect_indices.empty()) {
            continue;
        }
        DPRINTF(Rename, "Play with op [%i] -> src reg [%i]\n",
                phy_op, indirect_indices.front());

        inst->hasOp[phy_op] = true;

        const RegId& src_reg = inst->indirectRegIds[phy_op];

        if (src_reg.isZeroReg()) {
            DPRINTF(Rename, "Skip zero reg\n");
            FFRegValue v;
            v.i = 0;
            for (const auto i: indirect_indices) {
                inst->setSrcValue(i, v);
            }
            inst->srcTakenWithInst[phy_op] = true;
            inst->opReady[phy_op] = true;
            pairs.push_back(invalid_pair);
            continue;
        }
        auto sb_index = make_pair(src_reg.classValue(), src_reg.index());

        if (scoreboard.count(sb_index) && scoreboard[sb_index]) {

            if (!readyHint) {
                RegWriteCommitSB++;
            }
            for (const auto i: indirect_indices) {
                if (src_reg.classValue() == FloatRegClass) {
                    RegReadARF++;
                    inst->setSrcValue(i, floatArchRF[src_reg.index()]);

                } else if (src_reg.classValue() == IntRegClass) {
                    RegReadARF++;
                    inst->setSrcValue(i, intArchRF[src_reg.index()]);

                } else {
                    panic("Unexpected reg type\n");
                }
            }
            inst->srcTakenWithInst[phy_op] = true;
            inst->opReady[phy_op] = true;
            pairs.push_back(invalid_pair);

            DPRINTF(Rename||Debug::RSProbe1,
                    "Inst[%llu] read reg[%s %d] directly from arch RF\n",
                    inst->seqNum, src_reg.className(), src_reg.index());

        } else if (readyHint && hintSB.count(sb_index) && hintSB[sb_index]) {

            RegReadSpecSB++;

            for (const auto i: indirect_indices) {
                if (src_reg.classValue() == FloatRegClass) {
                    inst->setSrcValue(i, hintFloatRF[src_reg.index()]);
                    RegReadSpecARF++;

                } else if (src_reg.classValue() == IntRegClass) {
                    inst->setSrcValue(i, hintIntRF[src_reg.index()]);
                    RegReadSpecARF++;

                } else {
                    panic("Unexpected reg type\n");
                }
            }
            inst->srcTakenWithInst[phy_op] = true;
            inst->opReady[phy_op] = true;
            pairs.push_back(invalid_pair);

            DQPointer parent_ptr = parentMap[src_reg];

            countChild(parent_ptr, inst);

            DPRINTF(Rename||Debug::RSProbe1,
                    "Inst[%llu] read reg[%s %d] fortunately from hint RF",
                    inst->seqNum, src_reg.className(), src_reg.index());
            if (src_reg.classValue() == FloatRegClass) {
                DPRINTFR(Rename||Debug::RSProbe1,
                        " with value: %f\n", hintFloatRF[src_reg.index()].f);
            } else {
                DPRINTFR(Rename||Debug::RSProbe1,
                        " with value: %llu\n", hintIntRF[src_reg.index()].i);
            }

        } else {
            if (!parentMap.count(src_reg) || !renameMap.count(src_reg)) {
                dumpMaps();
                assert(parentMap.count(src_reg)); // to parents only
                assert(renameMap.count(src_reg)); // to parents or siblings
            }
            num_busy_ops++;

            DQPointer parent_ptr = parentMap[src_reg];

            DPRINTF(Rename, "Looking up %s arch reg %i, got pointer" ptrfmt "\n",
                    src_reg.className(), src_reg.index(), extptr(parent_ptr));

            countChild(parent_ptr, inst);

            for (const auto i: indirect_indices) {
                inst->renameSrcReg(i, parent_ptr);
                DPRINTF(Reshape||Debug::RSProbe1 || Debug::Rename,
                        "Rename src reg(%i) to" ptrfmt "\n", i, extptr(parent_ptr));
            }

            auto renamed_ptr = renameMap[src_reg];
            bool predecessor_is_forwarder = false;
            DynInstPtr predecessor = dq->readInst(renamed_ptr);
            if (predecessor && !predecessor->isSquashed() && predecessor->isForwarder()) {
                predecessor_is_forwarder = true;
            }

            diewc->setOldestFw(renameMap[src_reg]);

            auto &old = renameMap[src_reg];
            auto dest = inst->dqPosition;
            dest.op = phy_op;
            pairs.push_back({old, dest});

            RegReadMap++;
            if (!predecessor_is_forwarder || old.op == 3) {
                assert(old.op <= 3);
                DPRINTF(Rename||Debug::RSProbe1,
                        "Inst[%lu] forward reg[%s %d]from" ptrfmt "to" ptrfmt "\n",
                        inst->seqNum, src_reg.className(), src_reg.index(),
                        extptr(old), extptr(dest));
                renameMap[src_reg] = dest;
                RegWriteMap++;

            } else {
                assert(old.op < 3);
                old.op = old.op + 1;
                DPRINTF(Reshape||Debug::RSProbe1,
                        "(%i %i) (%i) incremented to (%i %i) (%i) "
                        "with dest: (%i %i) (%i)\n",
                        old.bank, old.index, old.op - 1,
                        old.bank, old.index, old.op,
                        dest.bank, dest.index, dest.op);
            }
        }
    }
    numBusyOperands[num_src_ops] += num_busy_ops;
    numDispInsts[num_src_ops]++;

    if (inst->numDestRegs()) {
        const RegId& dest_reg = inst->destRegIdx(0);
        if (dest_reg.isZeroReg()) {
            DPRINTF(Rename, "Skip zero dest reg\n");
            inst->hasOp[0] = false;

        } else {
            auto dest_idx = make_pair(dest_reg.classValue(), dest_reg.index());
            if (!inst->isForwarder()) {
                scoreboard[dest_idx] = false;
                hintSB[dest_idx] = false;
                RegWriteCommitSB++;
                if (readyHint) {
                    RegWriteSpecSB++;
                }

                reverseTable[dest_idx] = inst->dqPosition;
                hintRT[dest_idx] = inst->dqPosition;
                RegWriteRT++;
                if (readyHint) {
                    RegWriteSpecRT++;
                }

                parentMap[dest_reg] = inst->dqPosition;
                RegWriteMap++;
            }
            renameMap[dest_reg] = inst->dqPosition;

            auto &m = renameMap[dest_reg];
            DPRINTF(Rename||Debug::RSProbe1,
                    "Inst[%lu] defines reg[%s %d]" ptrfmt "\n",
                    inst->seqNum, dest_reg.className(), dest_reg.index(), extptr(m));
        }
    } else {
        inst->hasOp[0] = false;
        DPRINTF(Rename, "Skip inst without dest reg\n");
    }

    if (inst->isControl() ||
            (diewc->cptHint &&
            diewc->toCheckpoint == inst->instAddr())) {
        takeCheckpoint(inst);
//        diewc->cptHint = false;
    }

    DPRINTF(Rename, "Rename produces %lu pairs\n", pairs.size());
    return pairs;
}

template<class Impl>
void ArchState<Impl>::setIntReg(int reg_idx, uint64_t val)
{
    scoreboard[make_pair(IntRegClass, reg_idx)] = true;
    hintSB[make_pair(IntRegClass, reg_idx)] = true;
    intArchRF[reg_idx].i = val;
}

template<class Impl>
void ArchState<Impl>::setFloatReg(int reg_idx, double val)
{
    scoreboard[make_pair(FloatRegClass, reg_idx)] = true;
    hintSB[make_pair(FloatRegClass, reg_idx)] = true;
    floatArchRF[reg_idx].f = val;
}

template<class Impl>
void ArchState<Impl>::setFloatRegBits(int reg_idx, uint64_t val)
{
    scoreboard[make_pair(FloatRegClass, reg_idx)] = true;
    hintSB[make_pair(FloatRegClass, reg_idx)] = true;
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

    RegReadMap += numTotalReg;
    SRAMWriteMap += numTotalReg;

    RegReadCommitSB += numTotalReg;
    SRAMWriteSB += numTotalReg;

    RegReadRT += numTotalReg;
    SRAMWriteRT += numTotalReg;

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
    SRAMReadMap += numTotalReg;
    RegWriteMap += numTotalReg;

    parentMap = cpt.parentMap;

    scoreboard = cpt.scoreboard;
    SRAMReadSB += numTotalReg;
    RegWriteCommitSB += numTotalReg;

    reverseTable = cpt.reverseTable;
    SRAMReadRT += numTotalReg;
    RegWriteRT += numTotalReg;

//    RegWriteARF += numTotalReg;

    if (readyHint) {
        hintSB = cpt.scoreboard;
        RegWriteSpecSB += numTotalReg;

        hintRT = cpt.reverseTable;
        RegWriteSpecRT += numTotalReg;

//        RegWriteSpecARF += numTotalReg;
    }

    auto it = cpts.begin();
    while (it != cpts.end()) {
        if (it->first > num) {
            it = cpts.erase(it);
        } else {
            it++;
        }
    }
}

template<class Impl>
ArchState<Impl>::ArchState(DerivFFCPUParams *params)
    : MaxCheckpoints(params->MaxCheckpoints),
    gen(0xa2c57a7e),
    decoupleOpPosition(params->DecoupleOpPosition),
    readyHint(params->ReadyHint)
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
        if (it->first <= num) {
            DPRINTF(FFCommit, "inst[%llu] is older than cpt[%llu], erase it!\n",
                    num, it->first);
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
        if (dest.isIntReg()) {
            intArchRF[dest.index()] = val;

        } else if (dest.isFloatReg()) {
            floatArchRF[dest.index()] = val;

        } else {
            panic("not ready for other instructions!");
        }

        DPRINTF(FFCommit, "CommitSB in current arch state\n");
        bool updated = commitInstInSB(inst, scoreboard, reverseTable, dest);
        RegWriteCommitSB++;
        if (updated) {
            RegWriteARF++;
            RegWriteRT++;
        }

        for (auto &pair: cpts) {
            if (pair.first >= inst->seqNum) {
                DPRINTF(FFCommit, "CommitSB in checkpoint[%llu]\n", pair.first);
                commitInstInSB(inst, pair.second.scoreboard,
                        pair.second.reverseTable, dest);
            } else {
                DPRINTF(FFCommit, "inst[%llu] is younger than cpt[%llu], skip!\n",
                        inst->seqNum, pair.first);
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
void ArchState<Impl>::setDQ(DQTop *_dq)
{
    dq = _dq;
}

template<class Impl>
bool
ArchState<Impl>::commitInstInSB(
        DynInstPtr &inst, Scoreboard &sb, ReverseTable &rt, const RegId &dest)
{
    bool ret = false;
    SBIndex idx = make_pair(dest.classValue(), dest.index());
    if (!sb.count(idx)) {
        DPRINTF(FFCommit,"Set reg (%s %i) for the first time\n",
                dest.className(), dest.index());
        sb[idx] = true;
        ret = true;
    } else {
        if (!sb[idx] && (rt.count(idx) &&
                    rt[idx] == inst->dqPosition)) {
            DPRINTF(FFCommit,"Set reg (%s %i) by inst[%llu]" ptrfmt "\n",
                    dest.className(), dest.index(), inst->seqNum,
                    extptr(inst->dqPosition));
            sb[idx] = true;
            ret = true;
        } else {
            if (!rt.count(idx)) {
                DPRINTF(FFCommit,"Reg (%s %i) remains to busy because it"
                        " was not cleared by other instructions???\n", // funny
                        dest.className(), dest.index());
            } else {
                DPRINTF(FFCommit,"Reg (%s %i) remains to busy"
                                 " becase its was cleared by" ptrfmt
                                 ", but committing inst[%llu] is @" ptrfmt "\n",
                        dest.className(), dest.index(),
                        extptr(rt[idx]),
                        inst->seqNum,
                        extptr(inst->dqPosition));

            }

        }
    }
    return ret;
}

template<class Impl>
InstSeqNum ArchState<Impl>::getYoungestCPTBefore(InstSeqNum violator)
{
    InstSeqNum youngest = 0;
    for (const auto& it: cpts) {
        DPRINTFR(FFSquash, "Traverse cpt: %llu\n", it.first);
        if (it.first < violator && it.first > youngest) {
            youngest = it.first;
        }
    }
    return youngest;
}

template<class Impl>
void ArchState<Impl>::squashAll()
{
    cpts.clear();
    renameMap.clear();
    parentMap.clear();
    reverseTable.clear();
    hintRT.clear();
    hintSB.clear();

    for (auto &pair: scoreboard) {
        pair.second = true;
    }
}

template<class Impl>
void ArchState<Impl>::dumpMaps()
{
    DPRINTF(Rename, "Definition map:\n");
    for (const auto &pair: parentMap) {
        DPRINTFR(Rename, "%s %i -> (%i %i) (%i)\n",
                pair.first.className(), pair.first.index(),
                pair.second.bank, pair.second.index, pair.second.op);
    }
    DPRINTF(Rename, "Forwarding map:\n");
    for (const auto &pair: renameMap) {
        DPRINTFR(Rename, "%s %i -> (%i %i) (%i)\n",
                 pair.first.className(), pair.first.index(),
                 pair.second.bank, pair.second.index, pair.second.op);
    }
}

template<class Impl>
std::pair<bool, bool>
ArchState<Impl>::forwardAfter(DynInstPtr &inst, std::list<DynInstPtr> &need_forward)
{
    bool is_lf_source = false;
    bool is_lf_drain = false;
    if (inst->predLargeFanout && inst->predReshapeProfit > 0) {
        is_lf_source = true;
        need_forward.push_back(inst);
    }
    // TODO! what if inst is  x = x !!!???
    unsigned num_src_regs = inst->numSrcRegs();

    std::list<RegId> forwarded;

    for (unsigned src_idx = 0; src_idx < num_src_regs; src_idx++) {
        const RegId& src_reg = inst->srcRegIdx(src_idx);
        if (src_reg.isZeroReg()) {
            DPRINTF(Reshape, "Skip zero reg\n");
            continue;
        }
        auto sb_index = make_pair(src_reg.classValue(), src_reg.index());
        if ((scoreboard.count(sb_index) && scoreboard[sb_index]) ||
                (readyHint && hintSB.count(sb_index) && hintSB[sb_index])) {
            DPRINTF(Reshape, "Skip already ready\n");
            continue; // already ready
        }

        if (inst->destRegIdx(0) == src_reg) {
            DPRINTF(Reshape, "Skip x = x\n");
            continue; // do not need to forward anymore
        }

        DQPointer renamed_ptr = renameMap[src_reg];
        DynInstPtr predecessor = dq->readInst(renamed_ptr); // sibling or parent
        if (!predecessor || predecessor->isSquashed()) {
            DPRINTF(Reshape, "Skip squashed\n");
            continue; //squashed
        }

        const auto &old = renameMap[src_reg];
        auto found = std::find(forwarded.begin(), forwarded.end(), src_reg);
        if (predecessor->isForwarder() && old.op >= 2 &&
                predecessor->numForwardRest > 0 && found == forwarded.end()) {
            is_lf_drain = true;
            forwarded.push_back(src_reg);
            need_forward.push_back(predecessor);
        } else {
            if (!predecessor->isForwarder()) {
                DPRINTF(Reshape, "predecessor is not forwarder\n");
            } else if (old.op < 2) {
                DPRINTF(Reshape, "old.op < 2\n");
            } else {
                assert(predecessor->numForwardRest == 0 || found != forwarded.end());
                DPRINTF(Reshape, "numForwardRest = %i\n", predecessor->numForwardRest);
            }
        }
    }
    return std::make_pair(is_lf_source, is_lf_drain);
}

template<class Impl>
void
ArchState<Impl>::randomizeOp(DynInstPtr &inst)
{
    std::shuffle(std::begin(inst->indirectRegIndices) + 1,
            std::end(inst->indirectRegIndices), gen);
}

template<class Impl>
void
ArchState<Impl>::postExecInst(DynInstPtr &inst) {
    FFRegValue val = FFRegValue();
    if (inst->numDestRegs() > 0) {
        // in RV it must be 1
        assert (inst->numDestRegs() == 1);

        const RegId &dest = inst->staticInst->destRegIdx(0);

        SBIndex idx = make_pair(dest.classValue(), dest.index());
        if (hintRT.count(idx) && hintRT[idx] == inst->dqPosition) {

            RegWriteSpecSB++;
            RegWriteSpecARF++;

            RegReadSpecRT++;

            hintSB[idx] = true;

            val = inst->getDestValue();
            if (dest.isIntReg()) {
                hintIntRF[dest.index()] = val;
                DPRINTF(ReadyHint, "Set Hint Int reg (%i) to %llu\n", dest.index(), val.i);

            } else if (dest.isFloatReg()) {
                hintFloatRF[dest.index()] = val;
                DPRINTF(ReadyHint, "Set Hint Float reg (%i) to %f\n", dest.index(), val.f);

            } else {
                panic("not ready for other instructions!");
            }
        }
    }

}

template<class Impl>
void
ArchState<Impl>::countChild(DQPointer parent_ptr, DynInstPtr &inst)
{
    DynInstPtr parent = dq->readInst(parent_ptr);
    if (!parent) {
        warn("Parent is null ptr at renaming\n");
    }

    if (parent && !inst->isForwarder()) {
        parent->numChildren++;
        DPRINTF(FanoutPred||Debug::RSProbe1,
                "inc num children of inst[%lu] to %u\n",
                parent->seqNum, parent->numChildren);

        const auto &ancestorPtr = parent->ancestorPointer;
        if (parent->isForwarder()) {
            DPRINTF(Reshape, "Parent is forwarder, trying to add to ancestor"
                    "(%i) (%i %i)\n", ancestorPtr.valid,
                    ancestorPtr.bank, ancestorPtr.index);
            if (dq->validPosition(dq->c.pointer2uint(ancestorPtr))) {
                if (dq->logicallyLT(dq->c.pointer2uint(ancestorPtr),
                            dq->c.pointer2uint(inst->dqPosition))) {
                    DPRINTF(Reshape, "Ancestor found!\n");
                } else {
                    DPRINTF(Reshape, "Ancestor has been committed!\n");
                }
            } else {
                DPRINTF(Reshape, "Ancestor not valid!\n");
            }
        }
        if (parent->isForwarder() && ancestorPtr.valid &&
                dq->validPosition(dq->c.pointer2uint(ancestorPtr)) &&
                dq->logicallyLT(dq->c.pointer2uint(ancestorPtr),
                    dq->c.pointer2uint(inst->dqPosition))) {
            DynInstPtr ancestor = dq->readInst(ancestorPtr);
            if (ancestor) {
                inst->ancestorPointer = ancestorPtr;
                ancestor->numChildren++;
                DPRINTF(FanoutPred, "inc num children of ancestor[%lu] to %u\n",
                        ancestor->seqNum, ancestor->numChildren);
            } else {
                DPRINTF(Reshape, "Ancestor (%i %i) is squashed\n",
                        ancestorPtr.bank, ancestorPtr.index);
            }
        }
    }
}

template<class Impl>
void
ArchState<Impl>::regStats()
{
    numBusyOperands
        .init(Impl::MaxOps)
        .name(name() + ".numBusyOperands")
        .desc("numBusyOperands");

    numDispInsts
        .init(Impl::MaxOps)
        .name(name() + ".numDispInsts")
        .desc("numDispInsts");

    for (int n_busy_op = 0; n_busy_op < Impl::MaxOps; n_busy_op++) {
        meanBusyOp[n_busy_op]
            .name(name() + ".meanBusyOp_" + std::to_string(n_busy_op))
            .desc("meanBusyOp");
        meanBusyOp[n_busy_op] = numBusyOperands[n_busy_op] / numDispInsts[n_busy_op];
    }

    CombRename
            .name(name() + ".CombRename")
            .desc("CombRename");
    RegReadCommitSB
            .name(name() + ".RegReadCommitSB")
            .desc("RegReadCommitSB");
    RegWriteCommitSB
            .name(name() + ".RegWriteCommitSB")
            .desc("RegWriteCommitSB");
    RegReadSpecSB
            .name(name() + ".RegReadSpecSB")
            .desc("RegReadSpecSB");
    RegWriteSpecSB
            .name(name() + ".RegWriteSpecSB")
            .desc("RegWriteSpecSB");
    RegReadMap
            .name(name() + ".RegReadMap")
            .desc("RegReadMap");
    RegWriteMap
            .name(name() + ".RegWriteMap")
            .desc("RegWriteMap");

    SRAMWriteMap
            .name(name() + ".SRAMWriteMap")
            .desc("SRAMWriteMap");
    SRAMReadMap
            .name(name() + ".SRAMReadMap")
            .desc("SRAMReadMap");

    SRAMReadSB
            .name(name() + ".SRAMReadSB")
            .desc("SRAMReadSB");
    SRAMWriteSB
            .name(name() + ".SRAMWriteSB")
            .desc("SRAMWriteSB");

    RegReadRT
            .name(name() + ".RegReadRT")
            .desc("RegReadRT");
    RegWriteRT
            .name(name() + ".RegWriteRT")
            .desc("RegWriteRT");

    RegReadSpecRT
            .name(name() + ".RegReadSpecRT")
            .desc("RegReadSpecRT");
    RegWriteSpecRT
            .name(name() + ".RegWriteSpecRT")
            .desc("RegWriteSpecRT");
    SRAMReadRT
            .name(name() + ".SRAMReadRT")
            .desc("SRAMReadRT");
    SRAMWriteRT
            .name(name() + ".SRAMWriteRT")
            .desc("SRAMWriteRT");

    RegReadARF
            .name(name() + ".RegReadARF")
            .desc("RegReadARF");
    RegWriteARF
            .name(name() + ".RegWriteARF")
            .desc("RegWriteARF");

    RegReadSpecARF
            .name(name() + ".RegReadSpecARF")
            .desc("RegReadSpecARF");
    RegWriteSpecARF
            .name(name() + ".RegWriteSpecARF")
            .desc("RegWriteSpecARF");
}

}

#include "cpu/forwardflow/isa_specific.hh"

template class FF::ArchState<FFCPUImpl>;

