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
  cp->registerRunningHighId(LvNATasks::job2QosId(job_id),true);
  up(now_cycle,now_insts);
}
void
FgJobMeta::jobDown(int job_id, int cpu_id,uint64_t now_cycle, uint64_t now_insts)
{
  down(now_cycle,now_insts);
  cp->registerRunningHighId(LvNATasks::job2QosId(job_id),false);
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
    l2_waymask_set(p->l2_waymask_set),
    l3_waymask_set(p->l3_waymask_set),
    _system(p->system),
    cpus(p->cpus),
    l2s(p->l2s),
    l3(p->l3),
    np(cpus.size()),
    l2inc(p->l2inc),
    l3inc(p->l3inc),
    l2_tb_size(p->l2_tb_size),
    l3_tb_size(p->l3_tb_size),
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
  // for (size_t i = 0; i < np; i++)
  // {
  //   setContextQosId(i,15);
  // }
  const auto max_requestors = getSystem()->maxRequestors();
  for (size_t i = 0; i < max_requestors; i++)
  {
    setContextQosId(i,15);
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

void
ControlPlane::registerRunningHighId(uint32_t qos_id, bool flag)
{
  if (flag)
  {
    for (const auto &c:l2s)
    {
      c->runningHighIds.insert(qos_id);
    }
    l3->runningHighIds.insert(qos_id);
  }
  else
  {
    for (const auto &c:l2s)
    {
      c->runningHighIds.erase(qos_id);
    }
    l3->runningHighIds.erase(qos_id);
  }
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
}

void
ControlPlane::startQoS()
{
  //reset stats
  inform("start real QoS\n");
  if (np == 1){
    auto iid = getSystem()->lookupRequestorId(".cpu.inst");
    fatal_if(iid == Request::invldRequestorId,"can't find cpu inst id");
    setContextQosId(iid,0);
    auto did = getSystem()->lookupRequestorId(".cpu.data");
    fatal_if(did == Request::invldRequestorId,"can't find cpu data id");
    setContextQosId(did,1);
    auto dpid = getSystem()->lookupRequestorId(".cpu.dcache.prefetcher");
    // fatal_if(dpid == Request::invldRequestorId,"can't find cpu data hwp id");
    setContextQosId(dpid,1);
  }
  else{
    for (size_t i = 0; i < np; i++)
    {
      auto iid = getSystem()->lookupRequestorId(".cpu"+std::to_string(i)+".inst");
      fatal_if(iid == Request::invldRequestorId,"can't find cpu inst id");
      setContextQosId(iid,2*i);
      auto did = getSystem()->lookupRequestorId(".cpu"+std::to_string(i)+".data");
      fatal_if(did == Request::invldRequestorId,"can't find cpu data id");
      setContextQosId(did,2*i+1);
      auto dpid = getSystem()->lookupRequestorId(".cpu"+std::to_string(i)+".dcache.prefetcher");
      // fatal_if(dpid == Request::invldRequestorId,"can't find cpu data hwp id");
      setContextQosId(dpid,2*i+1);
    }
  }


  std::vector<double> bgIpcs;
  cpStat.CPUBackgroundIpc.result(bgIpcs);
  mixIpc = bgIpcs[0];

  //set l2 waymasks
  if (!l2_waymask_set.empty())
  {
    for (const auto &c:l2s)
    {
      c->setWaymaskEnable(true);
      for (size_t i = 0; i < l2_waymask_set.size(); i++)
      {
        c->setWaymask(i,l2_waymask_set[i]);
      }
    }
  }
  //set l3 waymasks
  if (!l3_waymask_set.empty())
  {
    l3->setWaymaskEnable(true);
    for (size_t i = 0; i < l3_waymask_set.size(); i++)
    {
      l3->setWaymask(i,l3_waymask_set[i]);
    }
  }
}

void
ControlPlane::tuning()
{
  inform("start tuning\n");
  std::vector<double> bgIpcs;
  cpStat.CPUBackgroundIpc.result(bgIpcs);
  double estimatedIpc = mixIpc * 1.10;
  double diff = (estimatedIpc - bgIpcs[0])/mixIpc;

  for (int i = 0; i < LvNATasks::QosIdStart; i++)
  {
    int l3accesses = l3->buckets[i]->get_accesses();
    l3->buckets[i]->reset_accesses();
    int l3inc = l3->buckets[i]->get_inc();
    // far from target
    if (diff > 0.08)
    {
      l3->buckets[i]->set_inc(l3accesses/2);
    }// still needs adjustment
    else if (diff > 0.02)
    {
      l3->buckets[i]->set_inc(l3inc-50);
    }// relax a little
    else if (diff < -0.02)
    {
      l3->buckets[i]->set_inc(l3inc+50);
    }
  }
}

void
ControlPlane::startTTI()
{
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
  //tell l3 tags updatehot
  l3->tags->updateHotSets();
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
