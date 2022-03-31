//
// Created by zcq on 2022/2/11.
//

#include "controlplane.hh"
#include "mem/packet_access.hh"

FgJobMeta::FgJobMeta(ControlPlane *cp):
  JobMeta(cp)
{
}
void
FgJobMeta::jobUp(int job_id, int cpu_id,uint64_t now_cycle, uint64_t now_insts)
{
  cp->setContextQosId(cpu_id, LvNATasks::job2QosId(job_id));
  up(now_cycle,now_insts);
}
void
FgJobMeta::jobDown(int job_id, int cpu_id,uint64_t now_cycle, uint64_t now_insts)
{
  down(now_cycle,now_insts);
  cp->cpStat.JobCycles[job_id] += total_cycle;
  cp->cpStat.JobInsts[job_id] += total_insts;
}

BgCpuMeta::BgCpuMeta(ControlPlane *cp):
  JobMeta(cp)
{
}
void
BgCpuMeta::bgUp(int cpu_id,uint64_t now_cycle, uint64_t now_insts)
{
  cp->setContextQosId(cpu_id, cpu_id);
  up(now_cycle,now_insts);
}
void
BgCpuMeta::bgDown(int cpu_id,uint64_t now_cycle, uint64_t now_insts)
{
  down(now_cycle,now_insts);
  cp->cpStat.CPUBackgroundCycles[cpu_id] += total_cycle;
  cp->cpStat.CPUBackgroundInsts[cpu_id] += total_insts;
}

Tick ControlPlane::read(PacketPtr pkt) {
  pkt->setLE(0);
  pkt->makeAtomicResponse();
  return pioDelay;
}

Tick ControlPlane::write(PacketPtr pkt) {
  pkt->makeAtomicResponse();
  return pioDelay;
}

ControlPlane::ControlPlane(const ControlPlaneParams *p) :
    BasicPioDevice(*p, p->pio_size),
    cpus(p->cpus),
    l2s(p->l2s),
    l3(p->l3),
    np(cpus.size()),
    l2inc(p->l2inc),
    l3inc(p->l3inc),
    cpStat(*this)
{
  for (size_t i = 0; i < LvNATasks::NumJobs; i++)
  {
    FgJobMap[i] = new FgJobMeta(this);
  }
  for (size_t i = 0; i < np; i++)
  {
    BgCpuMap[i] = new BgCpuMeta(this);
  }
  for (size_t i = 0; i < np; i++)
  {
    setContextQosId(i,i);
  }

  for (size_t i = 0; i < LvNATasks::NumId; i++)
  {
    uint32_t alt_id = i + LvNATasks::NumId;
    QosIDAlterMap[i] = alt_id;
    for (const auto &c:l2s)
    {
      c->QosIDAlterMap[i] = i + alt_id;
    }
    l3->QosIDAlterMap[i] = alt_id;
  }

  resetTTIMeta();

}

ControlPlane *
ControlPlaneParams::create() const
{
  return new ControlPlane(this);
}

void
ControlPlane::setContextQosId(uint32_t ctx_id, uint32_t qos_id)
{
  context2QosIDMap[ctx_id]=qos_id;
  for (const auto &c:l2s)
  {
    c->context2QosIDMap[ctx_id] = qos_id;
  }
  l3->context2QosIDMap[ctx_id] = qos_id;
}

void ControlPlane::resetTTIMeta()
{
  JobIpc.assign(LvNATasks::NumJobs,0.0);
  CPUBackgroundIpc.assign(np,0.0);
  for (size_t i = 0; i < LvNATasks::NumJobs; i++)
  {
    FgJobMap[i]->resetMeta();
  }
  for (size_t i = 0; i < np; i++)
  {
    BgCpuMap[i]->resetMeta();
  }
}

void
ControlPlane::startTraining()
{
  //mark auto tuning start
  for (int i = 0; i < 4; i++)
  {
    l2s[i/2]->buckets[i]->reset_accesses();
    l3->buckets[i]->reset_accesses();
  }
}

// #define INPUT_INC
void
ControlPlane::startQoS()
{
  //reset stats
  inform("start real QoS\n");
  std::vector<double> bgIpcs;
  cpStat.CPUBackgroundIpc.result(bgIpcs);
  mixIpc = bgIpcs[0];

#ifdef INPUT_INC
  for (int i = 0; i < LvNATasks::QosIdStart; i++)
  {
    l3->buckets[i]->set_bypass(false);
    l3->buckets[i]->set_inc(l3inc);
    l3->buckets[i]->set_tokens(l3inc);
    printf("%d tokens %d accesses %d\n",i,l3->buckets[i]->get_tokens(),l3->buckets[i]->get_accesses());
    l3->buckets[i]->reset_accesses();
  }
  printf("l2inc %d l3inc %d\n",l2inc,l3inc);
  l3->buckets[0]->set_bypass(true);
  l2s[0]->buckets[1]->set_bypass(false);
  l2s[0]->buckets[1]->set_inc(l2inc);
  l2s[0]->buckets[1]->set_tokens(l2inc);
  // this->schedule();
#else
  std::vector<int> l2acc, l3acc;

  // get info
  for (int i = 0; i < 4; i++)
  {
    l2acc.push_back(l2s[i/2]->buckets[i]->get_accesses());
    l3acc.push_back(l3->buckets[i]->get_accesses());
    l2s[i/2]->buckets[i]->reset_accesses();
    l3->buckets[i]->reset_accesses();
    printf("core %d l2acc %d l3acc %d\n", i, l2acc[i], l3acc[i]);
  }

  // start QoS
  l2s[0]->buckets[1]->set_bypass(false);
  l2s[0]->buckets[1]->set_inc(20);
  l2s[0]->buckets[1]->set_tokens(20);
  for (int i = 1; i < 4; i++){
    l3->buckets[i]->set_bypass(false);
    l3->buckets[i]->set_inc(50);
    l3->buckets[i]->set_tokens(50);
  }
#endif
}

void
ControlPlane::tuning()
{
#ifndef INPUT_INC
  inform("start tuning\n");
  std::vector<double> bgIpcs;
  cpStat.CPUBackgroundIpc.result(bgIpcs);
  double speedup = bgIpcs[0]/mixIpc;
  std::vector<int> l2acc, l3acc;

  // get info
  for (int i = 0; i < 4; i++)
  {
    l2acc.push_back(l2s[i/2]->buckets[i]->get_accesses());
    l3acc.push_back(l3->buckets[i]->get_accesses());
    l2s[i/2]->buckets[i]->reset_accesses();
    l3->buckets[i]->reset_accesses();

    printf("cpu %d ipc: %.6f l2acc %d l3acc %d \n", i, bgIpcs[i], l2acc[i], l3acc[i]);
  }
  printf("cpu 0 speedup %.4f\n", speedup);

  // start QoS
  for (int i = 1; i < 4; i++)
  {
    int l3inc = l3->buckets[i]->get_inc();
    // far from target
    if (speedup < 1.05)
    {
      l3->buckets[i]->set_inc(l3inc/2);
    }// still needs adjustment
    else if (speedup < 1.095)
    { //先按照百分比减少吧
      l3->buckets[i]->set_inc(l3inc-(1.1 - speedup)*2*(l2acc[1]+l2acc[2]+l2acc[3])/100/3);
    }// relax a little
    else if (speedup > 1.130)
    {
      // l3->buckets[i]->set_inc(l3inc+(speedup-1.1)*2*(l2acc[1]+l2acc[2]+l2acc[3])/100/3);
      l3->buckets[i]->set_inc(l3inc+10);
    }
    else if (speedup > 1.110)
    {
      l3->buckets[i]->set_inc(l3inc+5);
    }
    printf("cpu %d new inc %d\n", i, l3->buckets[i]->get_inc());
  }
#endif
}

void
ControlPlane::startTTI()
{
  inform("===============================================\n");
  inform("start a TTI\n");
  resetTTIMeta();
  // schedule auto tuning functions
  // this->schedule();
  //record all cpus as bg status at beginning
  for (size_t i = 0; i < np; i++)
  {
    //bg up
    uint64_t now_cycle = cpus[i]->getNumCycles();
    uint64_t now_insts = cpus[i]->getCommittedInsts();
    BgCpuMap[i]->bgUp(i,now_cycle,now_insts);
  }
}

void
ControlPlane::setJob(int job_id, int cpu_id, bool status)
{
  uint64_t now_cycle = cpus[cpu_id]->getNumCycles();
  uint64_t now_insts = cpus[cpu_id]->getCommittedInsts();
  if (status)
  {
    //job up
    FgJobMap[job_id]->jobUp(job_id,cpu_id,now_cycle,now_insts);
    //bg down
    BgCpuMap[cpu_id]->bgDown(cpu_id,now_cycle,now_insts);
  }
  else
  {
    //job down
    FgJobMap[job_id]->jobDown(job_id,cpu_id,now_cycle,now_insts);
    //bg up
    BgCpuMap[cpu_id]->bgUp(cpu_id,now_cycle,now_insts);
  }
}

void
ControlPlane::endTTI()
{
  inform("A TTI ends\n");
  //mark a TTI end, calculate stats
  //record all cpus in bg status in the end
  for (size_t i = 0; i < np; i++)
  {
    uint64_t now_cycle = cpus[i]->getNumCycles();
    uint64_t now_insts = cpus[i]->getCommittedInsts();
    BgCpuMap[i]->bgDown(i,now_cycle,now_insts);
  }
  //report stats

  // for (size_t i = 0; i < LvNATasks::NumJobs; i++)
  // {
  //   cpStat.JobCycles[i] += JobCycles[i];
  //   cpStat.JobInsts[i] += JobInsts[i];
  // }
  // for (size_t i = 0; i < np; i++)
  // {
  //   cpStat.CPUBackgroundCycles[i] += CPUBackgroundCycles[i];
  //   cpStat.CPUBackgroundInsts[i] += CPUBackgroundInsts[i];
  // }
}

ControlPlane::ControlPlaneStats::ControlPlaneStats(ControlPlane &cp)
  : Stats::Group(&cp),
  cp(cp),
  ADD_STAT(JobCycles, "cycles of a critical job working"),
  ADD_STAT(JobInsts , "Insts of a critical job committed"),
  ADD_STAT(JobIpc   , "IPC of a critical job"),
  ADD_STAT(CPUBackgroundCycles, "cycles of a cpu working in low priority"),
  ADD_STAT(CPUBackgroundInsts , "insts of a cpu working in low priority"),
  ADD_STAT(CPUBackgroundIpc   , "IPC of a cpu working in low priority")
{
  JobCycles
    .init(LvNATasks::NumJobs)
    .flags(Stats::nozero)
    .prereq(JobCycles);
  JobInsts
    .init(LvNATasks::NumJobs)
    .flags(Stats::nozero)
    .prereq(JobInsts);
  JobIpc
    .precision(6);
  JobIpc = JobInsts / JobCycles;

  CPUBackgroundCycles
    .init(cp.np)
    .flags(Stats::nozero)
    .prereq(CPUBackgroundCycles);
  CPUBackgroundInsts
    .init(cp.np)
    .flags(Stats::nozero)
    .prereq(CPUBackgroundInsts);
  CPUBackgroundIpc
    .precision(6);
  CPUBackgroundIpc = CPUBackgroundInsts / CPUBackgroundCycles;
}
void
ControlPlane::ControlPlaneStats::preDumpStats()
{
  Stats::Group::preDumpStats();
}
