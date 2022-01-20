#ifndef __TOKEN_BUCKET_H_
#define __TOKEN_BUCKET_H_

#include <cstdint>
#include <queue>

#include "mem/packet.hh"

//event? schedule?
#include "mem/cache/cache.hh"
#include "sim/eventq.hh"
//#include "sim/clocked_object.hh"

class OrderedReq {
public:
    PacketPtr pkt;
    /// timestamp
    uint64_t time_arrive = 0;

    OrderedReq(PacketPtr pkt, uint64_t time_arrive) :
      pkt(pkt), time_arrive(time_arrive)
    { };
};

class Token_Bucket
{
  private:
    /* param */
    int size, freq, inc;    // max_size of the bucket, freq of adding tokens, num of tokens added every cycle
    bool bypass;            // bypass true: do not use token bucket
    int tokens;             // current num of tokens in the bucket

    std::queue<OrderedReq *> waiting_queue;  // reqs not yet sent to mem_ctrl

    EventManager *em;                        // The manager which is used for the event queue
    void update_tokens();                    // Used to schedule updating tokens when curTick%freq==0
    EventFunctionWrapper updateTokenEvent;   // Event used to call update_tokens

    cross_queue_t* cross_queuePtr;           // point to the cross_queue in cache
    Cache *parent_cache;                     // point to cache it belongs to

  public:
    Token_Bucket(EventManager *_em, int size, int freq, int inc, bool bypass,\
    cross_queue_t *cross_queuePtr, Cache *parent_cache);

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
