#include "cpu/pred/decoupled_branch_pred.hh"

DecoupledBranchPred::DecoupledBranchPred(const Params &params)
    : SimObject(params),
    streamPred(params.stream_pred),
    streamUBTB(params.stream_ubtb)
{
}

void DecoupledBranchPred::tick()
{
    // s2
    // get stream from UBTB

    bool inconsistent = check_prediction(s1BackingPred, s1UbtbPred);
    bool overriding = false;
    if (inconsistent) {
        overrideStream();
        overrideUpdateUBTB();  // do we update it after override?
    }
    add2FTQ(s1BackingPred);
    s2Pred = s1BackingPred;

    // s1
    // get stream prediction initated XX cycles before
    auto [branch_pc, stream_payload] = streamPred->getStreamS1();
    // sanity check
    if (branch_pc) {  // pc = 0 indicates no valid prediction
        // the pc of return predicton must be sent X cycle ago
        assert(branch_pc == s1CtrlPC);
    }
    if (stream_payload.endIsRet) {
        stream_payload.nextStream = ras.top().instAddr();
        stream_payload.rasUpdated = true;
    }
    s1CtrlPC = s0CtrlPC;
    s1BackingPred = stream_payload;
    s1UbtbPred = s0UbtbPred;

    // s0
    streamPred->putPCHistory(s0CtrlPC, s0History);
    streamUBTB->putPCHistory(s0CtrlPC, s0History);
    s0UbtbPred = streamUBTB->getStream();
    s0CtrlPC = s0UbtbPred.nextStream;
    if (overriding) {
        s0CtrlPC = s2Pred.nextStream;
    }
}

std::pair<bool, TheISA::PCState>
DecoupledBranchPred::willTaken(Addr pc)
{
    return std::make_pair(false, TheISA::PCState(0));
}

void DecoupledBranchPred::add2FTQ(const StreamPrediction &fetchStream)
{
}

bool DecoupledBranchPred::check_prediction(const StreamPrediction &ubtb_prediction,
                                           const StreamPrediction &main_predictor_prediction)
{
    return false;
}

void DecoupledBranchPred::overrideStream()
{
}

void DecoupledBranchPred::overrideUpdateUBTB()
{
}

void DecoupledBranchPred::controlSquash(const InstSeqNum control_sn, const TheISA::PCState control_pc,
                                        const TheISA::PCState &corr_target, bool isConditional, bool isIndirect,
                                        bool actually_taken)
{
    /* two cases:
     *  sn exists in bpHistory, which indicates that BP already knows it is a control
     *  sn does not exist in bpHistory, which indicates that BP treates is as a non-control or not-taken branch
     */
    auto it = bpHistory.find(control_sn);
    if (it != bpHistory.end()) {
        // BP already knows it is a control
    } else {
        // BP treats it as a non-control or not-taken branch
    }
}

void DecoupledBranchPred::nonControlSquash(const InstSeqNum squashed_sn)
{
    for (auto &control : bpHistory) {
        if (control.first >= squashed_sn) {
            // squash it and release resource
        } else {
            break;
        }
    }
}

void DecoupledBranchPred::commitInst(const InstSeqNum committedSeq)
{
    // commit controls in local prediction history buffer to committedSeq
    // mark all committed control instructions as correct
    for (auto &control : bpHistory) {
        if (control.first <= committedSeq) {
            // This is a predicted taken control, mark it as correct
        } else {
            break;
        }
    }
}