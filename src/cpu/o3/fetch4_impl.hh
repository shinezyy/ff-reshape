#include "cpu/o3/fetch_pipe.hh"
#include "debug/Fetch.hh"
#include "debug/Fetch4.hh"

// IF4 do branch predict
template<class Impl>
void
FetchStage4<Impl>::fetch(bool &status_change)
{
    DPRINTF(Fetch4, "FetchStage4.fetch() is called\n");
    // ThreadID tid = this->upper->getFetchingThread();
    ThreadID tid = 0;

    unsigned fetchBufferSize = this->upper->fetchBufferSize;
    int instSize = this->upper->instSize;
    unsigned fetchWidth = this->upper->fetchWidth;
    unsigned fetchQueueSize = this->upper->fetchQueueSize;

    assert(!this->upper->cpu->switchedOut());

    if (tid == InvalidThreadID) {
        return;
    }

    TheISA::PCState thisPC;

    // squash logic
    if (this->fetchStatus[tid] == this->Squashing) {
        thisPC = 0;
        this->pcReg[tid] = 0;
        decoder[tid]->reset();
        this->fetchStatus[tid] = this->Idle;
        DPRINTF(Fetch4, "fetch4: Squashing\n");
        return;
    }

    this->fetchStatus[tid] =
        static_cast<typename BaseFetchStage<Impl>::ThreadStatus>
        (this->upper->toFetch4->lastStatus);

    // The current PC.
    if (this->upper->toFetch4->lastStatus == this->Running) {
        thisPC = this->upper->toFetch4->pc;
        this->pcReg[tid] = thisPC;
    } else {
        thisPC = this->pcReg[tid];
    }

    DPRINTF(Fetch4, "fetch4: thisPC = %08lx\n", thisPC.pc());

    TheISA::PCState nextPC = thisPC;

    DPRINTF(Fetch4, "fetch4: fetchStatus=%s\n", this->printStatus(this->fetchStatus[tid]));

    //---------------------------------------------------------//
    //                   decode and predict                    //
    //---------------------------------------------------------//

    if (this->upper->stalls[tid].decode || this->fetchStatus[tid] != this->Running) {
        DPRINTF(Fetch4, "[tid:%i] *Stall* if4 pc:%x to decode\n", tid, thisPC);
        return;
    }
    Addr pcOffset = fetchOffset[tid];
    Addr fetchAddr = (thisPC.instAddr() + pcOffset) & BaseCPU::PCMask;
    Addr fetchBufferBlockPC = this->upper->bufferAlignPC(thisPC.instAddr() & BaseCPU::PCMask,
            this->upper->fetchBufferMask);

    unsigned numInsts = fetchBufferSize / instSize;
    unsigned blkOffset = (fetchAddr - fetchBufferBlockPC) / this->upper->instSize;

    TheISA::MachInst *cacheInsts =
        reinterpret_cast<TheISA::MachInst *>(this->upper->toFetch4->cacheData);

    // THis run once when system wtartup
    if (cacheInsts == nullptr) { return; }

    DPRINTF(Fetch4, "==========\n");
    for (int i = 0; i < numInsts; i++) {
        DPRINTF(Fetch4, "%08x\n", cacheInsts[i]);
    }
    DPRINTF(Fetch4, "==========\n");

    bool predictedBranch = false;

    bool quiesce = false;

    StaticInstPtr staticInst = NULL;

    while (numInst < this->upper->fetchWidth
           && this->upper->fetchQueue[tid].size() < this->upper->fetchQueueSize
           && !predictedBranch && !quiesce)
    {
        fetchAddr = (thisPC.instAddr() + pcOffset) & BaseCPU::PCMask;

        fetchBufferBlockPC = this->upper->bufferAlignPC(fetchAddr, this->upper->fetchBufferMask);

        if (blkOffset >= numInsts) { break; }

        TheISA::MachInst inst = cacheInsts[blkOffset];

        decoder[tid]->moreBytes(thisPC, fetchAddr, inst);

        if (decoder[tid]->needMoreBytes()) {
            blkOffset++;
            fetchAddr += instSize;
            pcOffset += instSize;
        }

        do {
            if (decoder[tid]->instReady()) {
                staticInst = decoder[tid]->decode(thisPC);

                pcOffset = 0;
            } else {
                break;
            }

            DynInstPtr instruction =
                this->upper->buildInst(tid, staticInst, nullptr,
                          thisPC, nextPC, true);
            numInst++;

            nextPC = thisPC;
            DPRINTF(Fetch4, "Compressed: %i, This PC: 0x%x, NPC: 0x%x\n",
                    thisPC.compressed(),
                    thisPC.pc(),
                    thisPC.npc());

            predictedBranch = thisPC.branching() ||
                this->upper->lookupAndUpdateNextPC(instruction, nextPC);

            if (predictedBranch) {
                // predicted backward branch
                DPRINTF(Fetch, "Taken branch detected with PC : 0x%x => 0x%x\n",
                        thisPC.pc(),
                        thisPC.npc());
                this->upper->fetchSquash(nextPC, tid);
            }

            thisPC = nextPC;

            if (instruction->isQuiesce()) {
                DPRINTF(Fetch4,
                        "Quiesce instruction encountered, halting fetch!\n");
                // setFetchStatus(QuiescePending, tid);
                status_change = true;
                quiesce = true;
                break;
            }

        } while (decoder[tid]->instReady() &&
                numInst < fetchWidth &&
                this->upper->fetchQueue[tid].size() < fetchQueueSize);
    } // outer loop end

    fetchOffset[tid] = pcOffset;

    if (numInst > 0) {
        this->wroteToTimeBuffer = true;
    }

    delete this->upper->toFetch4->cacheData;

    //---------------------------------------------------------//
    //                   decode and predict                    //
    //---------------------------------------------------------//

    // DynInstPtr instruction =
    //     this->upper->buildInst(tid, staticInst, nullptr,
    //                 thisPC, nextPC, true);

    // lookupAndUpdateNextPC(instruction, nextPC);

    numInst = 0;

    // if (!this->upper->stalls[tid].decode && this->fetchStatus[tid] == this->Running) {
    if (!this->upper->stalls[tid].decode) {
        // this->upper->toDecode->pc[this->upper->toDecode->size++] = thisPC;
        DPRINTF(Fetch4, "[tid:%i] Sending if4 pc:%x to decode\n", tid, thisPC);
        this->wroteToTimeBuffer = true;
        this->upper->stalls[tid].fetch4 = false;
    } else {
        DPRINTF(Fetch4, "[tid:%i] *Stall* if4 pc:%x to decode\n", tid, thisPC);
        this->upper->stalls[tid].fetch4 = true;
    }

}

template <class Impl>
bool
FetchStage4<Impl>::lookupAndUpdateNextPC(
        const DynInstPtr &inst, TheISA::PCState &nextPC)
{
    // Do branch prediction check here.
    // A bit of a misnomer...next_PC is actually the current PC until
    // this function updates it.
    bool predict_taken;
    bool cpc_compressed = nextPC.compressed();

    if (!inst->isControl()) {
        DPRINTF(Fetch, "Advancing PC from %s", nextPC);
        TheISA::advancePC(nextPC, inst->staticInst);
        DPRINTFR(Fetch, " to %s\n", nextPC);

        inst->setPredTarg(nextPC);
        inst->setPredTaken(false);
        return false;
    }

    ThreadID tid = inst->threadNumber;
    Addr branch_pc = nextPC.pc();
    predict_taken = branchPred->predict(inst->staticInst, inst->seqNum,
                                        nextPC, tid);

    [[maybe_unused]] bool real_pred_taken = cpc_compressed ?
        branch_pc + 2 != nextPC.pc() :
        branch_pc + 4 != nextPC.pc();

    if (predict_taken) {
        DPRINTF(Fetch, "[tid:%i] [sn:%llu] Branch at PC %#x "
                "predicted to be taken to %s\n",
                tid, inst->seqNum, inst->pcState().instAddr(), nextPC);
    } else {
        DPRINTF(Fetch, "[tid:%i] [sn:%llu] Branch at PC %#x "
                "predicted to be not taken\n",
                tid, inst->seqNum, inst->pcState().instAddr());
    }

    DPRINTF(Fetch, "[tid:%i] [sn:%llu] Branch at PC %#x "
            "predicted to go to %s\n",
            tid, inst->seqNum, inst->pcState().instAddr(), nextPC);
    inst->setPredTarg(nextPC);
    inst->setPredTaken(predict_taken);

    /*++fetchStats.branches;

    if (predict_taken) {
        ++fetchStats.predictedBranches;
    }*/

    return predict_taken;
}

template<class Impl>
std::string
FetchStage4<Impl>::name() const
{
    return std::string(".Fetch4");
}
