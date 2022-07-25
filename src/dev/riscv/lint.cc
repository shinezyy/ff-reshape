//
// Created by zyy on 2020/11/24.
//

#include "lint.hh"

#include "mem/packet_access.hh"

Tick Lint::read(PacketPtr pkt) {
  assert(pkt->getAddr() >= pioAddr && pkt->getAddr() < pioAddr + pioSize);
  assert(pkt->getSize() == 8);

  Addr offset = pkt->getAddr() - pioAddr;
  uint64_t ret_val = 0;
  switch (offset)
  {
  case CLINT_FREQ:
    ret_val = freq;
    break;
  case CLINT_INC:
    ret_val = inc;
    break;
  case CLINT_MTIME:
    ret_val = mtime;
    break;
  case CLINT_MSIP:
    ret_val = msip;
    break;
  case CLINT_MTIMECMP:
    ret_val = mtimecmp;
    break;
  }
  DPRINTF(Lint,"read Lint offset:%x val:%x\n",offset,ret_val);
  update_mtip();
  pkt->setLE(ret_val);
  pkt->makeAtomicResponse();
  return pioDelay;
}

Tick Lint::write(PacketPtr pkt) {
  assert(pkt->getAddr() >= pioAddr && pkt->getAddr() < pioAddr + pioSize);
  assert(pkt->getSize() == 8);

  Addr offset = pkt->getAddr() - pioAddr;
  uint64_t write_val = pkt->getRaw<uint64_t>();
  DPRINTF(Lint,"write Lint offset:%x val:%x\n",offset,write_val);
  switch (offset)
  {
  case CLINT_FREQ:
    freq = write_val;
    break;
  case CLINT_INC:
    inc = write_val;
    break;
  case CLINT_MTIME:
    mtime = write_val;
    break;
  case CLINT_MSIP:
    msip = write_val;
    break;
  case CLINT_MTIMECMP:
    mtimecmp = write_val;
    break;
  }
  update_mtip();
  pkt->makeAtomicResponse();
  return pioDelay;
}

void Lint::update_mtip(void) {
  if (int_enable && mtime >= mtimecmp){
    DPRINTF(Lint,"post mtip! time:%x cmp:%x\n",mtime,mtimecmp);
    intrctrl->post(lint_id,INT_TIMER_MACHINE,0);
  }
  else if (int_enable && mtime < mtimecmp){
    DPRINTF(Lint,"clear mtip! time:%x cmp:%x\n",mtime,mtimecmp);
    intrctrl->clear(lint_id,INT_TIMER_MACHINE);
  }
}

void Lint::update_time() {
  mtime += 1;
  update_mtip();
  this->reschedule(update_lint_event,curTick()+interval,true);
}

Lint::Lint(const LintParams *p) :
    BasicPioDevice(*p, p->pio_size),
    lint_id(p->lint_id),
    intrctrl(p->intrctrl),
    int_enable(p->int_enable),
    freq(0),inc(0),mtime(0),
    msip(0),mtimecmp(999999),
    update_lint_event([this]{update_time();},"update clint time")
{
  interval = (Tick)(1*SimClock::Float::us);
  this->schedule(update_lint_event,curTick()+interval);
}

Lint *
LintParams::create() const
{
  return new Lint(this);
}

