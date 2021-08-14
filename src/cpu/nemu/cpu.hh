//
// Created by zyy on 2021/5/7.
//

#ifndef __GEM5_NEMUCPU_HH__
#define __GEM5_NEMUCPU_HH__

#include <algorithm>
#include <atomic>
#include <iterator>
#include <mutex>
#include <queue>
#include <thread>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include "cpu/base.hh"
#include "cpu/nemu/include/protocal/instr_trace.h"
#include "cpu/simple_thread.hh"
#include "debug/NemuCPU.hh"
#include "params/NemuCPU.hh"
#include "sim/sim_events.hh"

class NemuCPU: public BaseCPU
{
  public:
    NemuCPU(const NemuCPUParams &params);

    SimpleThread *thread;

    ThreadContext *tc;

    void init() override;

  protected:
    EventFunctionWrapper tickEvent;

    int nNEMU{1};

    CountedExitEvent *execCompleteEvent;

    uint64_t commitInstCount{};

  public:
    // class NemuCpuPort : public RequestPort
    // {
    //   public:
    //     NemuCpuPort(const std::string &_name, NemuCPU *_cpu)
    //             : RequestPort(_name, _cpu), cpu(_cpu)
    //     { }

    //   protected:
    //     /** KVM cpu pointer for finishMMIOPending() callback */
    //     NemuCPU *cpu;

    //     bool recvTimingResp(PacketPtr pkt) override;

    //     void recvReqRetry() override;
    // };

    /** Port for data requests */
    // NemuCpuPort dataPort;

    /** Unused dummy port for the instruction interface */
    // NemuCpuPort instPort;

    void wakeup(ThreadID tid) override;

    Counter totalInsts() const override;

    Counter totalOps() const override;

    void tick();

    void activateContext(ThreadID thread_num) override;
 private:

   void setupFetchRequest(const RequestPtr &req);

   bool dispatch(const ExecTraceEntry &entry);

   ExecTraceEntry lastEntry;
   int lastEntryType;

   void processFetch(const VPAddress &addr_pair);

   void processLoad(const VPAddress &addr_pair, Addr pc);

   void processStore(const VPAddress &addr_pair, Addr pc);

   void sendPacket(RequestPort &port, const PacketPtr &pkt);

   /**
     * An AtomicCPUPort overrides the default behaviour of the
     * recvAtomicSnoop and ignores the packet instead of panicking. It
     * also provides an implementation for the purely virtual timing
     * functions and panics on either of these.
     */
   class AtomicCPUPort : public RequestPort
   {

   public:
     AtomicCPUPort(const std::string &_name, NemuCPU*_cpu)
         : RequestPort(_name, _cpu)
     {
     }

   protected:
     bool
     recvTimingResp(PacketPtr pkt)
     {
       panic("Atomic CPU doesn't expect recvTimingResp!\n");
     }

     void
     recvReqRetry()
     {
       panic("Atomic CPU doesn't expect recvRetry!\n");
     }

    };

    class AtomicCPUDPort : public AtomicCPUPort
    {

      public:
        AtomicCPUDPort(const std::string &_name, NemuCPU *_cpu)
            : AtomicCPUPort(_name, _cpu), cpu(_cpu)
        {
            cacheBlockMask = ~(cpu->cacheLineSize() - 1);
        }

        bool isSnooping() const { return true; }

        Addr cacheBlockMask;
      protected:
        NemuCPU *cpu;

        virtual Tick recvAtomicSnoop(PacketPtr pkt);
        virtual void recvFunctionalSnoop(PacketPtr pkt);
    };

    AtomicCPUPort icachePort;
    AtomicCPUDPort dcachePort;
    Port &getDataPort() override {return dcachePort;}
    Port &getInstPort() override {return icachePort;}


    RequestPtr ifetchReq;
    RequestPtr dataReadReq;
    RequestPtr dataWriteReq;
    RequestPtr dataAmoReq;

    TheISA::MachInst dummyInst;

    uint8_t dummyData[64];

    bool inSameBlcok(Addr blk_addr, Addr addr);

    uint64_t instCount{};

    const uint64_t maxInsts;

    enum CPUState {
      Invalid = 0,
      Running,
      Stopping,
      Stopped
    } cpuState;

  private:
    void setBootLoaderPath(const NemuCPUParams &params);

  public:

    struct EntryPAddr {
        typedef ProtoAddr result_type;
        result_type operator() (const ExecTraceEntry &e) const
        {
            return e.memAddr.p;
        }
    };

    typedef typename boost::multi_index::multi_index_container<
        ExecTraceEntry,
        boost::multi_index::indexed_by<
            boost::multi_index::sequenced<>,
            boost::multi_index::hashed_unique<EntryPAddr>
            >
        > MRUList;

    typedef typename MRUList::iterator MRUIter;



  private:
    const unsigned assoc{8}; // 8 way
    const unsigned cacheBlockSize{64}; // 64 Byte
    const unsigned maxCacheSize{4*(1 << 20)};
    const unsigned numBlocks{maxCacheSize/cacheBlockSize};
    const unsigned numSets{maxCacheSize/cacheBlockSize/assoc};
    const unsigned setMask{numSets - 1}; // 32 MB
    std::vector<MRUList> sets;
    MRUList mru;
    Addr extractSet(Addr addr);
    void insertMemTrace(const ExecTraceEntry &entry);
    void sendMemAccToCaches(unsigned count);

    void mruInsert(MRUList &li, const ExecTraceEntry &e, size_t limit, unsigned set_id = 0);
    void mruMemAccess(MRUList &li, unsigned num);

    void receiveTraces();

    bool nemuEos{false}; // NEMU End of Stream
    bool finishedMem{false}; // memory acc finished
};


#endif //__GEM5_NEMUCPU_HH__
