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
    DPRINTF(DecoupleBP, "Prediction request: stream start=%#lx\n", pc);
    const auto &it = ubtb.find(pc); // TODO: use hash of pc and history
    if (it == ubtb.end()) {
        DPRINTF(DecoupleBP, "No entry found, guess an unlimited stream\n");
        prediction.valid = false;
        // prediction.bbStart = pc;
        // prediction.bbEnd = 0;
        // prediction.streamLength = unlimitedStreamLen;
        // prediction.nextStream = 0;
        // prediction.endIsRet = false;
    } else {
        DPRINTF(DecoupleBP, "UBTB Entry found\n");
        prediction.valid = true;
        prediction.bbStart = pc;
        prediction.bbEnd = it->second.bbEnd;
        prediction.streamLength = it->second.length;
        prediction.nextStream = it->second.nextStream;
        prediction.endIsRet = it->second.endIsRet;
        prediction.history = history;
    }
}

StreamPrediction
StreamUBTB::getStream()
{
    DPRINTF(DecoupleBP, "Response stream prediction: %#lx->%#lx\n", prediction.bbStart, prediction.bbEnd);
    return prediction;
}

void StreamUBTB::update(const PredictionID fsq_id, Addr stream_start_pc, Addr control_pc, Addr target,
                        bool is_conditional, bool is_indirect, unsigned control_size, bool actually_taken,
                        const boost::dynamic_bitset<> &history)
{
    std::string buf;
    boost::to_string(history, buf);
    DPRINTF(DecoupleBP,
            "StreamUBTB::update: fsq id: %d, control_pc: %#x, target: %#x, is_conditional: %d, is_indirect: %d, "
            "actually_taken: %d, history: %s, control size: %u\n",
            fsq_id, control_pc, target, is_conditional, is_indirect, actually_taken, buf.c_str(), control_size);

    auto tag = makePCHistTag(stream_start_pc, history);
    auto it = ubtb.find(tag);

    bool new_entry = it == ubtb.end();

    if (new_entry) {
        std::pop_heap(mruList.begin(), mruList.end(), older());
        const auto &ubtb_entry = mruList.back();
        DPRINTF(DecoupleBP, "StreamUBTB::update: pop ubtb_entry: %#x\n", ubtb_entry->first);
        ubtb.erase(ubtb_entry->first);
    }

    ubtb[tag].tick = curTick();
    ubtb[tag].bbStart = stream_start_pc;
    ubtb[tag].bbEnd = control_pc;
    ubtb[tag].length = control_pc - stream_start_pc + control_size;
    ubtb[tag].nextStream = target;

    if (new_entry) {
        auto it = ubtb.find(tag);
        mruList.back() = it;
        std::push_heap(mruList.begin(), mruList.end(), older());
    }

    DPRINTF(DecoupleBP, "StreamUBTB:: %s ubtb_entry, tag: %#lx:  %#lx - %u -  %#lx -> %#lx \n",
            new_entry ? "Insert new" : "update", tag, ubtb[tag].bbStart, ubtb[tag].length, ubtb[tag].bbEnd,
            ubtb[tag].nextStream);


    // Because fetch has been redirected, here we must make another prediction
}

uint64_t
StreamUBTB::makePCHistTag(Addr pc, const boost::dynamic_bitset<> &history)
{
    auto hist = history;
    hist.resize(64);
    return pc ^ history.to_ulong();
}
