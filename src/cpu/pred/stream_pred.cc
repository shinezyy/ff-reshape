#include "cpu/pred/stream_pred.hh"

StreamPredictor::StreamPredictor(const Params &p)
    : TimedPredictor(p)
{
}

void
StreamPredictor::tickStart()
{

}

void
StreamPredictor::tick()
{

}

void
StreamPredictor::putPCHistory(Addr pc, const boost::dynamic_bitset<> &history)
{

}

std::pair<Addr, StreamPrediction>
StreamPredictor::getStreamS1()
{
    return std::make_pair(0, StreamPrediction());
}

void StreamPredictor::update(const PredictionID pred_id, Addr control_pc, Addr target, bool is_conditional,
                             bool is_indirect, bool actually_taken, std::shared_ptr<void> bp_history)
{

}

