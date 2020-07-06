
#include "cpu/o3/loop_buffer.hh"
#include "debug/LoopBuffer.hh"
#include "debug/LoopBufferStack.hh"

LoopBuffer::LoopBuffer(LoopBufferParams *params)
    :
        SimObject(params),
        numEntries(params->numEntries),
        _name("LoopBuffer"),
        entrySize(params->entrySize),
        mask(~(entrySize - 1)),
        enable(params->enable),
        loopFiltering(params->loopFiltering)
{
    assert(numEntries >= 2 * evictRange);
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

    if (Debug::LoopBufferStack) {

        DPRINTF(LoopBufferStack, "Inserted PC: 0x%x\n", target);
        for (const auto &ele: rank) {
            DPRINTFR(LoopBufferStack, "PC: 0x%x, used: %u\n",
                    ele->first, ele->second.used);
        }
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
LoopBuffer::setFetched(Addr target)
{
    assert(table.count(target));
    assert(table[target].valid && !table[target].fetched);
    table[target].fetched = true;
}

void
LoopBuffer::probe(Addr branch_pc, Addr target_pc)
{
    if (getBufferedLine(target_pc)) {
        updateControl(target_pc);
        return;
    }
    switch (txn.state){
        case Recorded:
        case Invalid:
        case Aborted:
            if (isBackward(branch_pc, target_pc) &&
                    (branch_pc - target_pc) < entrySize) {
                DPRINTF(LoopBuffer, "Observing 0x%x|__>0x%x\n",
                        branch_pc, target_pc);
                txn.state = LRTxnState::Observing;
                txn.branchPC = branch_pc;
                txn.targetPC = target_pc;
                txn.count = 0;
            }
            break;

        case Observing:
            if (branch_pc == txn.branchPC && target_pc == txn.targetPC) {
                txn.count++;

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
            } else if (isBackward(branch_pc, target_pc) &&
                    (branch_pc - target_pc) < entrySize) {
                txn.branchPC = branch_pc;
                txn.targetPC = target_pc;
                txn.state = Observing;
                txn.count = 0;
                txn.offset = 0;
                DPRINTF(LoopBuffer, "Switch to 0x%x|__>0x%x: %u\n",
                        txn.branchPC, txn.targetPC, txn.count);
            } else {
                DPRINTF(LoopBuffer, "Abort 0x%x|__>0x%x due to another branch\n",
                        txn.branchPC, txn.targetPC);
                txn.state = Aborted;
                txn.count = 0;
                txn.offset = 0;
                txn.expectedPC = 0;
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
LoopBuffer::recordInst(uint8_t *building_inst, Addr pc, unsigned inst_size)
{
    if (txn.state != Recording) {
        return;
    }

    if (pc != txn.expectedPC) {
        DPRINTF(LoopBuffer, "0x%x|__>0x%x: 0x%x does not match expectedPC: 0x%x, abort\n",
                txn.branchPC, txn.targetPC, pc, txn.expectedPC);
        // insn stream is redirected
        txn.state = Aborted;
        txn.count = 0;
        txn.offset = 0;
        txn.expectedPC = 0;
        return;
    }

    DPRINTF(LoopBuffer, "0x%x|__>0x%x: recording 0x%x to 0x%x (offset: %u)\n",
            txn.branchPC, txn.targetPC, pc,
            getPendingEntryTarget(), txn.offset);
    memcpy(getPendingEntryPtr() + txn.offset,
            building_inst, inst_size);

    if (pc == txn.branchPC) {
        DPRINTF(LoopBuffer,
                "0x%x|__>0x%x: completely filled!\n",
                txn.branchPC, txn.targetPC);
        txn.state = Recorded;
        setFetched(txn.targetPC);
        clearPending();
        txn.offset = 0;
        txn.expectedPC = 0;
        table[txn.targetPC].branchPC = txn.branchPC;
        return;
    }
    txn.offset += inst_size;
    txn.expectedPC += inst_size;
}

bool
LoopBuffer::isBackward(Addr branch_pc, Addr target_pc)
{
    return branch_pc > target_pc;
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

LoopBuffer *
LoopBufferParams::create()
{
    return new LoopBuffer(this);
}
