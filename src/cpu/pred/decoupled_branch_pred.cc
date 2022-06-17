#include "cpu/pred/decoupled_branch_pred.hh"

DecoupledBranchPred::DecoupledBranchPred()
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
    auto [branch_pc, stream_payload] = streamPred.getStreamS1();
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
    streamPred.putPCHistory(s0CtrlPC, s0History);
    streamUBTB.putPCHistory(s0CtrlPC, s0History);
    s0UbtbPred = streamUBTB.getStream();
    s0CtrlPC = s0UbtbPred.nextStream;
    if (overriding) {
        s0CtrlPC = s2Pred.nextStream;
    }
}