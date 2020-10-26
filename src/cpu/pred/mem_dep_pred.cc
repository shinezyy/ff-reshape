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
    meta(params),
    tssbf(params),
    sssbf(params)
{
    nullTermedPointer.valid = false;
}

void
MemDepPredictor::predict(Addr load_pc, MemPredHistory &hist)
{
    predict(load_pc, controlPath, hist);
}

void
MemDepPredictor::predict(Addr load_pc, FoldedPC path, MemPredHistory &hist)
{
    pcPredict(hist.pcInfo, load_pc);
    pathPredict(hist.pathInfo, load_pc, path);
    patternPredict(hist.patternInfo, load_pc);

    MetaPredictor::WhichPredictor which = meta.choose(load_pc);

    if (hist.pathInfo.valid && abs(hist.pathInfo.confidence) > pathConfThres) {
        DPRINTF(NoSQPred, "Choosing path pred results because of high conf: %i\n", hist.pathInfo.confidence);
        hist.bypass = hist.pathInfo.bypass;
        hist.distPair = hist.pathInfo.distPair;
    } else {
        if (which == MetaPredictor::UsePattern && hist.patternInfo.valid) {
            DPRINTF(NoSQPred, "Choosing pattern pred results because of meta\n");
            hist.bypass = hist.patternInfo.bypass;
            hist.distPair = hist.patternInfo.distPair;

        } else if (hist.pathInfo.valid) {
            DPRINTF(NoSQPred, "Choosing path pred results because of meta\n");
            hist.bypass = hist.pathInfo.bypass;
            hist.distPair = hist.pathInfo.distPair;

        } else if (which == MetaPredictor::UsePC && hist.pcInfo.valid) {
            DPRINTF(NoSQPred, "Choosing pc pred results because of meta\n");
            hist.bypass = hist.pcInfo.bypass;
            hist.distPair = hist.pcInfo.distPair;
        } else {
            DPRINTF(NoSQPred, "Do not bypass because no valid prediction\n");
            hist.bypass = false;
        }
    }


    DPRINTF(NoSQPred, "For load @ 0x%lx, "
            "overall: bypass: %i, store dist: %u, dq dist: %u\n",
            load_pc,
            hist.bypass,
            hist.distPair.ssnDistance, hist.distPair.dqDistance
            );
}

void
MemDepPredictor::update(Addr load_pc, bool should_bypass, unsigned sn_dist,
                        unsigned dq_dist, MemPredHistory &hist)
{
    bool pred_bypass = hist.bypass;

    if (should_bypass == pred_bypass) {
        updatePredictorsOnCorrect(load_pc, should_bypass, sn_dist, dq_dist, hist);
        return;
    }
    if (hist.updated) {
        DPRINTF(NoSQPred, "history has been recorded, return\n");
        return;
    }
    hist.updated = true;
    // for miss
    meta.record(load_pc, should_bypass, hist.pcInfo.bypass,
                hist.pathInfo.bypass, hist.patternInfo.bypass,
                hist.patternInfo.valid, hist.pathInfo.valid, hist.willSquash);

    // All miss predictions arrive here
    localPredictor.updateOnMiss(load_pc, should_bypass, sn_dist, dq_dist, hist);
    misPredTable.record(load_pc, true);
    if (!should_bypass) {
        DPRINTF(NoSQPred, "Dec conf in pc table, ");
        decrement(pcTable, load_pc, true, false, load_pc);

        auto path_index = genPathKey(load_pc, hist.pathInfo.path);
        DPRINTF(NoSQPred, "Dec conf in path table with path: 0x%lx, index: %lu ",
                hist.pathInfo.path, path_index);
        decrement(pathTable, path_index, true, true, load_pc);

    } else {
        DPRINTF(NoSQPred, "Inc conf in pc table @ index: %u with sn dist: %i, dq dist: %i\n",
                load_pc, sn_dist, dq_dist);
        increment(pcTable, load_pc, {sn_dist, dq_dist}, false, load_pc);

        auto key = genPathKey(load_pc, hist.pathInfo.path);
        DPRINTF(NoSQPred, "Inc conf in path table @ index: %u with sn dist: %i, dq dist: %i, path: 0x%lx\n",
                key, sn_dist, dq_dist, hist.pathInfo.path);
        increment(pathTable, key, {sn_dist, dq_dist}, true, load_pc);
    }
}

void
MemDepPredictor::decrement(Addr pc, FoldedPC path)
{
    panic("Deprecated\n");
}

void
MemDepPredictor::decrement(MemPredTable &table, Addr key, bool alloc, bool isPath, Addr pc)
{
    bool found;
    MemPredCell *cell;
    std::tie(found, cell) = find(table, key, isPath, pc);
    if (found) {
        cell->conf.decrement();
        // DPRINTF(NoSQPred, "Found Cell@: %p\n", cell);
        DPRINTF(NoSQPred, "@%s index %lu conf after dec: %i\n",
                isPath ? "path": "pc", key, cell->conf.read());

    } else if (alloc) {
        // DPRINTF(NoSQPred, "Dec on allocation\n");
        cell = allocate(table, key, isPath, pc);
        cell->conf.decrement();
        DPRINTF(NoSQPred, "@%s index %lu conf after dec: %i\n",
                isPath ? "path": "pc", key, cell->conf.read());

        auto [found, cell] = find(table, key, isPath, pc);
        assert(found);
    }
}

void
MemDepPredictor::increment(MemPredTable &table, Addr key, const DistancePair &pair, bool isPath, Addr pc)
{
    bool found;
    MemPredCell *cell;
    std::tie(found, cell) = find(table, key, isPath, pc);
    if (found) {
        // DPRINTF(NoSQPred, "Inc in place\n");
        if (pair.dqDistance && pair.dqDistance * 100 == pair.ssnDistance) {
            cell->storeDistance = pair.ssnDistance;
        }
        cell->conf.increment(8);
        DPRINTF(NoSQPred, "@%s index %lu conf after inc: %i\n",
                isPath ? "path": "pc", key, cell->conf.read());
    } else {
        // DPRINTF(NoSQPred, "Inc on allocation\n");
        cell = allocate(table, key, isPath, pc);
        if (pair.dqDistance && pair.dqDistance * 100 == pair.ssnDistance) {
            cell->storeDistance = pair.ssnDistance;
        }
        cell->conf.increment(8);
        DPRINTF(NoSQPred, "@%s index %lu conf after inc: %i\n",
                isPath ? "path": "pc", key, cell->conf.read());

        auto [found, cell] = find(table, key, isPath, pc);
        assert(found);
    }
}


void
MemDepPredictor::squash(MemPredHistory &hist)
{
    hist.updated = true;
}

void
MemDepPredictor::recordPath(Addr control_pc, bool is_call, bool pred_taken)
{
    if (is_call) {
        unsigned mask = ((uint64_t)1 << callShamt) - 1;
        controlPath = (controlPath << callShamt) | ((control_pc >> pcShamt) & mask);
    } else {
        unsigned mask = ((uint64_t)1 << branchShamt) - 1;
        controlPath = (controlPath << (branchShamt + 1)) | (((control_pc >> pcShamt) & mask) << 1) | pred_taken;
    }
}

Addr
MemDepPredictor::genPathKey(Addr pc, FoldedPC path) const
{
    return pc ^ (((path) & PathMask) << pcShamt);
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
    unsigned shamt = isPath ? 0 : PCTableIndexBits;
    shamt += pcShamt;
    return (key >> shamt) & TagMask;
}

MemDepPredictor::FoldedPC
MemDepPredictor::getPath() const
{
    return controlPath;
}


std::pair<bool, MemPredCell *>
MemDepPredictor::find(MemPredTable &table, Addr indexKey, bool isPath, Addr tagKey)
{
    bool found;
    MemPredCell *cell = nullptr;

    MemPredSet &set = table.at(extractIndex(indexKey, isPath));
    Addr tag = extractTag(tagKey, isPath); // high bits are folded or thrown awary here

    auto it = set.find(tag);
    found = it != set.end();
    if (found) {
        cell = &(it->second);
    }
    return std::make_pair(found, cell);
}

MemPredCell *
MemDepPredictor::allocate(MemPredTable &table, Addr indexKey, bool isPath, Addr tagKey)
{
    MemPredSet &set = table.at(extractIndex(indexKey, isPath));
    unsigned assoc = isPath ? PathTableAssoc : PCTableAssoc;
    auto tag = extractTag(tagKey, isPath);
    assert(!set.count(tag));

    checkAndRandEvict(set, assoc);

    auto pair = set.emplace(tag, MemPredCell(ConfidenceBits));

    assert(pair.second); // no old equal key found
    assert(set.size() <= assoc);

    bool found;
    MemPredCell *cell;
    std::tie(found, cell) = find(table, indexKey, isPath, tagKey);
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
    cell->lastStoreSSN = sn;
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
        return cell->lastStoreSSN;
    }
}

void MemDepPredictor::commitLoad(Addr eff_addr, InstSeqNum sn, BasePointer &position) {
    // pass
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
            DPRINTF(NoSQSMB, "last store @ 0x%lx is %lu\n", eff_addr_low, cell->lastStoreSSN);
            skip_verify = cell->lastStoreSSN == nvul && cell->offset == offset && cell->size == size;
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
            skip_verify = cell->lastStoreSSN > 0 && cell->lastStoreSSN <= nvul;

            // log
            DPRINTF(NoSQSMB, "%s with last store @ 0x%lx is %lu\n",
                    skip_verify ? "Skip" : "Dont Skip",
                    eff_addr_low, cell->lastStoreSSN);
        }
    }

    return skip_verify;
}

void MemDepPredictor::touchSSBF(Addr eff_addr, InstSeqNum ssn)
{
    auto cell = tssbf.find(eff_addr);
    if (!cell) {
        cell = tssbf.allocate(eff_addr);
        cell->lastStoreSSN = ssn;
        cell->size = 0;
    }
}

void MemDepPredictor::completeStore(Addr eff_addr, InstSeqNum ssn)
{
    auto cell = tssbf.find(eff_addr);
    if (cell && cell->lastStoreSSN < ssn) {
        cell->lastStoreSSN = ssn;
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
        MemPredHistory &hist)
{
    if (!dq_dist || sn_dist != dq_dist * 100) {
        DPRINTF(NoSQPred, "The distance between producer and consumer is not reasonable:"
                          "%lu -> %lu with dq dist: %u\n", last_store_cell->lastStoreSSN, load_sn, dq_dist);
        if (hist.patternInfo.valid) {
            updatePredictorsOnCorrect(load_pc, hist.patternInfo.bypass, sn_dist, dq_dist, hist);
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
        updatePredictorsOnCorrect(load_pc, false, sn_dist, dq_dist, hist);
        return;
    } else {
        update(load_pc, true, sn_dist, dq_dist, hist);
    }
}

void
MemDepPredictor::updatePredictorsOnCorrect(Addr pc, bool should_bypass, unsigned int sn_dist, unsigned int dq_dist,
                                           MemPredHistory &history)
{
    if (history.updated) {
        DPRINTF(NoSQPred, "history has been recorded, return\n");
    }
    history.updated = true;
    // All correct predictions arrive here
    meta.record(pc, should_bypass, history.pcInfo.bypass,
                history.pathInfo.bypass, history.patternInfo.bypass,
                history.patternInfo.valid, history.pathInfo.valid, history.willSquash);

    auto key = genPathKey(pc, history.pathInfo.path);
    if (should_bypass) {
        increment(pcTable, pc, {sn_dist, dq_dist}, false, pc);
        increment(pathTable, key, {sn_dist, dq_dist}, true, pc);
    } else {
        decrement(pcTable, pc, false, false, pc);
        decrement(pathTable, key, false, true, pc);
    }
    localPredictor.updateOnCorrect(pc, should_bypass, sn_dist, dq_dist, history);
}

void MemDepPredictor::squashLoad(Addr pc, MemPredHistory &hist)
{
    if (hist.updated) {
        return;
    }
    hist.updated = true;
    if (hist.patternInfo.valid) {
        localPredictor.recordSquash(pc, hist);
    }
}

void MemDepPredictor::pcPredict(PredictionInfo &info, Addr pc)
{
    auto pc_index = pc;

    auto [found, cell] = find(pcTable, pc_index, false, pc);

    if (!found) {
        DPRINTF(NoSQPred, "For load @ 0x%x "
                          "pc entry not found\n", pc);
        info.valid = false;
        return;
    }
    info.valid = true;
    info.bypass = cell->conf.read() > 0;
    info.distPair.ssnDistance = cell->storeDistance;

    DPRINTF(NoSQPred, "For load @ 0x%x, @ index: %u "
                      "pc predictor predict %i with confi: %i "
                      "to storeDistance %u\n",
            pc, pc_index, info.bypass, cell->conf.read(),
            cell->storeDistance);
}

void MemDepPredictor::pathPredict(PathPredInfo &info, Addr pc, MemDepPredictor::FoldedPC path)
{
    auto path_index = genPathKey(pc, path);
    auto [found, cell] = find(pathTable, path_index, true, pc);
    if (!found) {
        DPRINTF(NoSQPred, "For load @ 0x%x with path 0x%lx, "
                          "path signature not found\n", pc, path);
        info.valid = false;
        info.path = path;
        return;
    }

    info.valid = true;
    info.bypass = cell->conf.read() > 0;
    info.distPair.ssnDistance = cell->storeDistance;
    info.confidence = cell->conf.read();
    info.path = path;
    DPRINTF(NoSQPred, "For load @ 0x%x with path 0x%lx, @ index: %u "
                      "path predictor predict %i with confi: %i "
                      "to storeDistance %u\n",
            pc, path, path_index,
            info.bypass, cell->conf.read(),
            cell->storeDistance);
}

void MemDepPredictor::patternPredict(PatternPredInfo &info, Addr pc)
{
    localPredictor.predict(pc, info);
}

TermedPointer MemDepPredictor::getStorePosition(unsigned ssn_distance) const {
    if (recentStoreTable.size() > ssn_distance) {
        assert(!storeWalking);
        return recentStoreTable[ssn_distance].pointer;
    } else {
        return nullTermedPointer;
    }
}

void MemDepPredictor::addNewStore(const TermedPointer &ptr, InstSeqNum seq) {
    DPRINTF(NoSQPred, "Insert inst[%lu] @" ptrfmt "into recent store table\n",
            seq, extptr(ptr));
    recentStoreTable.emplace_front(seq, ptr);
    DPRINTF(NoSQPred, "Now last store: %lu\n", recentStoreTable[0].seq);
}

void MemDepPredictor::squashStoreTable() {
    assert(!storeWalking);
    recentStoreTable.clear();
}

void MemDepPredictor::storeTableWalkStart() {
    storeWalking = true;
}

void MemDepPredictor::storeTableWalkEnd() {
    storeWalking = false;
}

void MemDepPredictor::removeStore(InstSeqNum seq) {
    auto it = recentStoreTable.rbegin(),
    e = recentStoreTable.rend();
    while (it != e) {
        if (it->seq <= seq) {
            DPRINTF(NoSQPred, "Remove Inst[%lu] from recent store table\n", it->seq);
            std::advance(it, 1); // backward to forward dirty things
            recentStoreTable.erase(it.base());
        } else {
            break;
        }
    }
}

std::deque<RecentStore> &MemDepPredictor::getRecentStoreTable() {
    return recentStoreTable;
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
          table(Depth, SSBFSet()),
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
            if (it->second.lastStoreSSN < oldest->second.lastStoreSSN) {
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
        if (it.second.lastStoreSSN > youngest) {
            youngest = it.second.lastStoreSSN;
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
void
LocalPredictor::predict(Addr pc, PatternPredInfo &info)
{
    auto pair = instTable.find(pc);
    if (pair == instTable.end() || !pair->second.active) {
        info.valid = false;
        if (pair != instTable.end()) {
            info.localHistory = pair->second.history;
        }
        return;
    }

    info.valid = true;
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

        info.bypass = val > 0;
        info.predictionValue = val;
    } else {
        // prediction with table:
         unsigned index = extractIndex(pc, cell.history);
        info.bypass = predTable.at(index).read() > 0;
    }

    info.localHistory = cell.history;
    info.distPair.ssnDistance = cell.storeDistance;
    cell.recentUsed = true;
    info.bypass &= info.distPair.ssnDistance > 0;

    if (Debug::NoSQPred) {
        std::cout << "Local history: " << info.localHistory
        << "\n";
    }

    cell.history <<= 1;
    cell.history[0] = info.bypass;
    cell.numSpeculativeBits += 1;
    if (Debug::NoSQPred) {
        std::cout << "History after pred: " << cell.history
                << ", num spec after pred: " << cell.numSpeculativeBits
                << "\n";
    }

    DPRINTF(NoSQPred, "Predicted by pattern predictor, bypass: %i, from with ssn dist: %u, "
            "dq dist: %u\n",
            info.bypass, cell.storeDistance);
}

void LocalPredictor::updateOnMiss(Addr pc, bool should_bypass, unsigned int ssn_dist, unsigned int dq_dist,
                                  MemPredHistory &history)
{
    auto pair = instTable.find(pc);
    DPRINTF(NoSQPred, "Pattern entry not found: %i, pattern sensitive: %i\n",
            pair == instTable.end(), history.patternInfo.valid);
    if (pair == instTable.end() || !history.patternInfo.valid) { // evicted
        recordMispred(pc, should_bypass, ssn_dist, dq_dist, history);
        return;
    }
    DPRINTF(NoSQPred, "Updating pattern history entry\n");
    auto &cell = pair->second;
    if (history.patternInfo.bypass != should_bypass) {
        if (Debug::NoSQPred) {
            std::cout << "History now: " << cell.history
                      << ", num spec: " << cell.numSpeculativeBits
                      << ", should bypass: " << should_bypass
                      << "\n";
        }
        boost::dynamic_bitset<> used_hist(visableHistoryLen);
        unsigned index;
        if (!history.willSquash) {
            if (cell.numSpeculativeBits > 0) {
                if (cell.numSpeculativeBits <= historyLen) {
                    cell.history[cell.numSpeculativeBits - 1] = should_bypass;
                    assert(cell.numSpeculativeBits >= 0);
                }
                cell.numSpeculativeBits--;
                DPRINTF(NoSQPred, "Inst[%lu] dec num spec for pc:0x%lx\n", history.inst, pc);
            }

            used_hist = history.patternInfo.localHistory;
            used_hist.resize(visableHistoryLen);
            index = extractIndex(pc, used_hist);
        } else {
            if (cell.numSpeculativeBits > 0) {
                if (cell.numSpeculativeBits <= historyLen) {
                    cell.history >>= 1;
                }
                cell.numSpeculativeBits--;
                DPRINTF(NoSQPred, "Inst[%lu] dec num spec for pc:0x%lx\n", history.inst, pc);
            }

            used_hist = history.patternInfo.localHistory;
            used_hist.resize(visableHistoryLen);
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
        if (history.willSquash) {
            if (should_bypass) {
                counter.increment();
            } else {
                counter.decrement();
            }
            if (perceptron && !history.patternInfo.localHistory.empty()) {
                cell.fit(history.patternInfo, should_bypass);
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
        cell.storeDistance = ssn_dist;
    }
}

void LocalPredictor::recordMispred(Addr pc, bool should_bypass, unsigned int ssn_dist, unsigned int dq_dist,
                                   MemPredHistory &hist)
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
    auto &cell = instTable[pc];
    if (should_bypass && dq_dist && ssn_dist == dq_dist * 100) {
        cell.storeDistance = ssn_dist;
    }
    cell.count++;

    touchCount++;
    if (touchCount >= resetCount) {
        touchCount = 0;
        for (auto &pair: instTable) {
            pair.second.recentTouched = false;
        }
    }
    cell.recentTouched = true;

    if (cell.count > activeThres && enablePattern) {
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
        if (perceptron && !hist.patternInfo.localHistory.empty()) {
            cell.fit(hist.patternInfo, should_bypass);
        }
    }

    cell.history = cell.history << 1;
    cell.history[0] = should_bypass;
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
LocalPredictor::updateOnCorrect(Addr pc, bool should_bypass, unsigned int sn_dist, unsigned int dq_dist,
                                MemPredHistory &history)
{
    auto pair = instTable.find(pc);
    if (pair == instTable.end()) {
        return;
    }
    auto &cell = instTable.at(pc);
    if (!cell.active) {
        return;
    }
    if (Debug::NoSQPred) {
        std::cout << "History now: " << cell.history
                  << ", num spec: " << cell.numSpeculativeBits
                  << ", should bypass: " << should_bypass
                  << "\n";
    }
    assert(cell.numSpeculativeBits > 0 || !history.patternInfo.valid);
//    assert(cell.history[cell.numSpeculativeBits - 1] == should_bypass);
    if (!history.patternInfo.valid) {
        return;
    }
    DPRINTF(NoSQPred, "Correctly predicted on history: ");
    if (Debug::NoSQPred) {
        std::cout << (cell.history >> cell.numSpeculativeBits) << "\n";
        std::cout << "History now: " << cell.history << "\n";
    }
    cell.numSpeculativeBits--;
    DPRINTF(NoSQPred, "Inst[%lu] dec num spec for pc:0x%lx\n", history.inst, pc);
    assert(cell.numSpeculativeBits >= 0);
}

void LocalPredictor::recordSquash(Addr pc, MemPredHistory &history)
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
    DPRINTF(NoSQPred, "Inst[%lu] dec num spec for pc:0x%lx\n", history.inst, pc);
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

void LocalPredCell::fit(PatternPredInfo &hist, bool should_bypass)
{
    if (should_bypass == hist.bypass &&
        abs(hist.predictionValue) > theta) {
        return;
    }
    if (should_bypass) {
        weights.back().increment();
    } else {
        weights.back().decrement();
    }

    const auto &hist_bits = hist.localHistory;

    if (Debug::NoSQPred) {
        std::cout << "Local history: " << hist.localHistory
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

void MetaPredictor::record(Addr load_pc, bool should, bool pc, bool path, bool pattern,
        bool pattern_sensitive, bool path_sensitive,
        bool will_squash)
{
    unsigned index = (load_pc >> pcShamt) & indexMask;
    auto &cell = table[index];
    float factor = 0.02;
    if (will_squash) {
        factor *= 3;
    }
    float decay = 1.0 - factor;
    cell.pcMissRate = (should != pc) * factor + cell.pcMissRate * decay;
    if (path_sensitive) {
        cell.pathMissRate = (should != path) * factor + cell.pathMissRate * decay;
    }
    if (pattern_sensitive) {
        cell.patternMissRate = (should != pattern) * factor + cell.patternMissRate * decay;
    }

//    if (cell.patternMissRate > 0.75 &&
//        cell.patternMissRate - cell.pcMissRate > 0.05 &&
//        cell.patternMissRate - cell.pathMissRate > 0.05) {
//        DPRINTF(NoSQPred, "Blacklist pc 0x%lx with pc: %f, path: %f, pattern: %f\n",
//                load_pc,
//                cell.pcMissRate, cell.pathMissRate, cell.patternMissRate);
//    }
}

MetaPredictor::WhichPredictor
MetaPredictor::choose(Addr load_pc)
{
    unsigned index = (load_pc >> pcShamt) & indexMask;
    auto &cell = table[index];
    if (blackList.count(load_pc)) {
        DPRINTF(NoSQPred, "Won't choose pattern pred for pc 0x%lx because of blacklist\n", load_pc);
    }
    DPRINTF(NoSQPred, "Miss rate, pattern: %f, path: %f, pc: %f\n",
            cell.patternMissRate, cell.pathMissRate, cell.pcMissRate);
    if (enablePattern && !blackList.count(load_pc) &&
            cell.patternMissRate < cell.pathMissRate && cell.patternMissRate < cell.pcMissRate) {
        return WhichPredictor::UsePattern;
    } else if (cell.pathMissRate < cell.pcMissRate) {
        return WhichPredictor::UsePath;
    } else {
        return WhichPredictor::UsePC;
    }
}
