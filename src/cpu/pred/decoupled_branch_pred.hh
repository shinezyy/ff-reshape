#ifndef __CPU_PRED_DECOUPLEDBRANCHPRED_HH__
#define __CPU_PRED_DECOUPLEDBRANCHPRED_HH__

#include <deque>
#include <limits>
#include <map>
#include <queue>
#include <vector>

#include <boost/dynamic_bitset.hpp>

#include "base/statistics.hh"
#include "base/trace.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/pred/ras.hh"
#include "cpu/pred/stream_pred.hh"
#include "cpu/pred/stream_struct.hh"
#include "cpu/pred/ubtb.hh"
#include "cpu/static_inst.hh"
#include "debug/DecoupleBP.hh"
#include "params/DecoupledBranchPred.hh"
#include "sim/sim_object.hh"


enum PredcitionSource {
    STREAM_PRED = 0,
    UBTB_PRED,
};

struct BPHistory {
    boost::dynamic_bitset<> history;
    Addr streamStart;
    StreamLen predStreamLength;
    Addr nextStream;
    // PredcitionSource predSource;
};

class DecoupledBranchPred : public SimObject {
    public:
    typedef DecoupledBranchPredParams Params;

    private:
    std::map<FsqID, FetchStream> fetchStreamQueue;
    int fetchStreamQueueSize;
    FsqID fsqID{0}; // this is a queue ptr for fsq itself
    Addr ftqEnqPC;
    FsqID ftqEnqFsqID{0}; // this is a queue ptr for ftq to read from fsq

    void tryToEnqFsq();
    

    std::map<FtqID, FtqEntry> ftq;
    int ftqSize;
    FtqID ftqID{0}; // this is a queue ptr for ftq itself
    FtqID fetchFtqID{0}; // this is a queue ptr for fetch to read from ftq
    std::pair<FtqID, FtqEntry> fetchReadFtqEntryBuffer;
    bool fetchReadFtqEntryBufferValid;

    void tryToEnqFtq();


    Addr alignToCacheLine(Addr addr) {
        return addr & ~0x3f; // TODO: parameterize this
    }

    std::string _name;

    const unsigned historyBits{128};

    boost::dynamic_bitset<> commitHistory;
    boost::dynamic_bitset<> speculativeHistory;

    StreamPredictor *streamPred;

    StreamUBTB *streamUBTB;

    ReturnAddrStack ras;

    // std::queue<Addr> pcSent;

    std::queue<StreamPrediction> uBTBHistory;

    std::map<PredictionID, BPHistory> bpHistory;

    // std::map<PredictionID, StreamPredictionWithID> ftq;

    PredictionID predictionID{0};

    PredictionID maxInflightPrediction{128};

    PredictionID incPredictionID() {
        auto old_predict_id = predictionID;
        predictionID++;
        DPRINTF(DecoupleBP, "inc predictionID: %lu -> %lu\n", old_predict_id, predictionID);
        if (predictionID == maxInflightPrediction * 4) {
            predictionID = 0;
        }
        return old_predict_id;
    }

    // an identifier of stream predictor at a miss condition
    bool streamMiss;

    Addr s0StreamPC;
    StreamPrediction s0UbtbPred;
    boost::dynamic_bitset<> s0History;
    void updateS0Hist();

    Addr s1StreamPC;
    StreamPrediction s1BackingPred, s1UbtbPred;
    boost::dynamic_bitset<> s1History;
    void updateS1Hist();

    Addr s2CtrlPC;
    StreamPrediction s2Pred;
    boost::dynamic_bitset<> s2History;
    void updateS2Hist();

    void add2FTQ(const StreamPrediction &fetchStream, PredictionID id);

    void overrideStream();

    void overrideUpdateUBTB();

    bool check_prediction(const StreamPrediction &ubtb_prediction,
                          const StreamPrediction &main_predictor_prediction);

    void stopDecoupledPrediction();

    void recordPrediction(Addr stream_start, StreamLen stream_len, Addr next_stream,
                          const boost::dynamic_bitset<> &history, PredictionID id);

    void printFtqEntry(FtqEntry e){
        DPRINTF(DecoupleBP, "ftq entry start: %x, end: %x, takenPC: %x, taken: %d, target: %x, fsqID: %lu\n",
            e.startPC, e.endPC, e.takenPC, e.taken, e.target, e.fsqID);
    }
    void printFsqEntry(FetchStream e){
        DPRINTF(DecoupleBP, "fsq entry start: %x, predEnded: %d, predEnd: %x, predTarget: %x, predBranchAddr: %x, predBranchType: %x, hasEnteredFtq: %d\n\
            exeEnded: %d, exeEnd: %x, exeTarget: %x, exeBranchAddr: %x, exeBranchType: %x\n",
            e.streamStart, e.predEnded, e.predStreamEnd, e.predTarget, e.predBranchAddr, e.predBranchType, e.hasEnteredFtq,
            e.exeEnded, e.exeStreamEnd, e.exeTarget, e.exeBranchAddr, e.exeBranchType);
    }

  public:
    // const std::string name() const {return _name};

    DecoupledBranchPred(const Params &params);

    // perform state update
    void tick();

    // fetch get prefetching cachelines from decoupled branch predictor
    std::vector<Addr> getPrefetchLines();

    // fetch get fetching addresses from decoupled branch predictor
    std::vector<FetchStream> getStreams();

    // taken, target, end_of_ftq_entry
    std::tuple<bool, TheISA::PCState, bool> willTaken(TheISA::PCState &cpc);

    void notifyStreamSeq(const InstSeqNum seq);

    /**
     * Squashes all outstanding updates until a given sequence number, and
     * corrects that sn's update with the proper address and taken/not taken.
     * @param squashed_sn The sequence number to squash any younger updates up
     * until.
     * @param corr_target The correct branch target.
     * @param actually_taken The correct branch direction.
     */
    void controlSquash(const FtqID inst_ftq_id, const FsqID inst_fsq_id, const TheISA::PCState control_pc,
                       const TheISA::PCState &corr_target, bool isConditional, bool isIndirect, bool actually_taken,
                       const InstSeqNum seq);
    /**
     * Squashes all outstanding updates until a given sequence number.
     * @param squashed_sn The sequence number to squash any younger updates up
     * until.
     */
    void nonControlSquash(const FtqID inst_ftq_id, const FsqID inst_fsq_id, const TheISA::PCState inst_pc,
                          const InstSeqNum seq);

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
    PredictionID getFTQHeadPredictionID() {
        return ftq.begin()->first;  // the prediction ID of the head of FTQ
    }
    // when building inst, fetchReadFtqEntryBuffer should contain the ftq entry being fetched
    FsqID getFsqIDFromFtqHead() {
        assert(fetchReadFtqEntryBufferValid);
        return fetchReadFtqEntryBuffer.second.fsqID;
    }
    FtqID getFtqIDFromHead() {
        assert(fetchReadFtqEntryBufferValid);
        return fetchReadFtqEntryBuffer.first;
    }

    // bool hasFtqEntryToFetch();
    bool tryToFillFtqEntryBuffer();

};

#endif // DecoupledBranchPred_HH
