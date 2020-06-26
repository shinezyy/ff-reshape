
#include "cpu/o3/loop_buffer.hh"
#include "debug/LoopBufferStack.hh"

LoopBuffer::LoopBuffer(LoopBufferParams *params)
    :
        SimObject(params),
        numEntries(params->numEntries),
        _name("LoopBuffer"),
        entrySize(params->entrySize),
        mask(~(entrySize - 1)),
        enable(params->enable)
{
    assert(numEntries >= 2 * evictRange);
}

LoopBuffer::~LoopBuffer()
{
}

void
LoopBuffer::processNewControl(Addr target)
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

LoopBuffer *
LoopBufferParams::create()
{
    return new LoopBuffer(this);
}
