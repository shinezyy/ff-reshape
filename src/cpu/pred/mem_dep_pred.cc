#include "cpu/pred/mem_dep_pred.hh"

#include <algorithm>

#include "base/intmath.hh"
#include "base/random.hh"
#include "debug/NoSQHash.hh"
#include "debug/NoSQPred.hh"
#include "debug/NoSQSMB.hh"

MemDepPredictor::MemDepPredictor(const Params *params)
    : SimObject(params),

    _name("NoSQPredictor"),

    PCTableSize(params->PCTableSize),
    PCTableAssoc(params->PCTableAssoc),
    PCTableDepth(PCTableSize/PCTableAssoc),
    PCTableIndexBits(ceilLog2(PCTableDepth)),
    PCTableIndexMask(((uint64_t)1 << (PCTableIndexBits + pcShamt)) - 1),

    PathTableSize(params->PathTableSize),
    PathTableAssoc(params->PathTableAssoc),
    PathTableDepth(PathTableSize/PathTableAssoc),
    PathTableIndexBits(ceilLog2(PathTableDepth)),
    PathTableIndexMask(((uint64_t)1 << (PathTableIndexBits + pcShamt)) - 1),

    DistanceBits(params->DistanceBits),
    ShamtBits(params->ShamtBits),
    StoreSizeBits(params->StoreSizeBits),
    ConfidenceBits(params->ConfidenceBits),
    TagBits(params->TagBits),
    TagMask(((uint64_t)1 << TagBits) - 1),

    HistoryLen(params->HistoryLen),
    PathMask(((uint64_t)1 << HistoryLen) - 1),

    BranchPathLen(params->BranchPathLen),
    CallPathLen(params->CallPathLen),
    pcTable(PCTableDepth),
    pathTable(PathTableDepth),
    localPredictor(params),
    tssbf(params),
    sssbf(params)
{
}

std::pair<bool, DistancePair>
MemDepPredictor::predict(Addr load_pc, MemPredHistory *&hist)
{
    return predict(load_pc, controlPath, hist);
}

std::pair<bool, DistancePair>
MemDepPredictor::predict(Addr load_pc, FoldedPC path, MemPredHistory *&hist)
{
    auto mp_history = new MemPredHistory;
    hist = mp_history;
    mp_history->willSquash = false;
    mp_history->updatedHistory = false;

    auto pc_index = load_pc;
    auto path_index = genPathKey(load_pc, path);

    bool found;
    bool bypass;
    DistancePair pair;
    std::tie(found, bypass, pair) = localPredictor.predict(load_pc, mp_history);

    MemPredCell *cell;
    std::tie(found, cell) = find(pathTable, path_index, true);

    if (found) { // in path table
        if (!mp_history->localSensitive) {
            mp_history->bypass = cell->conf.read() > 0;
            mp_history->distPair = cell->distPair;
        }
        mp_history->pathSensitive = true;
        mp_history->pathBypass = cell->conf.read() > 0;

        // DPRINTF(NoSQPred, "Found Cell@: %p with path: 0x%lx, index: 0x%lx\n",
        //         cell, path, path_index);
        DPRINTF(NoSQPred, "For load @ 0x%x with path 0x%lx, @ index: %u "
                "path predictor predict %i with confi: %i "
                "to storeDistance %u\n",
                load_pc, path, path_index,
                mp_history->pathBypass, cell->conf.read(),
                cell->distPair.snDistance);

    } else {
        if (!mp_history->localSensitive) {
            mp_history->bypass = false;
        }
        mp_history->pathSensitive = true;
        mp_history->pathBypass = false;
        DPRINTF(NoSQPred, "For load @ 0x%x with path 0x%lx, "
                "path signature not found\n",
                load_pc, path);
    }

    std::tie(found, cell) = find(pcTable, pc_index, false);

    if (found) {
        if (!mp_history->localSensitive && !mp_history->pathSensitive) {
            mp_history->bypass = cell->conf.read() > 0;
            mp_history->distPair = cell->distPair;
        }
        mp_history->pcBypass = cell->conf.read() > 0;

        DPRINTF(NoSQPred, "For load @ 0x%x, @ index: %u "
                "pc predictor predict %i with confi: %i "
                "to storeDistance %u\n",
                load_pc, pc_index,
                mp_history->pcBypass, cell->conf.read(),
                cell->distPair.snDistance);
    } else {
        if (!mp_history->localSensitive && !mp_history->pathSensitive) {
            mp_history->bypass = false;
        }
        mp_history->pcBypass = false;
    }

    DPRINTF(NoSQPred, "For load @ 0x%lx, "
            "overall: bypass: %i, pc: %i, path: 0x%i, path sensitive: %i, store dist: %u, dq dist: %u\n",
            load_pc,
            mp_history->bypass, mp_history->pcBypass, mp_history->pathBypass,
            mp_history->pathSensitive, mp_history->distPair.snDistance,
            mp_history->distPair.dqDistance
            );
    if (mp_history->bypass) {
        assert(mp_history->distPair.dqDistance > 0);
    }

    mp_history->path = path;
    return std::make_pair(mp_history->bypass, mp_history->distPair);
}

void
MemDepPredictor::update(Addr load_pc, bool should_bypass, unsigned sn_dist,
                        unsigned dq_dist, MemPredHistory *&hist)
{
    bool pred_bypass = hist->bypass;
    if (hist->localSensitive) {
        localPredictor.update(load_pc, should_bypass, sn_dist, dq_dist, hist);
    }
    if (!pred_bypass && !should_bypass) {
        // NOTE: check
        DPRINTF(NoSQPred, "For load @ 0x%x, correctly predicted non-bypassing\n",
                load_pc);
        return;

    } else if (pred_bypass && !should_bypass) {
        DPRINTF(NoSQPred, "For load @ 0x%x, mispredicted, should not bypass\n", load_pc);
        misPredTable.record(load_pc, false);

        DPRINTF(NoSQPred, "Dec conf in pc table, ");
        decrement(pcTable, load_pc, true, false);

        auto path_index = genPathKey(load_pc, hist->path);
        DPRINTF(NoSQPred, "Dec conf in path table with path: 0x%lx, ",
                hist->path);
        decrement(pathTable, path_index, true, true);

        if (!hist->localSensitive) {
            localPredictor.recordMispred(load_pc, should_bypass, sn_dist, dq_dist, hist);
        }

    } else {
        if (pred_bypass != should_bypass) {
            misPredTable.record(load_pc, true);
            DPRINTF(NoSQPred, "For load @ 0x%x, mispredicted, should bypass!\n", load_pc);
        } else {
            DPRINTF(NoSQPred, "For load @ 0x%x, correctly predicted bypassing\n",
                    load_pc);
        }

        DPRINTF(NoSQPred, "Inc conf in pc table @ index: %u with sn dist: %i, dq dist: %i\n",
                load_pc, sn_dist, dq_dist);
        increment(pcTable, load_pc, {sn_dist, dq_dist}, false);

        auto key = genPathKey(load_pc, hist->path);
        DPRINTF(NoSQPred, "Inc conf in path table @ index: %u with sn dist: %i, dq dist: %i, path: 0x%lx\n",
                key, sn_dist, dq_dist, hist->path);
        increment(pathTable, key, {sn_dist, dq_dist}, false);

        if (!hist->localSensitive) {
            localPredictor.recordMispred(load_pc, should_bypass, sn_dist, dq_dist, hist);
        }
    }
    delete hist;
    hist = nullptr;
}

void
MemDepPredictor::decrement(Addr pc, FoldedPC path)
{
    panic("Deprecated\n");
}

void
MemDepPredictor::decrement(MemPredTable &table, Addr key, bool alloc, bool isPath)
{
    bool found;
    MemPredCell *cell;
    std::tie(found, cell) = find(table, key, isPath);
    if (found) {
        cell->conf.decrement();
        // DPRINTF(NoSQPred, "Found Cell@: %p\n", cell);
        DPRINTF(NoSQPred, "conf after dec: %i\n", cell->conf.read());

    } else if (alloc) {
        // DPRINTF(NoSQPred, "Dec on allocation\n");
        cell = allocate(table, key, isPath);
        cell->conf.decrement();
        DPRINTF(NoSQPred, "conf after dec: %i\n", cell->conf.read());

        auto [found, cell] = find(table, key, isPath);
        assert(found);
    }
}

void
MemDepPredictor::increment(MemPredTable &table, Addr key,
                           const DistancePair &dist_pair, bool isPath)
{
    bool found;
    MemPredCell *cell;
    std::tie(found, cell) = find(table, key, isPath);
    if (found) {
        // DPRINTF(NoSQPred, "Inc in place\n");
        cell->distPair = dist_pair;
        cell->conf.increment();
        DPRINTF(NoSQPred, "conf after inc: %i\n", cell->conf.read());
    } else {
        // DPRINTF(NoSQPred, "Inc on allocation\n");
        cell = allocate(table, key, isPath);
        cell->distPair = dist_pair;
        cell->conf.increment();
        DPRINTF(NoSQPred, "conf after inc: %i\n", cell->conf.read());

        auto [found, cell] = find(table, key, isPath);
        assert(found);
    }
}


void
MemDepPredictor::squash(MemPredHistory* &hist)
{
    delete hist;
    hist = nullptr;
}

void
MemDepPredictor::recordPath(Addr control_pc, bool is_call, bool pred_taken)
{
    if (is_call) {
        // unsigned mask = ((uint64_t)1 << callShamt) - 1;
        // controlPath = (controlPath << callShamt) | ((control_pc >> pcShamt) & mask);
    } else {
        controlPath = (controlPath << (unsigned) 1) | pred_taken;
    }
}

Addr
MemDepPredictor::genPathKey(Addr pc, FoldedPC path) const
{
    return pc ^ ((path & PathMask) << pcShamt);
}

Addr
MemDepPredictor::extractIndex(Addr key, bool isPath)
{
    unsigned mask = isPath ? PathTableIndexMask : PCTableIndexMask;
    return (key & mask) >> pcShamt;
}

Addr
MemDepPredictor::extractTag(Addr key, bool isPath)
{
    unsigned shamt = isPath ? PathTableIndexBits : PCTableIndexBits;
    shamt += pcShamt;
    return (key >> shamt) & TagMask;
}

MemDepPredictor::FoldedPC
MemDepPredictor::getPath() const
{
    return controlPath;
}


std::pair<bool, MemPredCell *>
MemDepPredictor::find(MemPredTable &table, Addr key, bool isPath)
{
    bool found;
    MemPredCell *cell = nullptr;

    MemPredSet &set = table.at(extractIndex(key, isPath));
    Addr tag = extractTag(key, isPath); // high bits are folded or thrown awary here

    auto it = set.find(tag);
    found = it != set.end();
    if (found) {
        cell = &(it->second);
    }
    return std::make_pair(found, cell);
}

MemPredCell*
MemDepPredictor::allocate(MemPredTable &table, Addr key, bool isPath)
{
    MemPredSet &set = table.at(extractIndex(key, isPath));
    unsigned assoc = isPath ? PathTableAssoc : PCTableAssoc;
    auto tag = extractTag(key, isPath);
    assert(!set.count(tag));

    checkAndRandEvict(set, assoc);

    auto pair = set.emplace(tag, MemPredCell(ConfidenceBits));

    assert(pair.second); // no old equal key found
    assert(set.size() <= assoc);

    bool found;
    MemPredCell *cell;
    std::tie(found, cell) = find(table, key, isPath);
    assert(found);

    return &(pair.first->second);
}

void
MemDepPredictor::clear()
{
    for (auto &set: pcTable) {
        set.clear();
    }
    for (auto &set: pathTable) {
        set.clear();
    }
    controlPath = 0;
}

void MemDepPredictor::commitStore(Addr eff_addr, uint8_t eff_size,
                                  InstSeqNum sn, const BasePointer &position) {
    eff_addr = shiftAddr(eff_addr);
    SSBFCell *cell = tssbf.find(eff_addr);
    if (!cell) {
        DPRINTF(NoSQPred, "Allocating new entry\n");
        cell = tssbf.allocate(eff_addr);
    }
    cell->lastStore = sn;
    cell->lastStorePosition = position;
    cell->predecessorPosition = position;
    cell->offset = eff_addr & tssbf.offsetMask;
    cell->size = eff_size;

    DPRINTF(NoSQPred, "Setting 0x%lx last store SN to %lu with size: %u, offset: %u\n",
            eff_addr, sn, eff_size, cell->offset);

    tssbf.touch(eff_addr);

    InstSeqNum &sssbf_entry = sssbf.find(eff_addr);
    assert(sn > sssbf_entry);
    sssbf_entry = sn;
    sssbf.touch(eff_addr);
}

InstSeqNum MemDepPredictor::lookupAddr(Addr eff_addr) {
    SSBFCell *cell = tssbf.find(eff_addr);
    if (!cell) {
        return 0;
    } else {
        return cell->lastStore;
    }
}

void MemDepPredictor::commitLoad(Addr eff_addr, InstSeqNum sn, BasePointer &position) {
    DPRINTF(NoSQPred, "eff_addr: 0x%lx, debug: %p\n", eff_addr, debug);
    SSBFCell *cell = tssbf.find(eff_addr);
    if (!cell) {
        DPRINTF(NoSQPred, "When committing load producing store is not found\n");
    } else {
        cell->predecessorPosition = position;
    }
}

Addr
MemDepPredictor::shiftAddr(Addr addr)
{
    return addr;
}

bool MemDepPredictor::checkAddr(InstSeqNum load_sn, bool pred_bypass, Addr eff_addr_low,
                                Addr eff_addr_high, uint8_t size, InstSeqNum nvul)
{
    // forward for split access is not supported
    if (eff_addr_high) {
        DPRINTF(NoSQSMB, "Do not support to bypass split access\n");
        return false;
    }

    bool skip_verify = false;
    SSBFCell *cell = tssbf.find(eff_addr_low);

    unsigned offset = eff_addr_low & tssbf.offsetMask;
    DPRINTF(NoSQSMB, "NVul of load [%lu] is %lu, pred bypass: %i\n", load_sn, nvul, pred_bypass);
    if (pred_bypass) {
        if (!cell) {
            skip_verify = false;
            // log
            DPRINTF(NoSQSMB, "Cannot skip because SSBF entry not found\n");

        } else {
            DPRINTF(NoSQSMB, "last store @ 0x%lx is %lu\n", eff_addr_low, cell->lastStore);
            skip_verify = cell->lastStore == nvul && cell->offset == offset && cell->size == size;
            // log
            DPRINTF(NoSQSMB, "recorded offset: %u, offset: %u,"
                             "recorded size: %u, size: %u\n",
                    cell->offset, offset, cell->size, size);
        }
    } else {
        if (!cell) {
            InstSeqNum set_youngest = tssbf.findYoungestInSet(eff_addr_low);
            InstSeqNum sssbf_youngest = sssbf.find(eff_addr_low);
            skip_verify = (set_youngest > 0 && set_youngest <= nvul) ||
                    sssbf_youngest <= nvul;

            // log
            if (skip_verify) {
                DPRINTF(NoSQSMB, "Skip verification because either tssbf youngest (%lu) or sssbf youngest (%lu)"
                                 " in this set is older than nvul(%lu)\n",
                        set_youngest, sssbf_youngest, nvul);
            } else {
                DPRINTF(NoSQSMB, "Cannot Skip verification because tssbf youngest (%lu) and sssbf youngest (%lu)"
                                 " in this set are younger than nvul(%lu)\n",
                        set_youngest, sssbf_youngest, nvul);
                tssbf.dump();
                sssbf.dump();
            }
        } else {
            skip_verify = cell->lastStore > 0 && cell->lastStore <= nvul;

            // log
            DPRINTF(NoSQSMB, "%s with last store @ 0x%lx is %lu\n",
                    skip_verify ? "Skip" : "Dont Skip",
                    eff_addr_low, cell->lastStore);
        }
    }

    return skip_verify;
}

void MemDepPredictor::touchSSBF(Addr eff_addr, InstSeqNum ssn)
{
    auto cell = tssbf.find(eff_addr);
    if (!cell) {
        cell = tssbf.allocate(eff_addr);
        cell->lastStore = ssn;
        cell->size = 0;
    }
}

void MemDepPredictor::completeStore(Addr eff_addr, InstSeqNum ssn)
{
    auto cell = tssbf.find(eff_addr);
    if (cell && cell->lastStore < ssn) {
        cell->lastStore = ssn;
    }
}

void MemDepPredictor::dumpTopMisprediction() const
{
    if (Debug::NoSQPred) {
        misPredTable.dump();
    }
}

void MemDepPredictor::checkSilentViolation(
        InstSeqNum load_sn, Addr load_pc, Addr load_addr, uint8_t load_size,
        SSBFCell *last_store_cell,
        unsigned int sn_dist, unsigned int dq_dist,
        MemPredHistory *&hist)
{
    if (sn_dist != dq_dist * 100) {
        DPRINTF(NoSQPred, "The distance between producer and consumer is not reasonable:"
                          "%lu -> %lu with dq dist: %u\n", last_store_cell->lastStore, load_sn, dq_dist);
        if (hist->localSensitive) {
            recordCorrect(load_pc, hist->bypass, sn_dist, dq_dist, hist);
        }
        return;
    }

    Addr store_start = (load_pc & ~(tssbf.offsetMask)) | last_store_cell->offset;
    Addr store_end = store_start + last_store_cell->size - 1;

    Addr load_start = load_addr;
    Addr load_end = load_addr + load_size - 1;

    if (store_end < load_start || load_end < store_start) {
        DPRINTF(NoSQPred, "Store and load are not intersected: store: 0x%lx with size %u, "
                          "load: 0x%lx with size %u\n",
                          store_start, last_store_cell->size,
                          load_start, load_size);
        recordCorrect(load_pc, false, sn_dist, dq_dist, hist);
        return;
    }

    update(load_pc, true, sn_dist, dq_dist, hist);

}

void
MemDepPredictor::recordCorrect(Addr pc, bool should_bypass, unsigned int sn_dist, unsigned int dq_dist,
                               MemPredHistory *&history)
{
    localPredictor.recordCorrect(pc, should_bypass, sn_dist, dq_dist, history);
}

void MemDepPredictor::squashLoad(Addr pc, MemPredHistory *&hist)
{
    if (!hist || hist->updatedHistory) {
        return;
    }
    if (hist->localSensitive) {
        localPredictor.recordSquash(pc, hist);
    }
    delete hist;
    hist = nullptr;
}

MemDepPredictor *MemDepPredictorParams::create()
{
    return new MemDepPredictor(this);
}

TSSBF::TSSBF(const Params *p)
        : SimObject(p),
          TagBits(p->TSSBFTagBits),
          TagMask((((uint64_t)1) << TagBits) - 1),
          Size(p->TSSBFSize),
          Assoc(p->TSSBFAssoc),
          Depth(Size/Assoc),
          IndexBits(ceilLog2(Depth)),
          IndexMask((((uint64_t)1) << IndexBits) - 1),
          table(Depth),
          tableAccCount(Depth, 0)
{
}

Addr TSSBF::extractIndex(Addr key) const {
    key = key >> addrShamt;
    return key & IndexMask;
}

Addr TSSBF::extractTag(Addr key) const {
    key = key >> addrShamt;
    return (key >> IndexBits) & TagMask;
}

SSBFCell *TSSBF::find(Addr key) {
    // log
    DPRINTF(NoSQPred, "Looking up addr 0x%lx with hash index %lu, tag: %lx\n",
            key, extractIndex(key), extractTag(key));

    auto &set = table.at(extractIndex(key));
    auto tag = extractTag(key);
    auto it = set.find(tag);
    if (it != set.end()) {
        return &it->second;
    } else {
        return nullptr;
    }
}

SSBFCell *TSSBF::allocate(Addr key) {
    // log
    DPRINTF(NoSQPred, "Allocating addr 0x%lx at hash index %lu, tag: %lx\n",
            key, extractIndex(key), extractTag(key));

    auto &set = table.at(extractIndex(key));
    auto tag = extractTag(key);
    assert (set.count(tag) == 0);

    checkAndRandEvictOldest(set);

    auto pair = set.emplace(tag, SSBFCell());

    assert(pair.second); // no old equal key found
    assert(set.size() <= Assoc);

    return &(pair.first->second);
}

void TSSBF::checkAndRandEvictOldest(TSSBF::SSBFSet &set)
{
    if (set.size() >= Assoc) {
        DPRINTF(NoSQPred, "Doing Eviction\n");
        auto it = set.begin(), oldest = set.begin(), e = set.end();
        while (it != e) {
            if (it->second.lastStore < oldest->second.lastStore) {
                oldest = it;
            }
            it++;
        }
        DPRINTF(NoSQPred, "Evicting key: %lu\n", it->first);
        set.erase(oldest);
    }
}

InstSeqNum TSSBF::findYoungestInSet(Addr key)
{
    DPRINTF(NoSQPred, "Looking up addr 0x%lx with hash index 0x%lx, tag: %lx\n",
            key, extractIndex(key), extractTag(key));

    auto &set = table.at(extractIndex(key));
    auto tag = extractTag(key);
    auto non_exist = set.find(tag);
    assert (non_exist == set.end());

    InstSeqNum youngest = 0;
    for (const auto &it: set) {
        if (it.second.lastStore > youngest) {
            youngest = it.second.lastStore;
        }
    }
    return youngest;
}

void TSSBF::touch(Addr key)
{
    auto index = extractIndex(key);
    tableAccCount[index]++;
}

void TSSBF::dump()
{
    DPRINTFR(NoSQHash, "Tagged SSBF:\n");
    for (unsigned i = 0; i < tableAccCount.size(); i++) {
        DPRINTFR(NoSQHash, "0x%x: %lu\n", i, tableAccCount[i]);
    }
}


SimpleSSBF::SimpleSSBF(const Params *p)
        : SimObject(p),
          Size(p->TSSBFSize),
          IndexBits(ceilLog2(p->TSSBFSize)),
          IndexMask((((uint64_t)1) << IndexBits) - 1),
          SSBFTable(Size, 0),
          tableAccCount(Size, 0)
{
}

unsigned SimpleSSBF::hash(Addr key)
{
    return (key & IndexMask) ^ ((key >> IndexBits) & IndexMask) ^
           ((key >> IndexBits*2) & IndexMask);
}

InstSeqNum & SimpleSSBF::find(Addr key)
{
    DPRINTF(NoSQPred, "Looking up addr 0x%lx with hash index 0x%lx\n",
            key, hash(key));
    auto index = hash(key);
    assert(index < SSBFTable.size());
    return SSBFTable[index];
}

void SimpleSSBF::touch(Addr key)
{
    auto index = hash(key);
    tableAccCount[index]++;
}

void SimpleSSBF::dump()
{
    DPRINTFR(NoSQHash, "Untagged SSBF:\n");
    for (unsigned i = 0; i < tableAccCount.size(); i++) {
        DPRINTFR(NoSQHash, "0x%x: %lu\n", i, tableAccCount[i]);
    }
}

void MisPredTable::dump() const
{
    DPRINTFR(NoSQPred, "Mis pred table dump:\n");
    for (const auto &e: misPredRank) {
        DPRINTFR(NoSQPred, "PC: 0x%x, count: %lu, fp: %lu, fn: %lu\n",
                 e.pc, e.count, e.fpCount, e.fnCount);
    }
}

void MisPredTable::record(Addr pc, bool fn)
{
    bool found = misPredTable.count(pc);
    if (!found) {
        misPredRank.push_back({pc, 0, 0, 0});
    }
    auto it = found ? misPredTable[pc] : --misPredRank.end();
    if (!found) {
        misPredTable[pc] = it;
    }
    it->count++;
    if (fn) {
        it->fnCount++;
    } else {
        it->fpCount++;
    }

    if (it != misPredRank.begin()) {
        auto new_position = std::prev(it);
        while (new_position != misPredRank.begin() && new_position->count <= it->count) {
            new_position--;
        }
        if (new_position != misPredRank.begin()) {
            new_position++;
        }
        if (new_position != it) {
            misPredRank.splice(new_position, misPredRank, it, std::next(it));
        }
    }

    if (misPredRank.size() > size) {
        auto to_evict = random_mt.random<int>(1, size/2);
        // random choose one to evict

        auto it_evict = misPredRank.end();
        while (to_evict--) {
            it_evict--;
        }
        misPredTable.erase(it_evict->pc);
        misPredRank.erase(it_evict);
    }
}

// valid, bypass, pair
std::tuple<bool, bool, DistancePair>
LocalPredictor::predict(Addr pc, MemPredHistory *&history)
{
    auto pair = instTable.find(pc);
    if (pair == instTable.end() || !pair->second.active) {
        assert(history);
        history->localSensitive = false;
        if (pair != instTable.end()) {
            history->localHistory = pair->second.history;
        }
        return std::make_tuple(false, false, history->distPair);
    }
    useCount++;
    if (useCount >= resetCount) {
        useCount = 0;
        for (auto &pair: instTable) {
            pair.second.recentUsed = false;
        }
    }
    auto &cell = pair->second;


    // prediction with perceptron:
    if (perceptron) {
        int32_t val = cell.predict();

        history->bypass = val > 0;
        history->predictionValue = val;
    } else {
        // prediction with table:
         unsigned index = extractIndex(pc, cell.history);
         history->bypass = predTable.at(index).read() > 0;
    }

    history->localHistory = cell.history;
    history->distPair = cell.distPair;
    if (!history->distPair.dqDistance) {
        history->bypass = false;
    }
    history->localSensitive = true;
    cell.recentUsed = true;

    if (Debug::NoSQPred) {
        std::cout << "Local history: " << history->localHistory
        << "\n";
    }

    cell.history <<= 1;
    cell.history[0] = history->bypass;
    cell.numSpeculativeBits += 1;
    if (Debug::NoSQPred) {
        std::cout << "History after pred: " << cell.history
                << ", num spec after pred: " << cell.numSpeculativeBits
                << "\n";
    }

    DPRINTF(NoSQPred, "Predicted by pattern predictor, bypass: %i from with sn dist: %u, "
            "dq dist: %u\n",
            history->bypass,
            cell.distPair.snDistance, cell.distPair.dqDistance);
    return std::make_tuple(true, history->bypass, cell.distPair);
}

void LocalPredictor::update(Addr pc, bool should_bypass, unsigned int sn_dist, unsigned int dq_dist,
                            MemPredHistory *&history)
{
    auto pair = instTable.find(pc);
    DPRINTF(NoSQPred, "Pattern entry not found: %i, local sensitive: %i\n",
            pair == instTable.end(), history->localSensitive);
    if (pair == instTable.end() || !history->localSensitive) { // evicted
        recordMispred(pc, should_bypass, sn_dist, dq_dist, history);
        return;
    }
    DPRINTF(NoSQPred, "Updating pattern history entry\n");
    auto &cell = pair->second;
    history->updatedHistory = true;
    if (history->bypass != should_bypass) {
        if (Debug::NoSQPred) {
            std::cout << "History now: " << cell.history
                      << ", num spec: " << cell.numSpeculativeBits
                      << ", should bypass: " << should_bypass
                      << "\n";
        }
        boost::dynamic_bitset<> used_hist(historyLen);
        unsigned index;
        if (!history->willSquash) {
            if (cell.numSpeculativeBits > 0) {
                if (cell.numSpeculativeBits <= historyLen) {
                    cell.history[cell.numSpeculativeBits - 1] = should_bypass;
                    assert(cell.numSpeculativeBits >= 0);
                }
                cell.numSpeculativeBits--;
                DPRINTF(NoSQPred, "Inst[%lu] dec num spec for pc:0x%lx\n", history->inst, pc);
            }

            used_hist = history->localHistory;
            index = extractIndex(pc, used_hist);
        } else {
            if (cell.numSpeculativeBits > 0) {
                if (cell.numSpeculativeBits <= historyLen) {
                    cell.history >>= 1;
                }
                cell.numSpeculativeBits--;
                DPRINTF(NoSQPred, "Inst[%lu] dec num spec for pc:0x%lx\n", history->inst, pc);
            }

            used_hist = history->localHistory;
            index = extractIndex(pc, used_hist);
        }
        if (Debug::NoSQPred) {
            std::cout << "History now: " << cell.history
                      << ", num spec: " << cell.numSpeculativeBits
                      << "\n";
        }

        if (Debug::NoSQPred) {
            std::cout << "Recover history to " << cell.history << "\n";
        }
        SignedSatCounter &counter = predTable.at(index);
        if (history->willSquash) {
            if (should_bypass) {
                counter.increment();
            } else {
                counter.decrement();
            }
            if (perceptron && !history->localHistory.empty()) {
                cell.fit(history, should_bypass);
            }
        }
        if (Debug::NoSQPred) {
            std::cout << "Counter under history " << used_hist << ":"
                      << counter.read() << "\n";
        }

    } else {
        if (Debug::NoSQPred) {
            std::cout << "History remains to be " << cell.history << ", because no mispred\n";
        }
    }
    if (should_bypass && dq_dist) {
        cell.distPair.snDistance = sn_dist;
        cell.distPair.dqDistance = dq_dist;
    }
}

void LocalPredictor::recordMispred(Addr pc, bool should_bypass, unsigned int sn_dist, unsigned int dq_dist,
                                   MemPredHistory *hist)
{
    auto pair = instTable.find(pc);
    if (pair == instTable.end()) {
        if (instTable.size() >= size) {
            auto it = evictOneInst();
            DPRINTF(NoSQPred, "Evicting pc 0x%lx from pattern hist table\n",
                    it->first);
            instTable.erase(it);
        }
    }
    DPRINTF(NoSQPred, "%s reach 1\n", __func__);
    auto &cell = instTable[pc];
    if (should_bypass && dq_dist) {
        cell.distPair.snDistance = sn_dist;
        cell.distPair.dqDistance = dq_dist;
    }
    cell.count++;

    DPRINTF(NoSQPred, "%s reach 2\n", __func__);
    touchCount++;
    if (touchCount >= resetCount) {
        touchCount = 0;
        for (auto &pair: instTable) {
            pair.second.recentTouched = false;
        }
    }
    cell.recentTouched = true;

    DPRINTF(NoSQPred, "%s reach 3\n", __func__);
    if (cell.count > activeThres) {
        cell.active = true;
    }
    if (cell.active) {
        auto index = extractIndex(pc, cell.history);
        SignedSatCounter &counter = predTable.at(index);
        if (should_bypass) {
            counter.increment();
        } else {
            counter.decrement();
        }
        if (perceptron && !hist->localHistory.empty()) {
            cell.fit(hist, should_bypass);
        }
    }
    DPRINTF(NoSQPred, "%s reach 4\n", __func__);
    hist->updatedHistory = true;

    cell.history = cell.history << 1;
    cell.history[0] = should_bypass;
    DPRINTF(NoSQPred, "%s reach 5\n", __func__);
}

unsigned LocalPredictor::extractIndex(Addr pc, const boost::dynamic_bitset<> &hist) const
{
    unsigned index = (hist.to_ulong() ^ pc) & indexMask;
    DPRINTF(NoSQPred, "Pattern index is 0x%x\n", index);
    return index;
}

LocalPredictor::Table::iterator
LocalPredictor::evictOneInst()
{
    auto old_pointer = pointer;
    while (pointer != instTable.end()) {
        if (!(pointer->second.recentTouched || pointer->second.recentUsed)) {
            return pointer++;
        }
        pointer++;
    }
    pointer = instTable.begin();
    while (pointer != old_pointer) {
        if (!(pointer->second.recentTouched || pointer->second.recentUsed)) {
            return pointer++;
        }
        pointer++;
    }
    while (pointer != instTable.end()) {
        if (!(pointer->second.recentUsed || pointer->second.active)) {
            return pointer++;
        }
        pointer++;
    }
    pointer = instTable.begin();
    while (pointer != old_pointer) {
        if (!(pointer->second.recentUsed || pointer->second.active)) {
            return pointer++;
        }
        pointer++;
    }

    return pointer++;
}

void LocalPredictor::clearUseBit()
{
    for (auto &pair: instTable) {
        pair.second.recentUsed = false;
    }
}

void LocalPredictor::clearTouchBit()
{
    for (auto &pair: instTable) {
        pair.second.recentTouched = false;
    }
}

LocalPredictor::LocalPredictor(const LocalPredictor::Params *p)
: SimObject(p),
    predTable(predTableSize, SignedSatCounter(2, 0)),
    _name("MemPatternPredictor")
{
    pointer = instTable.end();
}

void
LocalPredictor::recordCorrect(Addr pc, bool should_bypass, unsigned int sn_dist, unsigned int dq_dist,
                              MemPredHistory *&history)
{
    auto pair = instTable.find(pc);
    if (pair == instTable.end()) {
        return;
    }
    auto &cell = instTable.at(pc);
    if (!cell.active) {
        return;
    }
    history->updatedHistory = true;
    if (Debug::NoSQPred) {
        std::cout << "History now: " << cell.history
                  << ", num spec: " << cell.numSpeculativeBits
                  << ", should bypass: " << should_bypass
                  << "\n";
    }
    assert(cell.numSpeculativeBits > 0 || !history->localSensitive);
//    assert(cell.history[cell.numSpeculativeBits - 1] == should_bypass);
    if (!history->localSensitive) {
        return;
    }
    DPRINTF(NoSQPred, "Correctly predicted on history: ");
    if (Debug::NoSQPred) {
        std::cout << (cell.history >> cell.numSpeculativeBits) << "\n";
        std::cout << "History now: " << cell.history << "\n";
    }
    cell.numSpeculativeBits--;
    DPRINTF(NoSQPred, "Inst[%lu] dec num spec for pc:0x%lx\n", history->inst, pc);
    assert(cell.numSpeculativeBits >= 0);
}

void LocalPredictor::recordSquash(Addr pc, MemPredHistory *&history)
{
    auto pair = instTable.find(pc);
    if (pair == instTable.end()) {
        return;
    }
    auto &cell = instTable[pc];
    if (!cell.active || !cell.numSpeculativeBits) {
        return;
    }
    DPRINTF(NoSQPred, "Squashing load with pc: 0x%lx\n", pc);
    if (Debug::NoSQPred) {
        std::cout << "History now: " << cell.history
                  << ", num spec: " << cell.numSpeculativeBits
                  << "\n";
    }
    cell.history >>= 1;
    cell.numSpeculativeBits--;
    DPRINTF(NoSQPred, "Inst[%lu] dec num spec for pc:0x%lx\n", history->inst, pc);
    assert(cell.numSpeculativeBits >= 0);
    if (Debug::NoSQPred) {
        std::cout << "History after squash: " << cell.history
                  << ", num spec: " << cell.numSpeculativeBits
                  << "\n";
    }
}

int LocalPredCell::b2s(bool bypass)
{
    // 1 -> 1; 0 -> -1
    return ((int) bypass << 1) - 1;
}

void LocalPredCell::fit(MemPredHistory *hist, bool should_bypass)
{
    if (should_bypass == hist->bypass &&
            abs(hist->predictionValue) > theta) {
        return;
    }
    if (should_bypass) {
        weights.back().increment();
    } else {
        weights.back().decrement();
    }

    const auto &hist_bits = hist->localHistory;

    if (Debug::NoSQPred) {
        std::cout << "Local history: " << hist->localHistory
                  << "\n";
    }
    for (int i = 0; i < historyLen; i++) {
        weights[i].add(b2s(should_bypass) * b2s(hist_bits[i]));
    }
}

int32_t LocalPredCell::predict()
{
    int32_t sum = weights.back().read(); // bias
    for (int i = 0; i < historyLen; i++) {
        sum += b2s(history[i]) * weights[i].read();
    }
    return sum;
}
