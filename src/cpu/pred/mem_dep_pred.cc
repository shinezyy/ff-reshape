#include "cpu/pred/mem_dep_pred.hh"

#include <algorithm>

#include "base/intmath.hh"
#include "debug/NoSQPred.hh"

MemDepPredictor::MemDepPredictor(const Params *params)
    : SimObject(params),

    _name("NoSQPredictor"),

    PCTableSize(params->PCTableSize),
    PCTableAssoc(params->PCTableAssoc),
    PCTableDepth(PCTableSize/PCTableAssoc),
    PCTableIndexBits(ceilLog2(PCTableDepth)),
    PCTableIndexMask((1 << (PCTableIndexBits + pcShamt)) - 1),

    PathTableSize(params->PathTableSize),
    PathTableAssoc(params->PathTableAssoc),
    PathTableDepth(PathTableSize/PathTableAssoc),
    PathTableIndexBits(ceilLog2(PathTableDepth)),
    PathTableIndexMask((1 << (PathTableIndexBits + pcShamt)) - 1),

    DistanceBits(params->DistanceBits),
    ShamtBits(params->ShamtBits),
    StoreSizeBits(params->StoreSizeBits),
    ConfidenceBits(params->ConfidenceBits),
    TagBits(params->TagBits),
    TagMask((1 << TagBits) - 1),

    HistoryLen(params->HistoryLen),
    PathMask((1 << HistoryLen) - 1),

    BranchPathLen(params->BranchPathLen),
    CallPathLen(params->CallPathLen),
    pcTable(PCTableDepth),
    pathTable(PathTableDepth),
    tssbf(params)
{
}

std::pair<bool, unsigned>
MemDepPredictor::predict(Addr load_pc, MemPredHistory *&hist)
{
    return predict(load_pc, controlPath, hist);
}

std::pair<bool, unsigned>
MemDepPredictor::predict(Addr load_pc, FoldedPC path, MemPredHistory *&hist)
{
    auto mp_history = new MemPredHistory;
    hist = mp_history;

    auto pc_index = load_pc;
    auto path_index = genPathKey(load_pc, path);

    // bool found;
    // MemPredCell *cell;
    auto [found, cell] = find(pathTable, path_index, true);
    if (found) { // in path table
        mp_history->bypass = cell->conf.read() > 0;
        mp_history->pathSensitive = true;
        mp_history->storeDistance = cell->distance;
        mp_history->pathBypass = mp_history->bypass;

        // DPRINTF(NoSQPred, "Found Cell@: %p with path: 0x%lx, index: 0x%lx\n",
        //         cell, path, path_index);
        DPRINTF(NoSQPred, "For load @ 0x%x with path 0x%lx, "
                "path predictor predict %i with confi: %i "
                "to storeDistance %u\n",
                load_pc, path, mp_history->pathBypass, cell->conf.read(),
                cell->distance);

    } else {
        mp_history->pathSensitive = false;
        mp_history->pathBypass = false;
        DPRINTF(NoSQPred, "For load @ 0x%x with path 0x%lx, "
                "path signature not found\n",
                load_pc, path);
    }

    std::tie(found, cell) = find(pcTable, pc_index, false);

    if (found) {
        if (!mp_history->pathSensitive) {
            mp_history->bypass = cell->conf.read() > 0;
            mp_history->storeDistance = cell->distance;
        }
        mp_history->pcBypass = cell->conf.read() > 0;

        DPRINTF(NoSQPred, "For load @ 0x%x, "
                "pc predictor predict %i with confi: 0x%i "
                "to storeDistance %u\n",
                load_pc, mp_history->pcBypass, cell->conf.read(),
                cell->distance);
    } else {
        if (!mp_history->pathSensitive) {
            mp_history->bypass = false;
        }
        mp_history->pcBypass = false;
    }

    DPRINTF(NoSQPred, "For load @ 0x%x, "
            "overall: bypass: %i, pc: %i, path: 0x%lx, path sensitive: %i, store dist: %i\n",
            load_pc,
            mp_history->bypass, mp_history->pcBypass, mp_history->pathBypass,
            mp_history->pathSensitive, mp_history->storeDistance);

    mp_history->path = path;
    return std::make_pair(mp_history->bypass, mp_history->storeDistance);
}

void
MemDepPredictor::update(Addr load_pc, bool should_bypass,
        unsigned actual_dist, MemPredHistory* &hist)
{
    bool pred_bypass = hist->bypass;
    if (!pred_bypass && !should_bypass) {
        // NOTE: check
        DPRINTF(NoSQPred, "For load @ 0x%x, correctly predicted non-bypassing\n",
                load_pc);
        return;

    } else if (pred_bypass && !should_bypass) {
        DPRINTF(NoSQPred, "For load @ 0x%x, mispredicted, should not bypass\n", load_pc);

        if (hist->pcBypass) {
            DPRINTF(NoSQPred, "Dec conf in pc table, ");
            decrement(pcTable, load_pc, true, false);
        }
        if (hist->pathBypass) {
            auto path_index = genPathKey(load_pc, hist->path);
            DPRINTF(NoSQPred, "Dec conf in path table with path: 0x%lx, ",
                    hist->path);
            decrement(pathTable, path_index, true, true);
        }

    } else {
        if (pred_bypass != should_bypass) {
            DPRINTF(NoSQPred, "For load @ 0x%x, mispredicted, should bypass!\n", load_pc);
        } else {
            DPRINTF(NoSQPred, "For load @ 0x%x, correctly predicted bypassing\n",
                    load_pc);
        }

        // if (!hist->pcBypass) {
        if (true) {
            DPRINTF(NoSQPred, "Inc conf in pc table with dist: %i, ", actual_dist);
            increment(pcTable, load_pc, actual_dist, false);
        }
        // if (!hist->pathBypass) {
        if (true) {
            DPRINTF(NoSQPred, "Inc conf in path table with dist: %i, path: 0x%lx, ",
                    actual_dist, hist->path);
            increment(pathTable, genPathKey(load_pc, hist->path), actual_dist, false);
        }
    }
    delete hist;
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

        bool found;
        MemPredCell *cell;
        std::tie(found, cell) = find(table, key, isPath);
        assert(found);
    }
}

void
MemDepPredictor::increment(MemPredTable &table, Addr key, unsigned dist, bool isPath)
{
    bool found;
    MemPredCell *cell;
    std::tie(found, cell) = find(table, key, isPath);
    if (found) {
        // DPRINTF(NoSQPred, "Inc in place\n");
        cell->distance = dist;
        cell->conf.increment();
        DPRINTF(NoSQPred, "conf after inc: %i\n", cell->conf.read());
    } else {
        // DPRINTF(NoSQPred, "Inc on allocation\n");
        cell = allocate(table, key, isPath);
        cell->distance = dist;
        cell->conf.increment();
        DPRINTF(NoSQPred, "conf after inc: %i\n", cell->conf.read());

        bool found;
        MemPredCell *cell;
        std::tie(found, cell) = find(table, key, isPath);
        assert(found);
    }
}


void
MemDepPredictor::squash(MemPredHistory* &hist)
{
    delete hist;
}

void
MemDepPredictor::recordPath(Addr control_pc, bool isCall)
{
    unsigned shamt = isCall ? callShamt : branchShamt;
    unsigned mask = isCall ?
        ((1 << callShamt) - 1) : ((1 << branchShamt) - 1);
    controlPath = (controlPath << shamt) | ((control_pc >> pcShamt) & mask);
}

Addr
MemDepPredictor::genPathKey(Addr pc, FoldedPC path) const
{
    return pc ^ (path & PathMask);
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
    shamt += 2;
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

    MemPredSet &set = table[extractIndex(key, isPath)];
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
    MemPredSet &set = table[extractIndex(key, isPath)];
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

void MemDepPredictor::commitStore(Addr eff_addr, InstSeqNum sn, BasePointer &position) {
    SSBFCell *cell = tssbf.find(eff_addr);
    if (!cell) {
        cell = tssbf.allocate(eff_addr);
    }
    cell->lastStore = sn;
    cell->lastStorePosition = position;
    cell->predecessorPosition = position;
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
    SSBFCell *cell = tssbf.find(eff_addr);
    if (!cell) {
        DPRINTF(NoSQPred, "When committing load producing store is not found\n");
    } else {
        cell->predecessorPosition = position;
    }
}

MemDepPredictor *MemDepPredictorParams::create()
{
    return new MemDepPredictor(this);
}

TSSBF::TSSBF(const Params *p)
        : SimObject(p),
          TagBits(p->TSSBFTagBits),
          TagMask((1 << TagBits) - 1),
          Size(p->TSSBFSize),
          Assoc(p->TSSBFAssoc),
          Depth(Size/Assoc),
          IndexBits(ceilLog2(Depth)),
          IndexMask((1 << IndexBits) - 1),
          table(Depth)
{

}

Addr TSSBF::extractIndex(Addr key) const {
    return key & IndexMask;
}

Addr TSSBF::extractTag(Addr key) const {
    return (key >> IndexBits) & TagMask;
}

SSBFCell *TSSBF::find(Addr key) {
    auto &set = table[extractIndex(key)];
    auto tag = extractTag(key);
    auto it = set.find(tag);
    if (it != set.end()) {
        return nullptr;
    } else {
        return &it->second;
    }
}

SSBFCell *TSSBF::allocate(Addr key) {
    auto &set = table[extractIndex(key)];
    auto tag = extractTag(key);
    assert (set.count(tag) == 0);

    checkAndRandEvict(set, Assoc);

    auto pair = set.emplace(tag, SSBFCell());

    assert(pair.second); // no old equal key found
    assert(set.size() <= Assoc);

    return &(pair.first->second);
}


