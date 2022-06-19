#ifndef __CPU_PRED_DECOUPLEDBRANCHPRED_HH__
#define __CPU_PRED_DECOUPLEDBRANCHPRED_HH__

#include <deque>
#include <map>
#include <queue>
#include <vector>

#include <boost/dynamic_bitset.hpp>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/pred/ras.hh"
#include "cpu/pred/stream_pred.hh"
#include "cpu/pred/stream_struct.hh"
#include "cpu/pred/ubtb.hh"
#include "cpu/static_inst.hh"
#include "params/DecoupledBranchPred.hh"
#include "sim/sim_object.hh"

struct BPHistory {
    boost::dynamic_bitset<> history;
};

class DecoupledBranchPred : public SimObject {
    public:
    typedef DecoupledBranchPredParams Params;

    private:
    std::deque<FetchStream> fetchStreamQueue;

    std::string _name;

    StreamPredictor *streamPred;

    StreamUBTB *streamUBTB;

    ReturnAddrStack ras;

    // std::queue<Addr> pcSent;

    std::queue<StreamPrediction> uBTBHistory;

    std::map<InstSeqNum, BPHistory> bpHistory;

    Addr s0CtrlPC;
    StreamPrediction s0UbtbPred;
    boost::dynamic_bitset<> s0History;
    void updateS0Hist();

    Addr s1CtrlPC;
    StreamPrediction s1BackingPred, s1UbtbPred;
    boost::dynamic_bitset<> s1History;

    Addr s2CtrlPC;
    StreamPrediction s2Pred;
    boost::dynamic_bitset<> s2History;
    void updateS2Hist();

    void add2FTQ(const StreamPrediction &fetchStream);

    void overrideStream();

    void overrideUpdateUBTB();

    bool check_prediction(const StreamPrediction &ubtb_prediction,
                          const StreamPrediction &main_predictor_prediction);

    void stopDecoupledPrediction();

    public:
    // const std::string name() const {return _name};

    DecoupledBranchPred(const Params &params);

    // perform state update
    void tick();

    // fetch get prefetching cachelines from decoupled branch predictor
    std::vector<Addr> getPrefetchLines();

    // fetch get fetching addresses from decoupled branch predictor
    std::vector<FetchStream> getStreams();

    std::pair<bool, TheISA::PCState> willTaken(Addr cpc);

    void update(const InstSeqNum);

    /**
     * Squashes all outstanding updates until a given sequence number, and
     * corrects that sn's update with the proper address and taken/not taken.
     * @param squashed_sn The sequence number to squash any younger updates up
     * until.
     * @param corr_target The correct branch target.
     * @param actually_taken The correct branch direction.
     */
    void controlSquash(const InstSeqNum control_sn, const TheISA::PCState control_pc,
                       const TheISA::PCState &corr_target, bool isConditional, bool isIndirect, bool actually_taken);
    /**
     * Squashes all outstanding updates until a given sequence number.
     * @param squashed_sn The sequence number to squash any younger updates up
     * until.
     */
    void nonControlSquash(const InstSeqNum squashed_sn);

    void commitInst(const InstSeqNum inst_sn);

    void commitConditional(const InstSeqNum inst_sn, const TheISA::PCState &pc,
                           bool actually_taken, const TheISA::PCState &target);

    void commitUnconditional(const InstSeqNum inst_sn, const TheISA::PCState &pc,
                           const TheISA::PCState &target);

    boost::dynamic_bitset<> getCurrentGHR() const;

    Addr getLastCallsite(ThreadID tid);

    bool isOracle() {
        return false;
    }
    virtual Addr getOracleAddr() {
        return 0;
    }
    virtual bool getLastDirection() {
        return false;
    }
    virtual bool canPredictLoop() {
        return false;
    }

};

#endif // DecoupledBranchPred_HH
