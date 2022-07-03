#include "cpu/pred/decoupled_branch_pred.hh"
#include "debug/DecoupleBP.hh"
#include "cpu/o3/decoupled_fetch.hh"

DecoupledBranchPred::DecoupledBranchPred(const Params &params)
    : SimObject(params),
    streamPred(params.stream_pred),
    streamUBTB(params.stream_ubtb)
{
    commitHistory.resize(historyBits, 0);
    speculativeHistory.resize(historyBits, 0);
    s2CtrlPC = 0x80000000;

    // TODO: remove this
    ftqSize = 256;
    fetchStreamQueueSize = 64;
    streamMiss = true;
    s0StreamPC = 0x80000000;
    ftqEnqPC = 0x80000000;
    fetchReadFtqEntryBufferValid = false;
    fetchReadFtqEntryBuffer = std::make_pair(0, FtqEntry());

    s0History.resize(historyBits, 0);
}

void DecoupledBranchPred::tryToEnqFtq()
{
    DPRINTF(DecoupleBP, "Try to enq FTQ\n");
    if (ftq.size() < ftqSize) {
        // ftq can accept new cache lines,
        // get cache lines from fetchStreamQueue
        if (fetchStreamQueue.size() != 0) {
            // find current stream with ftqEnqfsqID in fetchStreamQueue
            auto it = fetchStreamQueue.find(ftqEnqFsqID);
            if (it != fetchStreamQueue.end()) {
                auto stream_to_enq = it->second;
                // DPRINTF(DecoupleBP, "tryToEnqFtq: enq stream %d, pc:0x%lx\n", ftqEnqFsqID, s0StreamPC);
                DPRINTF(DecoupleBP,
                        "enq PC: %#lx, stream to enq to FTQ: %#lx, s0StreamPC: %#lx, end of a stream: %i, "
                        "Predicted stream end: %#lx\n",
                        ftqEnqPC, stream_to_enq.streamStart, s0StreamPC, stream_to_enq.streamEnded,
                        stream_to_enq.predStreamEnd);
                // dumpFsq("for debugging steam enq");
                DPRINTF(DecoupleBP, "ftqEnqPC < stream_to_enq.predStreamEnd: %d\n",
                        ftqEnqPC < stream_to_enq.predStreamEnd);
                // assert((ftqEnqPC <= s0StreamPC && !stream_to_enq.streamEnded)
                if (stream_to_enq.streamEnded) {
                    assert(ftqEnqPC < stream_to_enq.predStreamEnd);
                }
                auto ftq_entry = FtqEntry();
                if (!stream_to_enq.hasEnteredFtq) {
                    ftq_entry.startPC = stream_to_enq.streamStart;
                    stream_to_enq.hasEnteredFtq = true;
                    it->second = stream_to_enq;
                } else {
                    ftq_entry.startPC = ftqEnqPC;
                }
                ftqEnqPC = alignToCacheLine(ftq_entry.startPC + 0x40);
                DPRINTF(DecoupleBP, "Update ftqEnqPC from %#lx to %#lx\n", ftq_entry.startPC, ftqEnqPC);

                bool stream_ended = stream_to_enq.streamEnded;
                // check if this is the last cache line of the stream
                bool end_is_within_line = stream_to_enq.predStreamEnd - alignToCacheLine(ftq_entry.startPC) <= 0x40;
                // whether the first byte of the branch instruction lies in this cache line
                bool branch_is_within_line = stream_to_enq.predBranchAddr - alignToCacheLine(ftq_entry.startPC) < 0x40;

                ftq_entry.fsqID = ftqEnqFsqID;
                if ((end_is_within_line || branch_is_within_line) && stream_ended) {
                    ftq_entry.endPC = stream_to_enq.predStreamEnd;
                    ftq_entry.taken = true;
                    ftq_entry.takenPC = stream_to_enq.predBranchAddr;
                    ftq_entry.target = stream_to_enq.predTarget;
                    // done with this stream
                    dumpFsq("Before update ftqEnqFsqID");
                    DPRINTF(DecoupleBP, "done stream %lu entering ftq %lu\n", ftqEnqFsqID, ftqID);
                    ftqEnqFsqID++;
                    ftqEnqPC = stream_to_enq.predTarget;
                    DPRINTF(DecoupleBP, "Update ftqEnqPC to %#lx, enqFSQID to %u, because stream ends\n", ftqEnqPC,
                            ftqEnqFsqID);
                } else {
                    // align to the end of current cache line
                    ftq_entry.endPC = alignToCacheLine(ftq_entry.startPC + 0x40);

                    ftq_entry.taken = false;
                    ftq_entry.takenPC = 0;
                }
                ftq.insert(std::make_pair(ftqID, ftq_entry));
                ftqID++;
                DPRINTF(DecoupleBP, "a %s stream inc ftqID: %lu -> %lu\n",
                    stream_ended ? "ended" : "miss", ftqID-1, ftqID);
                printFtqEntry(ftq_entry, "Insert to FTQ");
            } else {
                DPRINTF(DecoupleBP, "FTQ enq FSQ ID %u is not found\n", ftqEnqFsqID);
            }
        } else {
            // no stream that have not entered ftq
            DPRINTF(DecoupleBP, "no stream to enter ftq in fetchStreamQueue\n");
        }
    } else {
        DPRINTF(DecoupleBP, "FTQ is full\n");
    }
}

void DecoupledBranchPred::makeNewPredictionAndInsertFsq()
{
    FetchStream entry;
    if (s0UbtbPred.valid) {
        entry.streamStart = s0UbtbPred.bbStart;
        entry.streamEnded = true;
        entry.predStreamEnd = s0UbtbPred.bbStart + s0UbtbPred.streamLength;
        entry.predBranchAddr = s0UbtbPred.bbEnd;
        entry.predTarget = s0UbtbPred.nextStream;
        s0StreamPC = s0UbtbPred.nextStream;
        DPRINTF(DecoupleBP, "Valid s0UbtbPred: %#lx-[%#lx, %#lx) --> %#lx\n", entry.streamStart, entry.predBranchAddr,
                entry.predStreamEnd, entry.predTarget);
    } else {
        DPRINTF(DecoupleBP, "No valid prediction, gen missing stream: %#lx -> ...\n", s0StreamPC);
        entry.streamStart = s0StreamPC;
        entry.streamEnded = false;
        entry.history.resize(historyBits, 0);
        // TODO: when hit, the remaining signals should be the prediction result
    }
    entry.setDefaultResolve();
    fetchStreamQueue.emplace(fsqID, entry);

    // only if the stream is predicted to be ended can we inc fsqID
    if (entry.streamEnded) {
        fsqID++;
    }
    DPRINTF(DecoupleBP, "Insert fetch stream %lu, FSQ ID update: -> %lu\n", fsqID-1, fsqID);
    printFsqEntry(entry);
}

void DecoupledBranchPred::tryToEnqFsq()
{
    if (fetchStreamQueue.size() < fetchStreamQueueSize) {
        // if queue empty, should make predictions
        if (fetchStreamQueue.empty()) {
            DPRINTF(DecoupleBP, "FSQ is empty\n");
            makeNewPredictionAndInsertFsq();
        } else {
            auto it = fetchStreamQueue.end();
            it--;
            auto &back = it->second;
            if (back.streamEnded || back.exeEnded) {
                DPRINTF(DecoupleBP, "FSQ is not empty, and stream has ended\n");
                // make new predictions
                makeNewPredictionAndInsertFsq();
            } else {
                DPRINTF(DecoupleBP, "FSQ is not empty, but stream has not ended\n");
                bool hit = false; // TODO: use prediction result
                // if hit, do something
                if (hit) {
                    // TODO: use prediction result
                    back.streamEnded = true;
                    back.predStreamEnd = 0;
                    back.predTarget = 0;
                    back.predBranchAddr = 0;
                    back.predBranchType = 0;
                    back.hasEnteredFtq = false;
                    s0StreamPC = back.predTarget;
                    DPRINTF(DecoupleBP, "fsq entry of id %lu modified to:\n", it->first);
                    // pred ended, inc fsqID
                    fsqID++;
                    printFsqEntry(back);
                } else {
                    // streamMiss = true;
                    s0StreamPC += 0x40;
                    DPRINTF(DecoupleBP, "s0StreamPC update to %#lx\n", s0StreamPC);
                }
            }
        }
    } else {
        DPRINTF(DecoupleBP, "FSQ is full: %lu\n", fetchStreamQueue.size());
    }
    DPRINTF(DecoupleBP,
            "Exit ticking BP, s0StreamPC = %x, ftqEnqPC=%x, ftqEnqFsqID=%lu,"
            " ftqID=%lu, fsqID=%lu, fetchFtqID=%lu, bufferValid=%d\n",
            s0StreamPC, ftqEnqPC, ftqEnqFsqID, ftqID, fsqID, fetchFtqID, fetchReadFtqEntryBufferValid);
    DPRINTF(DecoupleBP, "ftq entires %d, fsq entries %d\n", ftq.size(), fetchStreamQueue.size());
}


void DecoupledBranchPred::tick()
{
    DPRINTF(DecoupleBP,
            "Ticking BP, s0StreamPC = %x, ftqEnqPC=%x,"
            " ftqEnqFsqID=%lu, ftqID=%lu, fsqID=%lu, fetchFtqID=%lu, bufferValid=%d\n",
            s0StreamPC, ftqEnqPC, ftqEnqFsqID, ftqID, fsqID, fetchFtqID, fetchReadFtqEntryBufferValid);
    DPRINTF(DecoupleBP, "ftq entires %d, fsq entries %d\n", ftq.size(), fetchStreamQueue.size());

    if (!squashing) {
        s0UbtbPred = streamUBTB->getStream();

        tryToEnqFtq();
        tryToEnqFsq();

    } else {
        DPRINTF(DecoupleBP, "Squashing, skip this cycle\n");
    }


    // s2
    // get stream from UBTB

    // bool inconsistent = check_prediction(s1BackingPred, s1UbtbPred);
    // bool overriding = false;
    // if (inconsistent) {
    //     overrideStream();
    //     overrideUpdateUBTB();  // do we update it after override?
    // }
    // if (s1BackingPred.valid) {
    //     incPredictionID();
    //     add2FTQ(s1BackingPred, predictionID);
    // }
    // s2Pred = s1BackingPred;
    // s1BackingPred.valid = false;
    // updateS2Hist();

    // // s1
    // // get stream prediction initated XX cycles before
    // auto [branch_pc, stream_payload] = streamPred->getStreamS1();
    // // sanity check
    // if (branch_pc) {  // pc = 0 indicates no valid prediction
    //     // the pc of return predicton must be sent X cycle ago
    //     assert(branch_pc == s1StreamPC);
    // }
    // if (stream_payload.endIsRet && !ras.empty()) {
    //     stream_payload.nextStream = ras.top().instAddr();
    //     stream_payload.rasUpdated = true;
    // }
    // s1StreamPC = s0StreamPC;

    // // s1BackingPred = stream_payload;
    // // temporarily s1BackingPred = s0ubtbPred;
    // if (s0UbtbPred.valid) {
    //     s1BackingPred = s0UbtbPred;
    //     DPRINTF(DecoupleBP, "Forward s0 ubtb pred (%#lx..%u) to s1 backing pred\n", s0UbtbPred.bbStart,
    //             s0UbtbPred.streamLength);
    // }

    // s1UbtbPred = s0UbtbPred;
    // updateS1Hist();

    // // s0
    // streamPred->putPCHistory(s0StreamPC, s0History);

    // if (s0UbtbPred.valid) {
    //     s0StreamPC = s0UbtbPred.nextStream;
    //     DPRINTF(DecoupleBP, "Update s0 stream (%#lx..%u) with ubtb\n", s0UbtbPred.bbStart,
    //             s0UbtbPred.streamLength);
    // }
    // if (overriding) {
    //     s0StreamPC = s2Pred.nextStream;
    // }
    // updateS0Hist();
    streamUBTB->putPCHistory(s0StreamPC, s0History);

    // ftq is at the next stage of fetchStreamQueue
    // so we deal with it first

    // currently squashing only last for one cycle, change it if assuming longer squashing time
    squashing = false;
}

void DecoupledBranchPred::updateS0Hist()
{
}

void DecoupledBranchPred::updateS1Hist()
{
}

void DecoupledBranchPred::updateS2Hist()
{
}

bool
DecoupledBranchPred::tryToFillFtqEntryBuffer()
{
    if (!fetchReadFtqEntryBufferValid || fetchReadFtqEntryBuffer.first != fetchFtqID) {
        auto iter = ftq.find(fetchFtqID);
        if (iter != ftq.end()) {
            DPRINTF(DecoupleBP, "found ftq entry with id %lu, writing to fetchReadFtqEntryBuffer\n", fetchFtqID);
            fetchReadFtqEntryBufferValid = true;
            fetchReadFtqEntryBuffer.first = fetchFtqID;
            fetchReadFtqEntryBuffer.second = iter->second;
        } else {
            DPRINTF(DecoupleBP, "not found ftq entry with id %lu, buffervalid %d\n",
                fetchFtqID, fetchReadFtqEntryBufferValid);
            if (ftq.size() > 0) {
                --iter;
                DPRINTF(DecoupleBP, "last entry of ftq is %lu\n", iter->first);
                assert(iter->first < fetchFtqID);
            }
            return false;
        }
    }
    return true;
}

// taken, target, ftq_entry_done/empty
std::tuple<bool, TheISA::PCState, bool>
DecoupledBranchPred::willTaken(TheISA::PCState &pc)
{
    // if taken, store prediction history here
    
    // read ftq using fetchFtqID and check if we run out of entries
    // if so, we need to stall the fetch engine
    DPRINTF(DecoupleBP, "looking up pc %#lx\n", pc.instAddr());
    auto has_sth_to_fetch = fetchReadFtqEntryBufferValid && fetchReadFtqEntryBuffer.first == fetchFtqID;
    DPRINTF(DecoupleBP, "fetchFtqID %lu, fetchReadFtqEntryBuffer id %lu\n", fetchFtqID, fetchReadFtqEntryBuffer.first);
    if (!has_sth_to_fetch) {
        DPRINTF(DecoupleBP, "No ftq entry to fetch, return dummy prediction\n");
        return std::make_tuple(false, TheISA::PCState(0), true);
    }
    const auto &ftq_entry_to_fetch = fetchReadFtqEntryBuffer.second;
    // found corresponding entry
    auto start    = ftq_entry_to_fetch.startPC;
    auto end      = ftq_entry_to_fetch.endPC;
    auto taken_pc = ftq_entry_to_fetch.takenPC;
    DPRINTF(DecoupleBP, "Responsing fetch with");
    printFtqEntry(ftq_entry_to_fetch, "");
    assert(pc.instAddr() <= end && pc.instAddr() >= start);
    bool taken = pc.instAddr() == taken_pc && ftq_entry_to_fetch.taken;
    bool run_out_of_this_entry = pc.nextInstAddr() >= end; // npc could be end + 2
    if (run_out_of_this_entry) {
        // dequeue the entry
        ftq.erase(fetchFtqID);
        DPRINTF(DecoupleBP, "running out of ftq entry %lu\n", fetchFtqID);
        fetchFtqID++;
    }
    return std::make_tuple(taken, TheISA::PCState(ftq_entry_to_fetch.target), run_out_of_this_entry);
}

void
DecoupledBranchPred::notifyStreamSeq(const InstSeqNum seq)
{
    // We cannot index BP history with Instseq num, because it does not exists
    // when make a prediction.
}

void DecoupledBranchPred::add2FTQ(const StreamPrediction &fetchStream, PredictionID id)
{
    DPRINTF(DecoupleBP, "Add prediction %u to FTQ, start: %lx\n", id, fetchStream.bbStart);
    // ftq.emplace(id, StreamPredictionWithID{fetchStream, id});
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

void DecoupledBranchPred::controlSquash(const FtqID inst_ftq_id, const FsqID inst_fsq_id,
                                        const TheISA::PCState control_pc, const TheISA::PCState &corr_target,
                                        bool is_conditional, bool is_indirect,
                                        bool actually_taken, const InstSeqNum seq)
{
    DPRINTF(DecoupleBP,
            "Control squash: ftq_id=%lu, fsq_id=%lu, control_pc=0x%lx, corr_target=0x%lx, is_conditional=%u, "
            "is_indirect=%u, actually_taken=%u, branch seq: %lu\n",
            inst_ftq_id, inst_fsq_id, control_pc.instAddr(), corr_target.instAddr(), is_conditional, is_indirect,
            actually_taken, seq);

    squashing = true;

    // streamPred->update(pred_id, it->second.streamStart, control_pc.instAddr(), corr_target.instAddr(),
    // is_conditional,
    //                    is_indirect, actually_taken, it->second.history);
    s0StreamPC = corr_target.instAddr();

    // ftq.erase(pred_id);
    // only implemented 
    ftq.clear();

    dumpFsq("Before squash");
    auto it = fetchStreamQueue.find(inst_fsq_id);
    // DPRINTF(DecoupleBP, "1.5 fsqID %lu, inst_fsq_id %lu\n", fsqID, inst_fsq_id);

    // miss entry, write end info in it
    assert(it != fetchStreamQueue.end());
    auto &fsq_entry = it->second;
    fsq_entry.exeEnded = true;
    fsq_entry.exeBranchAddr = control_pc.instAddr();
    fsq_entry.exeBranchType = 0;
    fsq_entry.exeTarget = corr_target.instAddr();
    fsq_entry.branchSeq = seq;
    // it->second = fsq_entry;
    // unsigned control_inst_size = control_pc.nextInstAddr() - control_pc.instAddr();
    unsigned control_inst_size = control_pc.compressed() ? 2 : 4;
    fsq_entry.exeStreamEnd = fsq_entry.exeBranchAddr + control_inst_size;

    if (actually_taken) {
        DPRINTF(DecoupleBP, "a miss flow was redirected by taken branch, new fsq entry is:\n");
        printFsqEntry(fsq_entry);

        streamUBTB->update(inst_fsq_id, fsq_entry.streamStart, control_pc.instAddr(), corr_target.instAddr(),
                           is_conditional, is_indirect, control_inst_size, actually_taken, fsq_entry.history);

        // clear younger fsq entries
        auto erase_it = fetchStreamQueue.upper_bound(inst_fsq_id);
        while (erase_it != fetchStreamQueue.end()) {
            DPRINTF(DecoupleBP, "erasing entry %lu\n", erase_it->first);
            printFsqEntry(erase_it->second);
            fetchStreamQueue.erase(erase_it++);
        }
        ftqEnqFsqID = inst_fsq_id + 1;
        fsqID = inst_fsq_id + 1;
    } else {
        DPRINTF(DecoupleBP, "a taken flow was redirected by NOT taken branch, new fsq entry is:\n");
        fsq_entry.exeEnded = false; // its ned has not be found yet
        fsq_entry.streamEnded = false; // convert it to missing stream
        fsq_entry.predStreamEnd = 0;
        fsq_entry.predBranchAddr = 0;
        ftqEnqFsqID = inst_fsq_id;
        fsqID = inst_fsq_id;

        printFsqEntry(fsq_entry);
        // streamUBTB->update(inst_fsq_id, fsq_entry.streamStart, control_pc.instAddr(), corr_target.instAddr(),
        //                    is_conditional, is_indirect, control_inst_size, actually_taken, fsq_entry.history);

        // clear younger fsq entries
        auto erase_it = fetchStreamQueue.upper_bound(inst_fsq_id);
        while (erase_it != fetchStreamQueue.end()) {
            DPRINTF(DecoupleBP, "erasing entry %lu\n", erase_it->first);
            printFsqEntry(erase_it->second);
            erase_it = fetchStreamQueue.erase(erase_it);
        }

    }
    dumpFsq("After squash");
    // if (fetchStreamQueue.size() >= 1) {
    //     auto debug_it = fetchStreamQueue.end();
    //     debug_it--;
    //     DPRINTF(DecoupleBP, "debug: after squash fetchStreamQueue end id %lu, entry below\n", debug_it->first);
    //     printFsqEntry(it->second);
    // }

    // DPRINTF(DecoupleBP, "2 fsqID %lu, inst_fsq_id %lu\n", fsqID, inst_fsq_id);

    ftqID = inst_ftq_id + 1;
    ftqEnqPC = corr_target.instAddr();

    fetchFtqID = inst_ftq_id + 1;
    fetchReadFtqEntryBufferValid = false;

    s0UbtbPred.valid = false;
    // DPRINTF(DecoupleBP, "3 fsqID %lu, inst_fsq_id %lu\n", fsqID, inst_fsq_id);

    DPRINTF(DecoupleBP, "After squash,");
    DPRINTF(DecoupleBP, "FTQ size: %u FSQ size: %u\n", ftq.size(), fetchStreamQueue.size());
    DPRINTF(DecoupleBP,
            "ftqEnqPC set to %x, s0StreamPC set to %x, ftqEnqFsqID %lu, fetchFtqID %lu, fsqID %lu, ftqID %lu\n",
            ftqEnqPC, s0StreamPC, ftqEnqFsqID, fetchFtqID, fsqID, ftqID);
    // here, in fact we make another prediction
}

void DecoupledBranchPred::nonControlSquash(const FtqID inst_ftq_id, const FsqID inst_fsq_id,
                                           const TheISA::PCState inst_pc, const InstSeqNum seq)
{
    DPRINTF(DecoupleBP, "non control squash: ftq_id: %lu, fsq_id: %lu, inst_pc: %x, seq: %lu\n",
            inst_ftq_id, inst_fsq_id, inst_pc.instAddr(), seq);
    squashing = true;

    // Should not update s0StreamPC!!!
    // s0StreamPC = inst_pc.nextInstAddr();

    // ftq.erase(pred_id);
    // only implemented 
    ftq.clear();

    dumpFsq("before non-control squash");
    // make sure the fsq entry still exists
    auto it = fetchStreamQueue.find(inst_fsq_id);
    assert(it != fetchStreamQueue.end());

    // clear potential exe results and set to predicted result
    auto &fsq_entry = it->second;
    fsq_entry.setDefaultResolve();
    fsq_entry.branchSeq = -1;

    // fetching from the original predicted fsq entry
    // since this is not a mispredict
    // but we should use a new ftq entry id
    ftqID = inst_ftq_id + 1;
    fsqID = inst_fsq_id;

    ftqEnqPC = inst_pc.nextInstAddr();
    ftqEnqFsqID = inst_fsq_id;
    fetchFtqID = inst_ftq_id + 1;
    fetchReadFtqEntryBufferValid = false;
    // DPRINTF(DecoupleBP, "3 fsqID %lu, inst_fsq_id %lu\n", fsqID, inst_fsq_id);

    DPRINTF(DecoupleBP, "FTQ size: %u FSQ size: %u\n", ftq.size(), fetchStreamQueue.size());
    DPRINTF(DecoupleBP,
            "ftqEnqPC set to %x, s0StreamPC remains to be%x,"
            " ftqEnqFsqID %lu, fetchFtqID %lu, fsqID %lu, ftqID %lu\n",
            ftqEnqPC, s0StreamPC, ftqEnqFsqID, fetchFtqID, fsqID, ftqID);
}

void DecoupledBranchPred::commitStream(const FsqID fsq_id)
{
    // commit controls in local prediction history buffer to committedSeq
    // mark all committed control instructions as correct
    // do not need to dequeue when empty
    if (fetchStreamQueue.empty()) return;
    auto it = fetchStreamQueue.begin();
    while (it != fetchStreamQueue.end() && fsq_id >= it->first) {
        // TODO: do update here

        // dequeue
        DPRINTF(DecoupleBP, "dequeueing stream id: %lu, entry below:\n", it->first);
        it = fetchStreamQueue.erase(it);
    }
    DPRINTF(DecoupleBP, "after commit stream, fetchStreamQueue size: %lu\n", fetchStreamQueue.size());
    printFsqEntry(it->second);
}

void DecoupledBranchPred::dumpFsq(const char* when)
{
    DPRINTF(DecoupleBP, "dumping fsq entries %s...\n", when);
    for (auto it = fetchStreamQueue.begin(); it != fetchStreamQueue.end(); it++) {
        DPRINTFR(DecoupleBP, "FsqID %lu, ", it->first);
        printFsqEntry(it->second);
    }
}