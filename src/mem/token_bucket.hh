#ifndef __TOKEN_BUCKET_H_
#define __TOKEN_BUCKET_H_

#include "mem/packet.hh"

//event? schedule?
#include "sim/eventq.hh"

class OrderedReq {
public:
    PacketPtr pkt;
    /// timestamp
    uint64_t time_arrive = 0;

    OrderedReq(PacketPtr pkt, uint64_t time_arrive) :
      pkt(pkt), time_arrive(time_arrive)
    { };
};

typedef std::queue<PacketPtr> cross_queue_t;

class Token_Bucket
{
  private:
    /* param */
    int size, freq, inc;    // max_size of the bucket, freq of adding tokens, num of tokens added every cycle
    bool bypass;            // bypass true: do not use token bucket
    int tokens;             // current num of tokens in the bucket

    std::queue<OrderedReq *> waiting_queue;  // reqs not yet sent to mem_ctrl

    void update_tokens();   // update tokens when curTick%freq==0
    EventFunctionWrapper updateTokenEvent;   // event be scheduled every freq-cycles

    cross_queue_t* cross_queuePtr;           // point to the cross_queue in cache

  public:
    Token_Bucket(int size, int freq, int inc, bool bypass, cross_queue_t *cross_queuePtr);

    inline int get_size() { return size; }
    inline void set_size(int s) { size = s; }

    inline int get_freq() { return freq; }
    inline void set_freq(int f) { freq = f; }

    inline int get_inc() { return inc; }
    inline void set_inc(int i) { inc = std::min(std::max(1, i), size); }

    inline bool get_bypass() { return bypass; }
    inline void set_bypass(bool b) { bypass = b; }

    inline int get_tokens() { return (bypass) ? 1 : tokens; }

    OrderedReq* dequeue_request();
    void enqueue_request(OrderedReq *request, bool head);
    OrderedReq* get_waitq_front();

    bool test_and_get();
};

#endif
