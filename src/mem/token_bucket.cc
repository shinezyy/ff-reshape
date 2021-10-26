#include <cstdlib>
#include <cstring>

#include "sim/eventq.hh"
#include "token_bucket.hh"

using namespace std;

Token_Bucket::Token_Bucket(EventManager *_em, int _size, int _freq, int _inc, bool _bypass=true,
    cross_queue_t* cross_queuePtr=nullptr, Cache *parent_cache=nullptr)
    : size(_size), freq(_freq), inc(_inc), bypass(_bypass), tokens(_inc),
      em(_em),
      updateTokenEvent([this]{ update_tokens(); }, "update_token"),
      cross_queuePtr(cross_queuePtr), parent_cache(parent_cache)
{
    assert(size >= inc && "inc should not be greater than size");
    em->schedule(updateTokenEvent, curTick()+freq);
}

// when cycle == freq, add tokens
void Token_Bucket::update_tokens(){
    if (!bypass){
        tokens = std::min(size, tokens + inc);
    }
    if (tokens > 0) {
        OrderedReq* req = dequeue_request();
        if (req){
            //fprintf(stderr,"enqueue cus new token\n");
            cross_queuePtr->push(req->pkt);
        }
        if (!cross_queuePtr->empty()){
            PacketPtr cross_pkt = cross_queuePtr->front();
            parent_cache->sendOrderedReq(cross_pkt);
            cross_queuePtr->pop();
        }
    }
    em->reschedule(updateTokenEvent, curTick()+freq, true);
}

// fetch one req from waiting queue
OrderedReq* Token_Bucket::dequeue_request() {
    if (waiting_queue.empty()){
        return NULL;
    }
    if (bypass || inc == size || tokens > 0){
        OrderedReq* req = waiting_queue.front();
        tokens --;
        waiting_queue.pop();
        return req;
    }
    return NULL;
}

// add one req to waiting queue
void Token_Bucket::enqueue_request(OrderedReq *request, bool head=false) {
    waiting_queue.push(request);
    if (head) {   // head -> order?
        int num = waiting_queue.size() - 1;
        while (num > 0) {
            auto req = waiting_queue.front();
            waiting_queue.pop();
            waiting_queue.push(req);
            num--;
        }
    }
}

// get the first req in waiting queue but not dequeue
OrderedReq* Token_Bucket::get_waitq_front() {
    if (waiting_queue.empty()){
        return NULL;
    }else
    {
        OrderedReq* req = waiting_queue.front();
        return req;
    }
}

// if no tokens, return false; if there are, get one token out; used when a req arrives memobj
bool Token_Bucket::test_and_get(){
    if (bypass || inc == size || tokens > 0){
        tokens --;
        return true;
    }
    return false;
}
