#include "cpu/pred/ubtb.hh"

#include "base/trace.hh"
#include "debug/DecoupleBP.hh"

StreamUBTB::StreamUBTB(const Params &p)
    : TimedPredictor(p)
{
    for (auto i = 0; i < size; i++) {
        ubtb[0xfffffff-i];  // dummy initialization
    }
    for (auto it = ubtb.begin(); it != ubtb.end(); it++) {
        it->second.tick = 0;
        mruList.push_back(it);
    }
    std::make_heap(mruList.begin(), mruList.end(), older());
}

void
StreamUBTB::tickStart()
{

}

void
StreamUBTB::tick()
{

}

void
StreamUBTB::putPCHistory(Addr pc, const boost::dynamic_bitset<> &history)
{
    if (pc == 0) {
        prediction.valid = false;
        return;
    }
    DPRINTF(DecoupleBP, "Prediction request: stream start=0x%#lx\n", pc);
    if (ubtb.find(pc) == ubtb.end()) {
        DPRINTF(DecoupleBP, "No entry found, guess a unlimited stream\n");
        prediction.valid = true;
        prediction.bbStart = pc;
        prediction.bbEnd = 0;
        prediction.streamLength = unlimitedStreamLen;
        prediction.nextStream = 0;
        prediction.endIsRet = false;
    }
}

StreamPrediction
StreamUBTB::getStream()
{
    return prediction;
}

void StreamUBTB::update(const PredictionID pred_id, Addr stream_start_pc, Addr control_pc, Addr target,
                        bool is_conditional, bool is_indirect, bool actually_taken,
                        const boost::dynamic_bitset<> &history)
{
    std::string buf;
    boost::to_string(history, buf);
    DPRINTF(DecoupleBP,
            "StreamUBTB::update: pred_id: %d, control_pc: %#x, target: %#x, is_conditional: %d, is_indirect: %d, "
            "actually_taken: %d, history: %s\n",
            pred_id, control_pc, target, is_conditional, is_indirect, actually_taken, buf.c_str());

    std::pop_heap(mruList.begin(), mruList.end(), older());
    // now the LRU is at the end of mrulist
    const auto &ubtb_entry = mruList.back();
    DPRINTF(DecoupleBP, "StreamUBTB::update: pop ubtb_entry: %#x\n", ubtb_entry->first);
    ubtb.erase(ubtb_entry->first);

    auto tag = makePCHistTag(stream_start_pc, history);
    ubtb[tag].tick = curTick();
    ubtb[tag].bbStart = stream_start_pc;
    ubtb[tag].bbEnd = control_pc;
    ubtb[tag].length = control_pc - stream_start_pc;
    ubtb[tag].nextStream = target;

    DPRINTF(DecoupleBP, "StreamUBTB::update: insert ubtb_entry, tag: %#lx:  %#lx - %u -  %#lx -> %#lx \n", tag,
            ubtb[tag].bbStart, ubtb[tag].length, ubtb[tag].bbEnd, ubtb[tag].nextStream);

    mruList.pop_back();

    // Because fetch has been redirected, here we must make another prediction
}

uint64_t
StreamUBTB::makePCHistTag(Addr pc, const boost::dynamic_bitset<> &history)
{
    auto hist = history;
    hist.resize(64);
    return pc ^ history.to_ulong();
}
