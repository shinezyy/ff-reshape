#include "base/trace.hh"
#include "cpu/o3/loop_buffer.hh"
#include "debug/LoopBuffer.hh"
#include "debug/LoopBufferStack.hh"

LoopBuffer::LoopBuffer(const LoopBufferParams *params)
    :
        SimObject(*params),
        numEntries(params->numEntries),
        _name("LoopBuffer"),
        entrySize(params->entrySize),
        mask(~(entrySize - 1)),
        enable(params->enable),
        loopFiltering(params->loopFiltering),
        maxForwardBranches(params->maxForwardBranches)
{
    assert(numEntries >= 2 * evictRange);
    instSupply.invalidate();
}

LoopBuffer::~LoopBuffer()
{
}

void
LoopBuffer::processNewControl(Addr branch_pc, Addr target)
{
    if (table.count(target)) {
        assert(table[target].valid && !table[target].fetched);
        return;
    }
    table[target];
    table[target].instPayload = new uint8_t[entrySize];
    table[target].valid = true;
    table[target].fetched = false;
    table[target].used = 0;
    table[target].branchPC = branch_pc;

    rank.emplace_back(table.find(target));

    DPRINTF(LoopBufferStack, "Inserted PC: 0x%x\n", target);
    for (const auto &ele: rank) {
        DPRINTFR(LoopBufferStack, "PC: 0x%x, used: %u\n",
                ele->first, ele->second.used);
    }

    if (table.size() > numEntries) {
        auto evicted = random_mt.random<unsigned>(1, 1 + evictRange);
        DPRINTF(LoopBufferStack, "Evicting -%u\n", evicted - 1);
        OrderRIt it = rank.rbegin(); // pointed to newly inserted
        std::advance(it, evicted);

        delete (*it)->second.instPayload;

        table.erase((*it));

        rank.erase(std::next(it).base());
    }

    pending = true;
    pendingTarget = target;

    assert(rank.size() == table.size());
}

void
LoopBuffer::updateControl(Addr target)
{
    assert(table.count(target));
    auto used = ++table[target].used;
    totalUsed++;

    OrderIt insert_pos = rank.begin(), e = rank.end();
    for (; insert_pos != e; insert_pos++) {
        if ((*insert_pos)->second.used < used) {
            break;
        }
    }

    if (insert_pos != e) {
        OrderIt ele_pos = rank.begin();
        for (; ele_pos != e; ele_pos++) {
            if ((*ele_pos)->first == target) {
                break;
            }
        }

        assert(ele_pos != e);

        rank.splice(insert_pos, rank, ele_pos, std::next(ele_pos));
        // rank.insert(insert_pos, *ele_pos);
        // rank.erase(ele_pos);
    }

    if (totalUsed > 100) {
        for (auto &pair: table) {
            pair.second.used *= 0.9;
        }
        totalUsed = 0;
    }

    assert(rank.size() == table.size());
}

bool
LoopBuffer::hasPendingRecordTxn()
{
    return pending;
}

uint8_t*
LoopBuffer::getPendingEntryPtr()
{
    assert(table.count(pendingTarget));
    return table[pendingTarget].instPayload;
}

Addr
LoopBuffer::getPendingEntryTarget()
{
    return pendingTarget;
}

void
LoopBuffer::clearPending()
{
    pending = false;
}

uint8_t*
LoopBuffer::getBufferedLine(Addr pc)
{
    if (table.count(pc) && table[pc].valid && table[pc].fetched) {
        return table[pc].instPayload;
    } else {
        return nullptr;
    }
}

void
LoopBuffer::switchTo(Addr target_pc)
{
    instSupply.valid    = true;

    instSupply.buf      = getBufferedLine(target_pc);
    assert(instSupply.buf);
    instSupply.offset   = 0;

    instSupply.start    = target_pc;
    instSupply.end      = getBufferedLineBranchPC(target_pc);
    DPRINTF(LoopBuffer, "Switch to loop 0x%x|_>0x%x\n",
            instSupply.end, instSupply.start);

    instSupply.expectedPC = target_pc;

    auto fb_state = table[target_pc].forwardBranchState;
    instSupply.forwardBranchIndex = 0;
    if (fb_state->valid) {
        instSupply.expectedForwardBranch.set(
                fb_state->forwardBranches[instSupply.forwardBranchIndex]);
    } else {
        instSupply.expectedForwardBranch.invalidate();
    }
}

bool
LoopBuffer::canProvide(Addr pc)
{
    if (table.count(pc) && table[pc].valid && table[pc].fetched) {
        switchTo(pc);
        return true;
    } else {
        return false;
    }
}

void
LoopBuffer::setFetched(Addr target)
{
    assert(table.count(target));
    assert(table[target].valid && !table[target].fetched);
    table[target].fetched = true;
}

void
LoopBuffer::probe(Addr branch_pc, Addr target_pc, bool pred_taken)
{
    if (getBufferedLine(target_pc)) {
        updateControl(target_pc);
        return;
    }

    // if not taken but recording a forward branch
    switch (txn.state){
        case Recorded:
        case Invalid:
        case Aborted:
            if (isBackward(branch_pc, target_pc) &&
                    (branch_pc - target_pc) < entrySize - 4) {
                DPRINTF(LoopBuffer, "Observing 0x%x|__>0x%x\n",
                        branch_pc, target_pc);
                txn.reset();
                txn.state = LRTxnState::Observing;
                txn.branchPC = branch_pc;
                txn.targetPC = target_pc;
            }
            break;

        case Observing:
            if (branch_pc == txn.branchPC && target_pc == txn.targetPC) {
                txn.count++;

                if (txn.forwardBranchState->valid && txn.forwardBranchState->firstLap) {
                    txn.forwardBranchState->firstLap = false;
                }

                DPRINTF(LoopBuffer, "Counting 0x%x|__>0x%x: %u\n",
                        txn.branchPC, txn.targetPC, txn.count);

                if (txn.count >= txn.recordThreshold) {
                    DPRINTF(LoopBuffer, "0x%x|__>0x%x is qualified\n",
                            txn.branchPC, txn.targetPC);
                    txn.state = Recording;
                    // buffer size?
                    processNewControl(branch_pc, target_pc);
                    pendingTarget = target_pc;
                    txn.expectedPC = target_pc;
                    txn.offset = 0;
                }

            } else if (pred_taken && isBackward(branch_pc, target_pc) &&
                    (branch_pc - target_pc) < entrySize - 4) {
                txn.reset();
                if (!table.count(target_pc)) {
                    txn.branchPC = branch_pc;
                    txn.targetPC = target_pc;
                    txn.state = Observing;
                    DPRINTF(LoopBuffer, "Switch to observe 0x%x|__>0x%x: %u\n",
                            txn.branchPC, txn.targetPC, txn.count);
                } else {
                    txn.abort();
                    DPRINTF(LoopBuffer, "Loop 0x%x|__>? is in current loop, "
                            "do not support nested loop, abort\n", target_pc);
                }

            } else if (pred_taken && isForward(branch_pc, target_pc) &&
                    target_pc < txn.branchPC) { // in-loop forward branch
                bool new_f_jump = false;
                auto &fb_state = txn.forwardBranchState;
                auto &fw_branches = fb_state->forwardBranches;
                if (!fb_state->valid) {
                    // validate it
                    fb_state->valid = true;
                    fb_state->firstLap = true;
                    fw_branches.emplace_back(
                            branch_pc, target_pc);
                    new_f_jump = true;

                } else if (fb_state->firstLap) {
                    if (fw_branches.size() >= maxForwardBranches) {
                        txn.abort();
                        DPRINTF(LoopBuffer, "Abort 0x%x|__>0x%x due to forward branch:"
                                "0x%x|__>0x%x exceeds size limit\n",
                                txn.branchPC, txn.targetPC,
                                branch_pc, target_pc);
                    } else {
                        fw_branches.emplace_back(
                            branch_pc, target_pc);
                        fb_state->observingIndex++;
                        new_f_jump = true;
                    }

                } else {
                    // valid and not in the first lap
                    if (fw_branches[fb_state->observingIndex].branch == branch_pc &&
                            fw_branches[fb_state->observingIndex].target == target_pc) {
                        DPRINTF(LoopBuffer, "Forward branch 0x%x|__>0x%x of loop "
                                "0x%x|__>0x%x is confirmed again\n",
                                branch_pc, target_pc,
                                txn.branchPC, txn.targetPC);
                    } else {
                        // on a different path
                        if (fw_branches[fb_state->observingIndex].target != target_pc) {
                            DPRINTF(LoopBuffer, "Forward branch 0x%x|__>0x%x of loop "
                                    "0x%x|__>0x%x jumped to different place\n",
                                    branch_pc, target_pc,
                                    txn.branchPC, txn.targetPC);
                        } else {
                            DPRINTF(LoopBuffer, "Forward branch 0x%x|__>0x%x of loop "
                                    "0x%x|__>0x%x is unexpected\n",
                                    branch_pc, target_pc,
                                    txn.branchPC, txn.targetPC);
                        }
                        txn.abort();
                    }
                }

                if (new_f_jump) {
                    DPRINTF(LoopBuffer, "Register new forward jump 0x%x|__>0x%x"
                            " in loop 0x%x|__>0x%x\n",
                            branch_pc, target_pc,
                            txn.branchPC, txn.targetPC
                            );
                }

            } else if (pred_taken) {
                DPRINTF(LoopBuffer, "Abort 0x%x|__>0x%x due to another taken branch:"
                        "0x%x|__>0x%x\n",
                        txn.branchPC, txn.targetPC,
                        branch_pc, target_pc);
                txn.abort();
            }
            break;

        case Recording:
            if (branch_pc == txn.branchPC) {
                // A full loop has been recorded
                txn.state = Recorded;
            }
            break;
    }
}

void
LoopBuffer::padding_2()
{
    DPRINTF(LoopBuffer, "Padding 2 uchar to offset %u, inc offset to %u\n",
            txn.offset, txn.offset + 2);
    getPendingEntryPtr()[txn.offset] = 0;
    getPendingEntryPtr()[txn.offset + 1] = 0;
    txn.offset += 2;
}

void
LoopBuffer::recordInst(uint8_t *building_inst, Addr pc, unsigned inst_size)
{
    if (txn.state != Recording) {
        return;
    }

    if (pc != txn.expectedPC) {
        DPRINTF(LoopBuffer, "0x%x|__>0x%x: 0x%x does not match expectedPC: 0x%x, abort\n",
                txn.branchPC, txn.targetPC, pc, txn.expectedPC);
        // insn stream is redirected
        txn.abort();
        return;
    }

    DPRINTF(LoopBuffer, "0x%x|__>0x%x: recording 0x%x to 0x%x (offset: %u)\n",
            txn.branchPC, txn.targetPC, pc,
            getPendingEntryTarget(), txn.offset);

# define copy_in() \
    do { \
        memcpy(getPendingEntryPtr() + txn.offset, \
                building_inst, inst_size); \
        txn.offset += inst_size; \
    } while (0)

    if (pc % 4 == 0) {
        copy_in();
    } else { // == 2
        if (txn.offset % 4 == 0) {
            padding_2();
            copy_in();
        } else {
            copy_in();
        }
    }

#undef copy_in

    if (pc == txn.branchPC) {
        DPRINTF(LoopBuffer,
                "0x%x|__>0x%x: completely filled! with %u fjumps\n",
                txn.branchPC, txn.targetPC,
                txn.forwardBranchState->forwardBranches.size());
        txn.state = Recorded;
        setFetched(txn.targetPC);
        clearPending();
        table[txn.targetPC].branchPC = txn.branchPC;
        table[txn.targetPC].forwardBranchState = txn.forwardBranchState;
        txn.forwardBranchState = nullptr;
        return;
    }

    auto &fb_state = txn.forwardBranchState;
    auto &fw_branches = fb_state->forwardBranches;

    if (fb_state->valid &&
            pc == fw_branches[fb_state->recordIndex].branch) {
        txn.expectedPC = fw_branches[fb_state->recordIndex].target;
        DPRINTF(LoopBuffer, "0x%x|__>0x%x: recording will jump to 0x%x (offset: %u)\n",
                txn.branchPC, txn.targetPC, txn.expectedPC, txn.offset);
        fb_state->recordIndex++;

        if (txn.offset % 4 != 0) { // ==2
            padding_2();
        }
    } else {
        txn.expectedPC += inst_size;
    }
}

bool
LoopBuffer::isBackward(Addr branch_pc, Addr target_pc)
{
    return branch_pc > target_pc;
}

bool
LoopBuffer::isForward(Addr branch_pc, Addr target_pc)
{
    return branch_pc < target_pc;
}

Addr
LoopBuffer::getBufferedLineBranchPC(Addr target_pc)
{
    if (table.count(target_pc)) {
        return table[target_pc].branchPC;
    } else {
        return 0;
    }
}

bool
LoopBuffer::inRange(Addr target, Addr fetch_pc)
{
    assert(table.count(target));
    auto end_pc = table[target].branchPC;
    return fetch_pc >= target && fetch_pc <= end_pc;
}

bool
LoopBuffer::canContinueOnPC(Addr fetch_addr, Addr inst_pc)
{
    if (!instSupply.valid) {
        return false;
    }
    bool cont = inst_pc == instSupply.expectedPC;
    if (!cont) {
        DPRINTF(LoopBuffer, "Cannot continue, PC: %#lx, expected: %#lx\n",
                inst_pc, instSupply.expectedPC);
        instSupply.invalidate();
    }
    return cont;
}

uint8_t*
LoopBuffer::getInst(Addr fetch_addr, Addr inst_pc)
{
    // assert(pc >= instSupply.start && pc <= instSupply.end);
    uint8_t *ret;
    DPRINTF(LoopBuffer, "Req PC: %#lx\n", inst_pc);
    assert(fetch_addr % 4 == 0);
    assert(inst_pc == instSupply.expectedPC);
    if (instSupply.offset == 0 &&
            inst_pc == instSupply.start && (inst_pc % 4 == 2) && fetch_addr > inst_pc) {
        instSupply.offset += 4;
        DPRINTF(LoopBuffer,
                "Switch from cache line to lbuf at pc: %#lx, fetch addr: %#lx, "
                "inc offset to %u\n",
                inst_pc, fetch_addr, instSupply.offset);
    }
    ret = instSupply.buf + instSupply.offset;

    DPRINTF(LoopBuffer, "Response from loop 0x%x|_>0x%x offset: %u\n",
            instSupply.end, instSupply.start, instSupply.offset);

    return ret;
}

bool
LoopBuffer::canContinueOnNPC(Addr cpc, Addr npc, bool is_taken)
{
    auto &efb = instSupply.expectedForwardBranch;
    bool cont;
    if (is_taken) {
        DPRINTF(LoopBuffer, "Predicted taken\n");
        if (!instSupply.valid) {
            cont = false;
        } else if (cpc == instSupply.end && npc == instSupply.start) {
            // normal loop back
            cont = true;
        } else if (isBackward(cpc, npc)) {
            // do not support loop in loop
            cont = false;
        } else if (isForward(cpc, npc)) {
            if (efb.valid && efb.pair.branch == cpc
                    && efb.pair.target == npc) {
                DPRINTF(LoopBuffer, "Forward branch: 0x%x|_>0x%x matched\n",
                        cpc, npc);
                cont = true;
            } else {
                DPRINTF(LoopBuffer, "Forward branch(T): 0x%x|_>0x%x mismatched"
                        ", expected(T): (%i) 0x%x|_>0x%x\n",
                        cpc, npc,
                        efb.valid,
                        efb.pair.branch, efb.pair.target);
                cont = false;
            }

        } else {
            DPRINTF(LoopBuffer, "cpc == npc: unaligned branch\n");
            cont = true;
        }
    } else {
        if (efb.valid && cpc == efb.pair.branch) {
            DPRINTF(LoopBuffer, "Forward branch(NT): 0x%x|_>0x%x mismatched"
                    ", expected(T): 0x%x|_>0x%x\n",
                    cpc, npc, efb.pair.branch, efb.pair.target);
            cont = false;

        } else if (cpc == instSupply.end) {
            cont = false;

        } else {
            cont = true;
        }
    }
    if (!cont) {
        instSupply.invalidate();
    }
    return cont;
}

void
LoopBuffer::notifyLastInstSize(Addr fetch_addr, Addr inst_pc, unsigned inst_size)
{

    if (inst_size == 0) {
        DPRINTF(LoopBuffer, "Unaligned 4-byte instruction found, inc offset to %u\n",
                instSupply.offset + 4);
        instSupply.offset += 4;
        return;
    }

    auto &efb = instSupply.expectedForwardBranch;
    if (inst_pc == instSupply.end && fetch_addr + 4 >= inst_pc + inst_size) {
        DPRINTF(LoopBuffer, "Meet the end of loop %#lx, wrap around to %#lx\n",
                inst_pc, instSupply.start);
        instSupply.expectedPC = instSupply.start;
        instSupply.offset = 0;

    } else if (efb.valid && efb.pair.branch == inst_pc
            && fetch_addr + 4 >= inst_pc + inst_size) {
        instSupply.expectedPC = efb.pair.target;
        instSupply.forwardBranchIndex++;

        instSupply.offset += 4;
        DPRINTF(LoopBuffer, "Meet a forward jump %#lx, set offset to %u\n",
                inst_pc, instSupply.offset);
        auto fb_state = table[instSupply.start].forwardBranchState;
        if (fb_state->valid &&
                fb_state->forwardBranches.size() > instSupply.forwardBranchIndex) {
            // switch to next fb
            instSupply.expectedForwardBranch.set(
                    fb_state->forwardBranches[instSupply.forwardBranchIndex]);
        } else {
            // instSupply.expectedForwardBranch.invalidate();
        }

    } else {// not wrapping-around && not jumping forward
        instSupply.expectedPC = inst_pc + inst_size;
        if ((inst_pc + inst_size) % 4 == 0) {
            instSupply.offset += 4;
            DPRINTF(LoopBuffer, "Meet normal inst %#lx, set offset to %u\n",
                    inst_pc, instSupply.offset);
        } else {
            DPRINTF(LoopBuffer, "Meet normal inst %#lx, keep offset to %u, because not used-up\n",
                    inst_pc, instSupply.offset);
        }
    }
}

void
InstSupplyState::invalidate()
{
    valid = false;
}

LoopBuffer *
LoopBufferParams::create() const
{
    return new LoopBuffer(this);
}

