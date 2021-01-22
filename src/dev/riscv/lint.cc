//
// Created by zyy on 2020/11/24.
//

#include "lint.hh"

#include "mem/packet_access.hh"

Tick Lint::read(PacketPtr pkt) {
  assert(pkt->getAddr() >= pioAddr && pkt->getAddr() < pioAddr + pioSize);
  assert(pkt->getSize() == 8);

  timeStamp += 800;
  pkt->setLE(timeStamp);
  pkt->makeAtomicResponse();
  return pioDelay;
}

Tick Lint::write(PacketPtr pkt) {
  assert(pkt->getAddr() >= pioAddr && pkt->getAddr() < pioAddr + pioSize);
  assert(pkt->getSize() == 8);

  timeStamp = pkt->getRaw<uint64_t>();

  pkt->makeAtomicResponse();
  return pioDelay;
}

Lint::Lint(const LintParams *p) :
    BasicPioDevice(*p, p->pio_size)
{

}

Lint *
LintParams::create() const
{
  return new Lint(this);
}

