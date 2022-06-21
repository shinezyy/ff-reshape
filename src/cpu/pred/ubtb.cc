#include "cpu/pred/ubtb.hh"

StreamUBTB::StreamUBTB(const Params &p)
    : TimedPredictor(p)
{
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

}

StreamPrediction
StreamUBTB::getStream()
{
    return StreamPrediction();
}

void
StreamUBTB::update(const PredictionID pred_id, Addr control_pc, Addr target, bool is_conditional, bool is_indirect,
                  bool actually_taken, std::shared_ptr<void> bp_history)
{

}