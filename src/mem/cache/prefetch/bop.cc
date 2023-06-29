/**
 * Copyright (c) 2018 Metempsy Technology Consulting
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
 */

#include "mem/cache/prefetch/bop.hh"

#include "debug/BOPPrefetcher.hh"
#include "params/BOPPrefetcher.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Prefetcher, prefetch);
namespace prefetch
{

BOP::BOP(const BOPPrefetcherParams &p)
    : Queued(p),
      scoreMax(p.score_max), roundMax(p.round_max),
      badScore(p.bad_score), rrEntries(p.rr_size),
      tagMask((1 << p.tag_bits) - 1),
      delayQueueEnabled(p.delay_queue_enable),
      delayQueueSize(p.delay_queue_size),
      delayTicks(cyclesToTicks(p.delay_queue_cycles)),
      delayQueueEvent([this]{ delayQueueEventWrapper(); }, name()),
      issuePrefetchRequests(false), bestOffset(1), phaseBestOffset(0),
      bestScore(0), round(0)
{
    if (!isPowerOf2(rrEntries)) {
        fatal("%s: number of RR entries is not power of 2\n", name());
    }
    if (!isPowerOf2(blkSize)) {
        fatal("%s: cache line size is not power of 2\n", name());
    }
    if (p.negative_offsets_enable && !(p.offset_list_size % 2 == 0)) {
        fatal("%s: negative offsets enabled with odd offset list size\n",
              name());
    }

    rrLeft.resize(rrEntries);
    rrRight.resize(rrEntries);

    int16_t default_offset = 16;  // 1kB / 64B = 16, as an complement for SMS.stream
    offsetsList.emplace_back(default_offset, (uint8_t) 0);
    DPRINTF(BOPPrefetcher, "add %d to offset list\n", default_offset);

    offsetsListIterator = offsetsList.begin();
}

void
BOP::delayQueueEventWrapper()
{
    while (!delayQueue.empty() &&
            delayQueue.front().processTick <= curTick())
    {
        Addr addr_x = delayQueue.front().baseAddr;
        insertIntoRR(addr_x, RRWay::Left);
        delayQueue.pop_front();
    }

    // Schedule an event for the next element if there is one
    if (!delayQueue.empty()) {
        schedule(delayQueueEvent, delayQueue.front().processTick);
    }
}

unsigned int
BOP::hash(Addr addr, unsigned int way) const
{
    Addr hash1 = addr >> way;
    Addr hash2 = hash1 >> floorLog2(rrEntries);
    return (hash1 ^ hash2) & (Addr)(rrEntries - 1);
}

void
BOP::insertIntoRR(Addr addr, unsigned int way)
{
    switch (way) {
        case RRWay::Left:
            rrLeft[hash(addr, RRWay::Left)] = addr;
            break;
        case RRWay::Right:
            rrRight[hash(addr, RRWay::Right)] = addr;
            break;
    }
}

void
BOP::insertIntoDelayQueue(Addr x)
{
    if (delayQueue.size() == delayQueueSize) {
        return;
    }

    // Add the address to the delay queue and schedule an event to process
    // it after the specified delay cycles
    Tick process_tick = curTick() + delayTicks;

    delayQueue.push_back(DelayQueueEntry(x, process_tick));

    if (!delayQueueEvent.scheduled()) {
        schedule(delayQueueEvent, process_tick);
    }
}

void
BOP::resetScores()
{
    for (auto& it : offsetsList) {
        it.second = 0;
    }
}

inline Addr
BOP::tag(Addr addr) const
{
    return (addr >> lBlkSize) & tagMask;
}

bool
BOP::testRR(Addr addr) const
{
    for (auto& it : rrLeft) {
        if (it == addr) {
            return true;
        }
    }

    for (auto& it : rrRight) {
        if (it == addr) {
            return true;
        }
    }

    return false;
}

void
BOP::tryAddOffset(int64_t offset, bool late)
{
    if (missCount < 128) {
        return;
    } else {
        missCount = 0;
    }
    DPRINTF(BOPPrefetcher, "Reach %s entry, iter offset: %d\n", __FUNCTION__, offsetsListIterator->calcOffset());
    // dump offsets:
    DPRINTF(BOPPrefetcher, "offsets: ");
    for (const auto& it : offsetsList) {
        DPRINTFR(BOPPrefetcher, "%d*%d ", it.first, it.depth);
    }
    DPRINTFR(BOPPrefetcher, "\n");

    if (offsets.size() >= maxOffsetCount) {
        auto it = offsetsList.begin();
        while (it != offsetsList.end()) {
            if (it->second <= badScore) {
                break;
            }
            it++;
        }
        if (it == offsetsList.end()) {
            // all offsets are good, erase the one before the iterator
            if (offsetsListIterator == offsetsList.begin()) {
                // the iterator is the first element, erase the last one
                DPRINTF(BOPPrefetcher, "erase offset %d from offset list\n", offsetsList.rbegin()->first);
                offsets.erase(offsetsList.rbegin()->first);
                offsetsList.erase(--offsetsList.end());
                
            } else {
                offsets.erase((--offsetsListIterator)->first);
                DPRINTF(BOPPrefetcher, "erase offset %d from offset list\n", offsetsListIterator->first);
                offsetsListIterator = offsetsList.erase(offsetsListIterator);
            }
        } else {
            // erase it from set and list
            DPRINTF(BOPPrefetcher, "erase unused offset %d from offset list\n", it->first);
            offsets.erase(it->first);
            if (it == offsetsListIterator) {
                offsetsListIterator = offsetsList.erase(it);  // update iterator
                if (offsetsListIterator == offsetsList.end()) {
                    offsetsListIterator = offsetsList.begin();
                }
            } else {
                offsetsList.erase(it);
            }
            DPRINTF(BOPPrefetcher, "%s after erase: iter offset: %d\n", __FUNCTION__,
                    offsetsListIterator->calcOffset());
        }
    }

    auto offset_it = offsets.find(offset);
    if (offset_it == offsets.end()) {
        offsets.insert(offset);
        bool found = false;
        for (auto it = offsetsList.begin(); it != offsetsList.end(); it++) {
            if (it == offsetsListIterator) {
                found = true;
            }
        }
        DPRINTF(BOPPrefetcher, "%s mid: iter offset: %d\n", __FUNCTION__, offsetsListIterator->calcOffset());
        assert(found);
        // insert it next to the offsetsListIterator
        auto next_it = std::next(offsetsListIterator);
        offsetsList.emplace(next_it, (int16_t) offset, (uint8_t) 0);
        DPRINTF(BOPPrefetcher, "add %d to offset list\n", offset);

    } else {
        bool found = false;
        for (auto it = offsetsList.begin(); it != offsetsList.end(); it++) {
            if (it->first == offset) {
                found = true;
                if (it->late.calcSaturation() > 0.7) {
                    it->depth++;
                    it->late.reset();
                    DPRINTF(BOPPrefetcher, "Late saturates, offset updated to %d * %d\n", it->first, it->depth);
                } else if (it->late.calcSaturation() < 0.2) {
                    it->depth = std::max(1, it->depth - 1);
                    it->late.reset();
                    DPRINTF(BOPPrefetcher, "Late is few, offset updated to %d * %d\n", it->first, it->depth);
                }
                break;
            } else {
                DPRINTF(BOPPrefetcher, "offset %d != %ld\n", offset, it->first);
            }
        }
        assert(found);
    }
    DPRINTF(BOPPrefetcher, "Reach %s end, iter offset: %d\n", __FUNCTION__, offsetsListIterator->calcOffset());
}

void
BOP::bestOffsetLearning(Addr x, bool late)
{
    DPRINTF(BOPPrefetcher, "Reach %s entry, iter offset: %d\n", __FUNCTION__, offsetsListIterator->calcOffset());
    Addr offset_addr = offsetsListIterator->calcOffset();
    Addr lookup_addr = x - offset_addr;
    DPRINTF(BOPPrefetcher, "%s: offset: %d lookup addr: %#lx\n", __FUNCTION__, offset_addr, lookup_addr);
    // There was a hit in the RR table, increment the score for this offset
    if (testRR(lookup_addr)) {
        DPRINTF(BOPPrefetcher, "Address %#lx found in the RR table\n", x);
        offsetsListIterator->second++;
        if (late) {
            offsetsListIterator->late++;
        } else {
            offsetsListIterator->late--;
        }
        if (offsetsListIterator->second > bestScore) {
            bestScore = (*offsetsListIterator).second;
            phaseBestOffset = offsetsListIterator->calcOffset();
            DPRINTF(BOPPrefetcher, "New best score is %lu, phase best offset is %lu\n", bestScore, phaseBestOffset);
        }
    }

    offsetsListIterator++;

    // All the offsets in the list were visited meaning that a learning
    // phase finished. Check if
    if (offsetsListIterator == offsetsList.end()) {
        offsetsListIterator = offsetsList.begin();
        round++;

        // Check if the best offset must be updated if:
        // (1) One of the scores equals SCORE_MAX
        // (2) The number of rounds equals ROUND_MAX
        if ((bestScore >= scoreMax) || (round == roundMax)) {
            DPRINTF(BOPPrefetcher, "update new score: %d round: %d bop: %d\n",
                    bestScore, round, phaseBestOffset);
            bestOffset = phaseBestOffset;
            round = 0;
            bestScore = 0;
            phaseBestOffset = 0;
            resetScores();
            issuePrefetchRequests = true;
        } else if (bestScore <= badScore) {
            DPRINTF(BOPPrefetcher, "best score %d <= bad score %d\n",
                    bestScore, badScore);
        }
    }
    DPRINTF(BOPPrefetcher, "Reach %s end, iter offset: %d\n", __FUNCTION__, offsetsListIterator->calcOffset());
}

void
BOP::calculatePrefetch(const PrefetchInfo &pfi,
        std::vector<AddrPriority> &addresses, bool late)
{
    Addr addr = blockAddress(pfi.getAddr());
    Addr tag_x = tag(addr);

    DPRINTF(BOPPrefetcher,
            "Train prefetcher with addr %#lx tag %#lx\n", addr, tag_x);

    if (delayQueueEnabled) {
        insertIntoDelayQueue(tag_x);
    } else {
        insertIntoRR(tag_x, RRWay::Left);
    }

    // Go through the nth offset and update the score, the best score and the
    // current best offset if a better one is found
    bestOffsetLearning(tag_x, late);

    // This prefetcher is a degree 1 prefetch, so it will only generate one
    // prefetch at most per access
    if (issuePrefetchRequests) {
        Addr prefetch_addr = addr + (bestOffset << lBlkSize);
        addresses.push_back(AddrPriority(prefetch_addr, 32));
        DPRINTF(BOPPrefetcher,
                "Generated prefetch %#lx offset: %d\n",
                prefetch_addr, bestOffset);
    } else {
        DPRINTF(BOPPrefetcher, "Issue prefetch is false, can't issue\n");
    }

    if (pfi.isCacheMiss()) {
        missCount++;
    }
    DPRINTF(BOPPrefetcher, "Reach %s end, iter offset: %d\n", __FUNCTION__, offsetsListIterator->calcOffset());
}

void
BOP::notifyFill(const PacketPtr& pkt)
{
    // Only insert into the RR right way if it's the pkt is from BOP
    if (!pkt->cmd.fromBOP()) return;

    Addr tag_y = tag(pkt->getAddr());
    DPRINTF(BOPPrefetcher, "Notify fill, addr: %#lx tag: %#lx issuePf: %d\n",
            pkt->getAddr(), tag(pkt->getAddr()), issuePrefetchRequests);

    if (issuePrefetchRequests) {
        insertIntoRR(tag_y - bestOffset, RRWay::Right);
    }
}

} // namespace prefetch
} // namespace gem5
