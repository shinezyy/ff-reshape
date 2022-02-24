#include <cstdlib>
#include <cstring>

#include "sim/eventq.hh"
#include "token_bucket.hh"

using namespace std;

Token_Bucket::Token_Bucket(EventManager *_em, int _size, int _freq, int _inc, bool _bypass=true,
    Cache *parent_cache=nullptr)
    : size(_size), freq(_freq), inc(_inc), bypass(_bypass), tokens(_inc),
      em(_em),
      updateTokenEvent([this]{ update_tokens(); }, "update_token"),
      parent_cache(parent_cache)
{
    assert(size >= inc && "inc should not be greater than size");
    em->schedule(updateTokenEvent, curTick() + parent_cache->cyclesToTicks(Cycles(freq)));
}

// when cycle == freq, add tokens
void Token_Bucket::update_tokens(){
    tokens = std::min(size, tokens + inc);
    while (!waiting_queue.empty())
    {
        if (test_and_get())
        {
            PacketPtr outPkt = waiting_queue.front();
            parent_cache->handleStalledPkt(outPkt);
            waiting_queue.pop();
        }
        else
            break;
    }
    em->reschedule(updateTokenEvent, curTick()+parent_cache->cyclesToTicks(Cycles(freq)), true);
}

bool Token_Bucket::checkPassPkt(PacketPtr pkt)
{
    if (test_and_get())
    {
        return true;
    }
    else
    {
        enqueue_request(pkt,false);
        return false;
    }
}
// add one req to waiting queue
void Token_Bucket::enqueue_request(PacketPtr request, bool head) {
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

// if no tokens, return false; if there are, get one token out; used when a req arrives memobj
bool Token_Bucket::test_and_get(){
    if (bypass) {
        return true;
    }
    if (tokens > 0 || inc == size){
        tokens --;
        return true;
    }
    return false;
}
