#include "mem/cache/prefetch/sms.hh"

#include "debug/SMSPrefetcher.hh"
#include "mem/cache/prefetch/associative_set_impl.hh"

namespace gem5
{
namespace prefetch
{

SMSPrefetcher::SMSPrefetcher(const SMSPrefetcherParams &p)
    : Queued(p),
      region_size(p.region_size),
      region_blocks(p.region_size / p.block_size),
      act(p.act_entries, p.act_entries, p.act_indexing_policy,
          p.act_replacement_policy, ACTEntry(SatCounter8(2, 1))),
      strideDynDepth(p.stride_dyn_depth),
      stride(p.stride_entries, p.stride_entries, p.stride_indexing_policy,
             p.stride_replacement_policy, StrideEntry(SatCounter8(2, 0))),
      pht(p.pht_assoc, p.pht_entries, p.pht_indexing_policy,
          p.pht_replacement_policy,
          PhtEntry(2 * (region_blocks - 1), SatCounter8(2, 0))),
      potentialHeap(8),
      pfBlockLRUFilter(pfFilterSize),
      pfPageLRUFilter(pfFilterSize),
      bop(dynamic_cast<BOP *>(p.bop)),
      spp(dynamic_cast<SignaturePath *>(p.spp)),
      xsPrefStats(this)
{
    assert(bop);
    assert(isPowerOf2(region_size));
    DPRINTF(SMSPrefetcher, "SMS: region_size: %d region_blocks: %d\n",
            region_size, region_blocks);
}

SMSPrefetcher::XSPrefStats::XSPrefStats(statistics::Group *parent)
    : statistics::Group(parent),
    ADD_STAT(heapRootFound, statistics::units::Count::get(),
             "number of heap root found after squash correction"),
    ADD_STAT(heapRootMiss, statistics::units::Count::get(),
             "number of heap root not found after squash correction")
{
}

void
SMSPrefetcher::calculatePrefetch(const PrefetchInfo &pfi, std::vector<AddrPriority> &addresses, bool late,
                                 PrefetchSourceType pf_source)
{
    bool can_prefetch = !pfi.isWrite() && pfi.hasPC();
    if (!can_prefetch) {
        return;
    }

    Addr pc = pfi.getPC();
    Addr vaddr = pfi.getAddr();
    Addr block_addr = blockAddress(vaddr);

    DPRINTF(SMSPrefetcher, "blk addr: %lx, prefetch source: %i, miss: %i, late: %i, after squash: %i\n", block_addr,
            pf_source, pfi.isCacheMiss(), late, pfi.isReqAfterSquash());

    if (pfi.isReqAfterSquash()) {
        heapSquash(heapEntry);
    }

    if (!pfi.isCacheMiss()) {
        assert(pf_source != PrefetchSourceType::PF_NONE);
    }

    // Addr region_addr = regionAddress(vaddr);
    Addr region_offset = regionOffset(vaddr);
    bool is_active_page = false;
    ACTEntry *act_match_entry = actLookup(pfi, is_active_page);
    if (act_match_entry) {
        bool decr = act_match_entry->decr_mode;
        bool is_cross_region_match = act_match_entry->access_cnt == 0;
        if (is_cross_region_match) {
            act_match_entry->access_cnt = 1;
        }
        DPRINTF(SMSPrefetcher,
                "ACT hit or match: pc:%x addr: %x offset: %d active: %d decr: "
                "%d\n",
                pc, vaddr, region_offset, is_active_page, decr);
        if (is_active_page) {
            // active page
            Addr pf_tgt_addr = decr ? block_addr - act_match_entry->depth * blkSize
                                    : block_addr + act_match_entry->depth * blkSize;  // depth here?
            Addr pf_tgt_region = regionAddress(pf_tgt_addr);
            Addr pf_tgt_offset = regionOffset(pf_tgt_addr);
            DPRINTF(SMSPrefetcher, "tgt addr: %x, offset: %d, current depth: %u, page: %lx\n", pf_tgt_addr,
                    pf_tgt_offset, act_match_entry->depth, pf_tgt_region);
            if (decr) {
                // for (int i = (int)region_blocks - 1; i >= pf_tgt_offset && i >= 0; i--) {
                for (int i = region_blocks - 1; i >= 0; i--) {
                    Addr cur = pf_tgt_region * region_size + i * blkSize;
                    sendPFWithFilter(cur, addresses, i, PrefetchSourceType::SStream);
                    DPRINTF(SMSPrefetcher, "pf addr: %x [%d]\n", cur, i);
                    fatal_if(i < 0, "i < 0\n");
                }
            } else {
                // for (int i = std::max(1, ((int) pf_tgt_offset) - 4); i <= pf_tgt_offset; i++) {
                for (int i = 0; i < region_blocks; i++) {
                    Addr cur = pf_tgt_region * region_size + i * blkSize;
                    sendPFWithFilter(cur, addresses, region_blocks - i, PrefetchSourceType::SStream);
                    DPRINTF(SMSPrefetcher, "pf addr: %x [%d]\n", cur, i);
                }
            }
            pfPageLRUFilter.insert(pf_tgt_region, 0);
        }
    }

    if (pf_source == PrefetchSourceType::SStream) {
        auto it = act.begin();
        while (it != act.end()) {
            ACTEntry *it_entry = &(*it);
            if (late) {
                it_entry->lateConf += 3;
                if (it_entry->lateConf.isSaturated()) {
                    it_entry->depth++;
                    it_entry->lateConf.reset();
                }
            } else if (!pfi.isCacheMiss()) {
                it_entry->lateConf--;
                if ((int)it_entry->lateConf == 0) {
                    it_entry->depth = std::max(1U, (unsigned)it_entry->depth - 1);
                    it_entry->lateConf.reset();
                }
            }

            it++;
        }
        it = act.begin();
        ACTEntry *it_entry = &(*it);
        if (late || !pfi.isCacheMiss()) {
            DPRINTF(SMSPrefetcher, "act entry %lx, late or hit, now depth: %d, lateConf: %d\n",
                    it_entry->getTag(), it_entry->depth, (int)it_entry->lateConf);
        }
    }

    if (pfi.isCacheMiss() || pf_source != PrefetchSourceType::SStream) {
        bool use_bop = pf_source == PrefetchSourceType::HWP_BOP || pfi.isCacheMiss();
        if (use_bop) {
            DPRINTF(SMSPrefetcher, "Do BOP traing/prefetching...\n");
            size_t old_addr_size = addresses.size();
            bop->calculatePrefetch(pfi, addresses, late && pf_source == PrefetchSourceType::HWP_BOP);
            bool covered_by_bop;
            if (addresses.size() > old_addr_size) {
                // BOP hit
                AddrPriority addr = addresses.back();
                addresses.pop_back();
                // Filter
                sendPFWithFilter(addr.addr, addresses, addr.priority, PrefetchSourceType::HWP_BOP);
                covered_by_bop = true;
            }
        }

        bool use_stride =
            pf_source == PrefetchSourceType::SStride || pf_source == PrefetchSourceType::HWP_BOP || pfi.isCacheMiss();
        Addr stride_pf_addr = 0;
        bool covered_by_stride = false;
        bool heap_allocated = false;
        if (use_stride) {
            DPRINTF(SMSPrefetcher, "Do stride lookup...\n");
            covered_by_stride = strideLookup(pfi, addresses, late, stride_pf_addr, pf_source, heap_allocated);
        }

        bool use_heap =
            (pf_source == PrefetchSourceType::HWP_Heap || pfi.isCacheMiss() || pfi.getPC() == heapEntry.pc) &&
            !heap_allocated;
        use_heap = false;
        bool covered_by_heap = false;
        if (use_heap) {
            DPRINTF(SMSPrefetcher, "Do heap lookup...\n");
            covered_by_heap = heapLookup(pfi, addresses, late, stride_pf_addr, pf_source, heapEntry);
        }

        bool use_pht = pf_source == PrefetchSourceType::SPP || pf_source == PrefetchSourceType::SPht ||
                       pf_source == PrefetchSourceType::SStride || pfi.isCacheMiss();
        bool trigger_pht = false;
        // stride_pf_addr = 0;
        if (use_pht) {
            DPRINTF(SMSPrefetcher, "Do PHT lookup...\n");
            bool trigger_pht = phtLookup(pfi, addresses, late && pf_source == PrefetchSourceType::SPht, stride_pf_addr);
        }

        bool use_spp = false;
        if (!pfi.isCacheMiss()) {
            if (pf_source == PrefetchSourceType::SPP) {
                use_spp = true;
            }
        } else {
            // cache miss
            if (late) {
                if (pf_source != PrefetchSourceType::SStride && pf_source != PrefetchSourceType::HWP_BOP) {
                    // other components cannot adjust depth
                    use_spp = true;
                }
            } else {  // no prefetch issued
                if (!covered_by_stride) {
                    use_spp = true;
                }
            }
        }
        use_spp = false;
        if (use_spp) {
            int32_t spp_best_offset = 0;
            bool coverd_by_spp = spp->calculatePrefetch(pfi, addresses, pfBlockLRUFilter, spp_best_offset);
            if (coverd_by_spp && spp_best_offset != 0) {
                // TODO: Let BOP to adjust depth by itself
                bop->tryAddOffset(spp_best_offset, late);
            }
        }
    }
}

SMSPrefetcher::ACTEntry *
SMSPrefetcher::actLookup(const PrefetchInfo &pfi, bool &in_active_page)
{
    Addr pc = pfi.getPC();
    Addr vaddr = pfi.getAddr();
    Addr region_addr = regionAddress(vaddr);
    Addr region_offset = regionOffset(vaddr);
    bool secure = pfi.isSecure();

    ACTEntry *entry = act.findEntry(region_addr, secure);
    if (entry) {
        // act hit
        act.accessEntry(entry);
        in_active_page = entry->in_active_page();
        uint64_t region_bit_accessed = 1 << region_offset;
        if (!(entry->region_bits & region_bit_accessed)) {
            entry->access_cnt += 1;
        }
        entry->region_bits |= region_bit_accessed;
        return entry;
    }

    ACTEntry *old_entry = act.findEntry(region_addr - 1, secure);
    if (old_entry) {
        in_active_page = old_entry->in_active_page();
        // act miss, but cur_region - 1 = entry_region, => cur_region =
        // entry_region + 1
        entry = act.findVictim(0);
        // evict victim entry to pht
        updatePht(entry);
        // alloc new act entry
        entry->pc = pc;
        entry->is_secure = secure;
        entry->decr_mode = false;
        entry->region_bits = 1 << region_offset;
        entry->access_cnt = 0;
        entry->region_offset = region_offset;
        entry->lateConf = old_entry->lateConf;
        entry->depth = old_entry->depth;
        DPRINTF(SMSPrefetcher, "act miss, but cur_region - 1 = entry_region, copy depth = %u, lateConf = %i\n",
                entry->depth, (int) entry->lateConf);
        act.insertEntry(region_addr, secure, entry);
        return entry;
    }

    old_entry = act.findEntry(region_addr + 1, secure);
    if (old_entry) {
        in_active_page = old_entry->in_active_page();
        // act miss, but cur_region + 1 = entry_region, => cur_region =
        // entry_region - 1
        entry = act.findVictim(0);
        // evict victim entry to pht
        updatePht(entry);
        // alloc new act entry
        entry->pc = pc;
        entry->is_secure = secure;
        entry->decr_mode = true;
        entry->region_bits = 1 << region_offset;
        entry->access_cnt = 0;
        entry->region_offset = region_offset;
        entry->lateConf = old_entry->lateConf;
        entry->depth = old_entry->depth;
        DPRINTF(SMSPrefetcher, "act miss, but cur_region + 1 = entry_region, copy depth = %u, lateConf = %i\n",
                entry->depth, (int) entry->lateConf);
        act.insertEntry(region_addr, secure, entry);
        return entry;
    }

    // no matched entry, alloc new entry
    entry = act.findVictim(0);
    updatePht(entry);
    entry->pc = pc;
    entry->is_secure = secure;
    entry->decr_mode = false;
    entry->region_bits = 1 << region_offset;
    entry->access_cnt = 1;
    entry->region_offset = region_offset;
    act.insertEntry(region_addr, secure, entry);
    return nullptr;
}

bool
SMSPrefetcher::strideLookup(const PrefetchInfo &pfi, std::vector<AddrPriority> &addresses, bool late, Addr &stride_pf,
                            PrefetchSourceType last_pf_source, bool &heap_allocated)
{
    Addr lookupAddr = pfi.getAddr();
    StrideEntry *entry = stride.findEntry(pfi.getPC(), pfi.isSecure());
    // TODO: add DPRINFT for stride
    DPRINTF(SMSPrefetcher, "Stride lookup: pc:%x addr: %x\n", pfi.getPC(),
            lookupAddr);
    bool should_cover = false;
    if (entry) {
        stride.accessEntry(entry);
        int64_t new_stride = lookupAddr - entry->last_addr;
        if (new_stride == 0) {
            DPRINTF(SMSPrefetcher, "Stride = 0, ignore redundant req\n");
            return false;
        }
        bool stride_match = new_stride == entry->stride ||
                            (entry->stride > 64 && new_stride % entry->stride == 0 && new_stride / entry->stride <= 4);
        DPRINTF(SMSPrefetcher, "Stride hit, with stride: %ld(%lx), old stride: %ld(%lx)\n", new_stride, new_stride,
                entry->stride, entry->stride);

        if (stride_match) {
            if (entry->stride > 512 && new_stride == entry->stride * 2 && entry->conf == 0) {
                if (potentialHeap.contains(pfi.getPC())) {
                    auto counter = potentialHeap.get((Addr)pfi.getPC()).get();
                    ++(*counter);
                    DPRINTF(SMSPrefetcher, "Heap pattern conf = %i\n", (int)*counter);
                    if (counter->isSaturated()) {
                        if (heapEntry.pc != pfi.getPC()) {
                            DPRINTF(SMSPrefetcher, "Heap pattern detected, send it to heap prefetcher\n");
                            heap_allocated = allocHeapEntry(pfi, addresses);
                        } else {
                            DPRINTF(SMSPrefetcher, "Heap active, ignore\n");
                        }
                    }
                } else {
                    potentialHeap.insert(pfi.getPC(), std::make_shared<SatCounter8>(3, 2));
                }
                DPRINTF(SMSPrefetcher, "Stride unmatch, but doubled, maybe heap pattern, sat: %i\n",
                        (int) *potentialHeap.get(pfi.getPC()).get());
            }

            entry->conf++;
            if (strideDynDepth) {
                if (!pfi.isCacheMiss() && last_pf_source == PrefetchSourceType::SStride) {  // stride pref hit
                    entry->lateConf--;
                } else if (late) {  // stride pf late or other prefetcher late
                    entry->lateConf += 3;
                }
                if (entry->lateConf.isSaturated()) {
                    entry->depth++;
                    entry->lateConf.reset();
                } else if ((uint8_t)entry->lateConf == 0) {
                    entry->depth = std::max(1, entry->depth - 1);
                    entry->lateConf.reset();
                }
            }
            DPRINTF(SMSPrefetcher, "Stride match, inc conf to %d, late: %i, late sat:%i, depth: %i\n",
                    (int)entry->conf, late, (uint8_t)entry->lateConf, entry->depth);
            entry->last_addr = lookupAddr;

        } else if (labs(entry->stride) > 64L && labs(new_stride) < 64L) {
            // different stride, but in the same cache line
            DPRINTF(SMSPrefetcher, "Stride unmatch, but access goes to the same line, ignore\n");

        } else {
            entry->conf--;
            entry->last_addr = lookupAddr;
            DPRINTF(SMSPrefetcher, "Stride unmatch, dec conf to %d\n", (int) entry->conf);
            if ((int) entry->conf == 0) {
                DPRINTF(SMSPrefetcher, "Stride conf = 0, reset stride to %ld\n", new_stride);
                entry->stride = new_stride;
                entry->depth = 1;
                entry->lateConf.reset();
            }
        }
        if (entry->conf >= 2) {
            // if miss send 1*stride ~ depth*stride, else send depth*stride
            unsigned start_depth = pfi.isCacheMiss() ? std::max(1, (entry->depth - 4)) : entry->depth;
            Addr pf_addr = 0;
            for (unsigned i = start_depth; i <= entry->depth; i++) {
                pf_addr = lookupAddr + entry->stride * i;
                DPRINTF(SMSPrefetcher, "Stride conf >= 2, send pf: %x with depth %i\n", pf_addr, i);
                sendPFWithFilter(pf_addr, addresses, 0, PrefetchSourceType::SStride);
            }
            if (!pfi.isCacheMiss()) {
                stride_pf = pf_addr;
            }
            should_cover = true;
        }
    } else {
        DPRINTF(SMSPrefetcher, "Stride miss, insert it\n");
        entry = stride.findVictim(0);
        DPRINTF(SMSPrefetcher, "Stride found victim pc = %x, stride = %i\n", entry->pc, entry->stride);
        if (entry->conf >= 2 && entry->stride > 1024) { // > 1k
            DPRINTF(SMSPrefetcher, "Stride Evicting a useful stride, send it to BOP with offset %i\n",
                    entry->stride / 64);
            bop->tryAddOffset(entry->stride / 64);
        }
        entry->conf.reset();
        entry->last_addr = lookupAddr;
        entry->stride = 0;
        entry->depth = 1;
        entry->lateConf.reset();
        entry->pc = pfi.getPC();
        DPRINTF(SMSPrefetcher, "Stride miss, insert with stride 0\n");
        stride.insertEntry(pfi.getPC(), pfi.isSecure(), entry);
    }
    periodStrideDepthDown();
    return should_cover;
}

void
SMSPrefetcher::periodStrideDepthDown()
{
    if (depthDownCounter < depthDownPeriod) {
        depthDownCounter++;
    } else {
        for (StrideEntry &entry : stride) {
            if (entry.conf >= 2) {
                entry.depth = std::max(entry.depth - 1, 1);
            }
        }
        depthDownCounter = 0;
    }
}

void
SMSPrefetcher::updatePht(SMSPrefetcher::ACTEntry *act_entry)
{
    if (!act_entry->region_bits) {
        return;
    }
    PhtEntry *pht_entry = pht.findEntry(act_entry->pc, act_entry->is_secure);
    bool is_update = pht_entry != nullptr;
    if (!pht_entry) {
        pht_entry = pht.findVictim(act_entry->pc);
        for (uint8_t i = 0; i < 2 * (region_blocks - 1); i++) {
            pht_entry->hist[i].reset();
        }
    } else {
        pht.accessEntry(pht_entry);
    }
    Addr region_offset = act_entry->region_offset;
    // incr part
    for (uint8_t i = region_offset + 1, j = 0; i < region_blocks; i++, j++) {
        uint8_t hist_idx = j + (region_blocks - 1);
        bool accessed = (act_entry->region_bits >> i) & 1;
        if (accessed) {
            pht_entry->hist[hist_idx]++;
        } else {
            pht_entry->hist[hist_idx]--;
        }
    }
    // decr part
    for (int i = int(region_offset) - 1, j = region_blocks - 2; i >= 0;
         i--, j--) {
        bool accessed = (act_entry->region_bits >> i) & 1;
        if (accessed) {
            pht_entry->hist[j]++;
        } else {
            pht_entry->hist[j]--;
        }
    }
    if (!is_update) {
        pht.insertEntry(act_entry->pc, act_entry->is_secure, pht_entry);
    }
}
bool
SMSPrefetcher::phtLookup(const Base::PrefetchInfo &pfi, std::vector<AddrPriority> &addresses, bool late,
                         Addr look_ahead_addr)
{
    Addr pc = pfi.getPC();
    Addr vaddr = look_ahead_addr ? look_ahead_addr : pfi.getAddr();
    Addr blk_addr = blockAddress(vaddr);
    // Addr region_addr = regionAddress(vaddr);
    Addr region_offset = regionOffset(vaddr);
    bool secure = pfi.isSecure();
    PhtEntry *pht_entry = pht.findEntry(pc, secure);
    bool found = false;
    if (pht_entry) {
        pht.accessEntry(pht_entry);
        DPRINTF(SMSPrefetcher, "Pht lookup hit: pc: %x, vaddr: %x (%s), offset: %x, late: %i\n", pc, vaddr,
                look_ahead_addr ? "ahead" : "current", region_offset, late);
        int priority = 2 * (region_blocks - 1);
        // find incr pattern
        for (uint8_t i = 0; i < region_blocks - 1; i++) {
            if (pht_entry->hist[i + region_blocks - 1].calcSaturation() > 0.5) {
                Addr pf_tgt_addr = blk_addr + (i + 1) * blkSize;
                sendPFWithFilter(pf_tgt_addr, addresses, priority--, PrefetchSourceType::SPht);
                found = true;
            }
        }
        for (int i = region_blocks - 2, j = 1; i >= 0; i--, j++) {
            if (pht_entry->hist[i].calcSaturation() > 0.5) {
                Addr pf_tgt_addr = blk_addr - j * blkSize;
                sendPFWithFilter(pf_tgt_addr, addresses, priority--, PrefetchSourceType::SPht);
                found = true;
            }
        }
        DPRINTF(SMSPrefetcher, "pht entry pattern:\n");
        for (uint8_t i = 0; i < 2 * (region_blocks - 1); i++) {
            DPRINTFR(SMSPrefetcher, "%.2f ", pht_entry->hist[i].calcSaturation());
            if (i == region_blocks - 1) {
                DPRINTFR(SMSPrefetcher, "| ");
            }
        }
        DPRINTFR(SMSPrefetcher, "\n");

        if (late) {
            int period = calcPeriod(pht_entry->hist, late);
        }
    }
    return found;
}

int
SMSPrefetcher::calcPeriod(const std::vector<SatCounter8> &bit_vec, bool late)
{
    std::vector<int> bit_vec_full(2 * (region_blocks - 1) + 1);
    // copy bit_vec to bit_vec_full, with mid point = 1
    for (int i = 0; i < region_blocks - 1; i++) {
        bit_vec_full.at(i) = bit_vec.at(i).calcSaturation() > 0.5;
    }
    bit_vec_full[region_blocks - 1] = 1;
    for (int i = region_blocks, j = region_blocks - 1; i < 2 * (region_blocks - 1);
         i++, j++) {
        bit_vec_full.at(i) = bit_vec.at(j).calcSaturation() > 0.5;
    }

    DPRINTF(SMSPrefetcher, "bit_vec_full: ");
    for (int i = 0; i < 2 * (region_blocks - 1) + 1; i++) {
        DPRINTFR(SMSPrefetcher, "%i ", bit_vec_full[i]);
    }
    DPRINTFR(SMSPrefetcher, "\n");

    int max_dot_prod = 0;
    int max_shamt = -1;
    for (int shamt = 2; shamt < 2 * (region_blocks - 1) + 1; shamt++) {
        int dot_prod = 0;
        for (int i = 0; i < 2 * (region_blocks - 1) + 1; i++) {
            if (i + shamt < 2 * (region_blocks - 1) + 1) {
                dot_prod += bit_vec_full[i] * bit_vec_full[i + shamt];
            }
        }
        if (dot_prod >= max_dot_prod) {
            max_dot_prod = dot_prod;
            max_shamt = shamt;
        }
    }
    DPRINTF(SMSPrefetcher, "max_dot_prod: %i, max_shamt: %i\n", max_dot_prod,
            max_shamt);
    if (max_dot_prod > 0 && max_shamt > 3) {
        bop->tryAddOffset(max_shamt, late);
    }
    return max_shamt;
}

bool
SMSPrefetcher::sendPFWithFilter(Addr addr, std::vector<AddrPriority> &addresses, int prio, PrefetchSourceType src)
{
    if (pfPageLRUFilter.contains(regionAddress(addr))) {
        DPRINTF(SMSPrefetcher, "Skip recently prefetched page: %lx\n", regionAddress(addr));
        return false;
    } else if (pfBlockLRUFilter.contains(addr)) {
        DPRINTF(SMSPrefetcher, "Skip recently prefetched: %lx\n", addr);
        return false;
    } else {
        DPRINTF(SMSPrefetcher, "Send pf: %lx\n", addr);
        pfBlockLRUFilter.insert(addr, 0);
        addresses.push_back(AddrPriority(addr, prio, src));
        return true;
    }
}

void
SMSPrefetcher::notifyFill(const PacketPtr &pkt)
{
    bop->notifyFill(pkt);
}

void
SMSPrefetcher::heapSquash(HeapEntry &entry)
{
    DPRINTF(SMSPrefetcher, "Heap squash mark\n");
    entry.state = HeapSquashed;
    entry.curStride = 0;
    entry.parentAddr = 0;
    entry.curAddr = 0;
}

bool
SMSPrefetcher::heapLookup(const PrefetchInfo &pfi, std::vector<AddrPriority> &address_list, bool late, Addr &pf_addr,
                          PrefetchSourceType src, HeapEntry &entry)
{
    // 1. active: addr in region
    // 1. new region: pc match
    if ((entry.state == HeapNewRegion || entry.state == HeapSquashed ) && pfi.getPC() != entry.pc) {
        return false;
    }
    DPRINTF(SMSPrefetcher, "Heap lookup: pc: %x, addr: %x\n", pfi.getPC(), pfi.getAddr());

    if (entry.state == HeapSquashed) {

        // Search addr in current prefetch tree

        const Addr oor_threshold = blkSize * 128;
        bool just_squash = false;
        int inferred_row = -1;
        Addr addr = pfi.getAddr();
        for (unsigned row = 0; row < entry.pfLevels; row++) {
            auto &r = entry.footprints[row];
            if (addr < r.start) {
                if (row == 0) {
                    just_squash = true;
                } else {
                    if (labs(addr - entry.footprints[row - 1].mid) < oor_threshold) {
                        inferred_row = row - 1;
                        break;
                    } else if (labs(r.mid - addr) < oor_threshold) {
                        inferred_row = row;
                        break;
                    } else {
                        just_squash = true;
                    }
                    // Infer that only current row and below are squashed, take mid of last level as heap root
                }
            }
        }
        if (inferred_row <= 0 || entry.footprints[inferred_row - 1].accessAddr == 0) {
            just_squash = true;
            xsPrefStats.heapRootMiss++;
        } else {
            xsPrefStats.heapRootFound++;
        }
        DPRINTF(SMSPrefetcher, "Heap lookup: just squash: %i, inferred row: %i\n", just_squash, inferred_row);
        if (just_squash) {
            // No older info, just squash and treate it as heap root
            entry.state = HeapNewRegion;
            entry.curAddr = addr;
            DPRINTF(SMSPrefetcher, "Heap lookup: squash and treat as new heap root\n");

        } else if (inferred_row != -1) {
            entry.state = HeapActive;
            entry.curAddr = pfi.getAddr();
            entry.parentAddr = entry.footprints[inferred_row - 1].accessAddr;
            entry.footprints[inferred_row].accessAddr = pfi.getAddr();
            entry.curStride = entry.curAddr - entry.parentAddr;
            entry.hitCount = 0;
            entry.outCount = 0;
            DPRINTF(SMSPrefetcher, "Heap lookup: newly inferred root: %lx, stride: %li\n", entry.parentAddr,
                    entry.curStride);
            calcHeapPrefAndUpdateEntry(pfi, entry, address_list, true, inferred_row);
            assert(entry.parentAddr != 0);
        }

    } else if (entry.state == HeapNewRegion) {
        // calculate stride
        int64_t stride = (int64_t)pfi.getAddr() - (int64_t)entry.curAddr;
        if (stride > 256) {
            DPRINTF(SMSPrefetcher, "Heap stride = %lu, become active\n", stride);
            entry.state = HeapActive;
            entry.curStride = stride;
            entry.parentAddr = entry.curAddr;
            entry.curAddr = pfi.getAddr();
            entry.hitCount = 0;
            entry.outCount = 0;
            calcHeapPrefAndUpdateEntry(pfi, entry, address_list, true, 0);
        } else {
            DPRINTF(SMSPrefetcher, "Heap stride = %li, switch to new region\n", stride);
            entry.state = HeapNewRegion;
            entry.curAddr = pfi.getAddr();
        }
    } else {
        assert(entry.state == HeapActive);

        // calculate stride
        int64_t stride = (int64_t)pfi.getAddr() - (int64_t)entry.curAddr;
        bool still_in_cur_region = false;
        bool still_in_track = false;
        bool skip = false;
        for (unsigned i = 0; i < entry.pfLevels; i++) {
            auto &r = entry.footprints[i];
            DPRINTF(SMSPrefetcher, "Heap lookup: level %i, range: %lx ~ %lx, last acc: %lx\n", i, r.start, r.end,
                    r.accessAddr);
            if (r.fallInSlackRange(pfi.getAddr())) {
                auto offset = r.bitOffset(pfi.getAddr(), blkSize);
                r.accessAddr = pfi.getAddr();
                bool fall_in_strict_range = r.fallInRange(pfi.getAddr());
                if (fall_in_strict_range) {
                    r.bitvec.set(offset);
                    r.confVec[offset]++;
                    entry.decayCounter++;
                }
                DPRINTF(SMSPrefetcher, "Heap hit, set bit %i, set access addr to %lx, in stride range: %i\n", offset,
                        pfi.getAddr(), fall_in_strict_range);

                // if (i == 0) {  // same level, mispredicted happens
                //     skip = true;
                //     reCalcRegion(pfi, entry, address_list, false);
                // }
                if (i != entry.pfLevels - 1) {
                    still_in_cur_region = true;
                }
                still_in_track = true;
                break;
            }
        }
        if (entry.decayCounter >= entry.decayPeriod) {
            DPRINTF(SMSPrefetcher, "Peroidically decay sat counter\n");
            for (unsigned i = 0; i < entry.pfLevels; i++) {
                auto &r = entry.footprints[i];
                for (unsigned j = 0; j < HeapPFRegionSize; j++) {
                    r.confVec[j]--;
                }
            }
            entry.decayCounter = 0;
        }
        if (skip) {
        } else if (still_in_track) {
            DPRINTF(SMSPrefetcher, "Still under heap's track\n");
            entry.state = HeapActive;  // keep active
            entry.outCount = 0;
            entry.hitCount++;
            entry.parentAddr = entry.curAddr;
            entry.curAddr = pfi.getAddr();
            entry.curStride = stride;
            if (!still_in_cur_region) {
                DPRINTF(SMSPrefetcher, "Reach last level of track, recalc future levels\n");
                // lv = max level, go to deeper level
                reCalcRegion(pfi, entry, address_list, false);
            }
            printHeapPFMap(entry);

        } else if (pfi.getPC() == entry.pc) {
            DPRINTF(SMSPrefetcher, "Out of heap's track, hit count = %u, out count = %u\n", entry.hitCount,
                    entry.outCount);
            printHeapPFMap(entry);
            entry.state = HeapNewRegion;
            entry.curAddr = pfi.getAddr();
            DPRINTF(SMSPrefetcher, "Mark it as enter new region\n");
        }
    }
    return false;
}

void
SMSPrefetcher::printHeapPFMap(HeapEntry &entry)
{
    DPRINTF(SMSPrefetcher, "Dump heap pf bitmap:\n");
    for (unsigned i = 0; i < entry.pfLevels; i++) {
        const auto &r = entry.footprints[i];
        for (unsigned j = 0; j < HeapPFRegionSize; j++) {
            if (j == HeapPFRegionSize / 2) {
                DPRINTFR(SMSPrefetcher, "| ");
            }
            char c;
            if (j < HeapPFRegionSize / 2) {
                c = '0' + (HeapPFRegionSize / 2 - j);
            } else {
                c = '0' + (j - HeapPFRegionSize / 2 + 1);
            }
            DPRINTFR(SMSPrefetcher, "%c ", r.bitvec.test(j) ? c : '.');
        }
        DPRINTFR(SMSPrefetcher, "\n");
    }

    DPRINTF(SMSPrefetcher, "Dump heap pf conf counter:\n");
    for (unsigned i = 0; i < entry.pfLevels; i++) {
        const auto &r = entry.footprints[i];
        for (unsigned j = 0; j < HeapPFRegionSize; j++) {
            if (j == HeapPFRegionSize / 2) {
                DPRINTFR(SMSPrefetcher, "| ");
            }
            DPRINTFR(SMSPrefetcher, "%i ", (int)r.confVec[j]);
        }
        DPRINTFR(SMSPrefetcher, "\n");
    }
}

bool
SMSPrefetcher::allocHeapEntry(const PrefetchInfo &pfi, std::vector<AddrPriority> &addresses)
{
    if (heapEntry.pc != 0) {
        return false;
    }
    heapEntry.pc = pfi.getPC();
    heapEntry.state = HeapPFState::HeapActive;
    heapEntry.hitCount = 0;
    heapEntry.outCount = 0;
    calcHeapPrefAndUpdateEntry(pfi, heapEntry, addresses, false, 0);
    return true;
}

void
SMSPrefetcher::calcHeapPrefAndUpdateEntry(const PrefetchInfo &pfi, HeapEntry &entry,
                                          std::vector<AddrPriority> &addresses, bool send_pf, unsigned calc_since_row)
{
    Addr cur_addr = pfi.getAddr();
    Addr cur_acc_stride = entry.curStride;

    Addr mid = cur_addr;
    for (unsigned i = calc_since_row; i < entry.pfLevels; i++) {
        auto &r = entry.footprints[i];
        r.start = mid - (HeapPFRegionSize / 2) * blkSize;
        r.end = mid + (HeapPFRegionSize / 2) * blkSize;
        r.mid = mid;
        r.accessAddr = 0;
        r.bitvec.reset();
        mid = mid + cur_acc_stride * 2;
        cur_acc_stride *= 2;
        DPRINTF(SMSPrefetcher, "Update heap pf range row %u: %lx ~ %lx\n", i, r.start, r.end);
    }

    // mark access addr for calc_since_row
    entry.footprints[calc_since_row].accessAddr = cur_addr;

    if (send_pf) {
        printHeapPFMap(entry);
        for (unsigned row = calc_since_row; row < 8; row++) {
            const auto &conf_row = entry.footprints[row - calc_since_row];
            const auto &addr_row = entry.footprints[row];
            DPRINTF(SMSPrefetcher, "Prefetching for row %u\n", row);
            for (int col = 0; col < 16; col++) {
                int left_index = HeapPFRegionSize / 2 - 1 - col;
                int right_index = HeapPFRegionSize / 2 + col;
                if ((int) conf_row.confVec[left_index] > 3) {
                    Addr pf_addr = addr_row.start + left_index * blkSize;
                    sendPFWithFilter(pf_addr, addresses, 16, PrefetchSourceType::HWP_Heap);
                    DPRINTF(SMSPrefetcher, "Heap pf: %lx, logic index: %i, abs index: %i\n", pf_addr, -col,
                            left_index);
                }
                if ((int) conf_row.confVec[right_index] > 5) {
                    Addr pf_addr = addr_row.start + right_index * blkSize;
                    sendPFWithFilter(pf_addr, addresses, 16, PrefetchSourceType::HWP_Heap);
                    DPRINTF(SMSPrefetcher, "Heap pf: %lx, logic index: %i, abs index: %i\n", pf_addr, col,
                            right_index);
                }
            }
        }
    }
}

void
SMSPrefetcher::reCalcRegion(const PrefetchInfo &pfi, HeapEntry &entry, std::vector<AddrPriority> &addresses,
                            bool send_pf)
{
    Addr cur_addr = pfi.getAddr();
    Addr cur_acc_stride = entry.curStride;

    Addr mid = cur_addr;
    for (unsigned i = 0; i < entry.pfLevels; i++) {
        auto &r = entry.footprints[i];
        r.start = mid - (HeapPFRegionSize / 2) * blkSize;
        r.end = mid + (HeapPFRegionSize / 2) * blkSize;
        r.mid = mid;
        r.accessAddr = 0;
        mid = mid + cur_acc_stride * 2;
        cur_acc_stride *= 2;
        DPRINTF(SMSPrefetcher, "Heap pf range updated, lv %u: %lx ~ %lx\n", i, r.start, r.end);
    }

    if (send_pf) {
    }
}

}  // prefetch
}  // gem5
