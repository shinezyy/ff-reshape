#include "token_bucket.h"

Token_Bucket::Token_Bucket(int size, int freq, int inc, bool bypass, cross_queue_t* cross_queuePtr)
    : size(size), freq(freq), inc(inc), bypass(bypass), tokens(inc),
      cross_queuePtr(cross_queuePtr),
      updateTokenEvent([this]{ update_tokens(); }, name())
{
    assert(size >= inc && "inc should not be greater than size");
    schedule(updateTokenEvent, curTick()+freq);
}

// when cycle == freq, add tokens
void Token_Bucket::update_tokens(){
    if (!bypass){
        tokens = std::min(size, tokens + inc);
    }
    OrderedReq* req = dequeue_request();
    if (req)
    {
        cross_queuePtr->push(req->pkt);
        //BaseCache::recvTimingReq(new_pkt);???
    }
    reschedule(updateTokenEvent, curTick()+freq, true);
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
