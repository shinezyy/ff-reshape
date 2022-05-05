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
    l3_waymask_set(p->l3_waymask_set),
    qos_started(false),
    l3_hot_thereshold(p->l3_hot_thereshold),
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
  // init qos id = context id
  for (size_t i = 0; i < np; i++)
  {
    setContextQosId(i,i);
  }
  // init alter_id = i + LvNATasks::NumId
  // to bypass token buckets
  for (size_t i = 0; i < LvNATasks::NumId; i++)
  {
    uint32_t alt_id = i + LvNATasks::NumId;
    QosIDAlterMap[i] = alt_id;
    for (const auto &c:l2s)
    {
      c->QosIDAlterMap[i] = alt_id;
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
  if (qos_started && !est_top_dir_path.empty()){
    l3->tags->updateHotPolicy();
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

#define SMALLJOBS
// #define INPUT_INC
#define CP_SUM(v) (std::accumulate(std::begin(v), std::end(v), 0.0))
#define CP_MAX(v) (*(std::max_elemet(std::begin(v), std::end(v)))
#define CP_MIN(v) (*(std::min_elemet(std::begin(v), std::end(v)))
#define CP_AVERAGE(v) (std::accumulate(std::begin(v), std::end(v), 0.0) / v.size() )
#define l2b(i,id) l2s[i]->buckets[id]
#define l3b(id) l3->buckets[id]

void
ControlPlane::setInc(int newl2inc, int newl3inc)
{
  // if input is -1, then inc remain unchanged
  l2inc = (newl2inc == -1)? l2inc : newl2inc;
  l3inc = (newl3inc == -1)? l3inc : newl3inc;
}

void
ControlPlane::startTraining()
{
  //mark auto tuning start
  for (int id = 0; id < LvNATasks::NumBuckets; id++)
  {
    for (int i = 0; i < np/2; i++)
    {
      l2b(i, id)->gar_acc();
    }
    l3b(id)->gar_acc();
  }
}

void
ControlPlane::startQoS()
{
  //reset stats
  inform("start real QoS\n");
  inform("init inc: l2 %d; l3 %d;\n", l2inc, l3inc);
  uint64_t sumJobCycles = 0;
  uint64_t sumJobInsts = 0;
  for (int i = 0; i < 5; i++){
    sumJobCycles += cpStat.JobCycles[i].value();
    sumJobInsts  += cpStat.JobInsts[i].value();
  }
  basicJobIpcTotal = double(sumJobInsts)/double(sumJobCycles);
  cpStat.JobIpc.result(basicJobIpcs);

  // restrict lower prior tb
  // use INPUT_INC for the initial inc
  for (int id = 0; id < LvNATasks::QosIdStart; id++){
    l3b(id)->set_bypass(false);
    l3b(id)->set_inc(l3inc);
    l3b(id)->set_tokens(l3inc);
  }
  // Qos guarantee tb
  for (int id = LvNATasks::QosIdStart; id < LvNATasks::NumId; id++){
    l3b(id)->set_bypass(true);
  }
  // alternative bypass tb
  for (int id = LvNATasks::NumId; id < LvNATasks::NumBuckets; id++){
    l3b(id)->set_bypass(true);
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

  if (!est_top_dir_path.empty())
  {
    //dump datas to l3 tags
    for (size_t i = 0; i < LvNATasks::NumId; i++)
    {
      std::ostringstream os;
      os << est_top_dir_path << "/" << i << "/0.csv";
      std::string csv_path = os.str();
      std::ifstream inputF(csv_path);
      std::istream_iterator<int> iit(inputF),iend;
      assert(iit!=iend);
      l3->tags->id_map_set_access_vecs[i].assign(iit,iend);
    }
    //tell l3 tags updatehot
    l3->tags->hot_thereshold = l3_hot_thereshold;
    l3->tags->updateHotSets();
  }
  qos_started = true;
}

void
ControlPlane::tuning()
{
  // get QoS speedup
  // compared with the no-ctrl first 100W cycle
  uint64_t sumJobCycles = 0;
  uint64_t sumJobInsts = 0;
  uint64_t sumBgCycles = 0;
  uint64_t sumBgInsts = 0;
  for (int i = 0; i < 5; i++){
    sumJobCycles += cpStat.JobCycles[i].value();
    sumJobInsts  += cpStat.JobInsts[i].value();
  }
  for (int i = 0; i < np; i++){
    sumBgCycles += cpStat.CPUBackgroundCycles[i].value();
    sumBgInsts  += cpStat.CPUBackgroundInsts[i].value();
  }
  double totalJobIpc = double(sumJobInsts)/double(sumJobCycles);
  double totalBgIpc = double(sumBgInsts)/double(sumBgCycles);
  inform("jobIpcTotal:%.6f, bgIpcTotal:%.6f\n", totalJobIpc, totalBgIpc);

  double speedup_total = totalJobIpc/basicJobIpcTotal;
  inform("speedup total:%.4f\n", speedup_total);

  // get mem-access info for every job
  // count bucket[id] and its bypass bucket[id+LvNATasks::NumBuckets]
  std::vector<int> l2acc, l3acc;
  for (int id = 0; id < LvNATasks::NumId; id++){
    l2acc.push_back(0.0);
    l3acc.push_back(0.0);
    for (int i = 0; i < np/2; i++){
      l2acc[id] += l2b(i, id)->gar_acc();
      l2acc[id] += l2b(i, id+LvNATasks::NumId)->gar_acc();
    }
    l3acc[id] += l3b(id)->gar_acc();
    l3acc[id] += l3b(id+LvNATasks::NumId)->gar_acc();
    inform("job id %d l2acc %d l3acc %d\n",id,l2acc[id],l3acc[id]);
  }

  // update tokens
  // THIS PART IS DONE IN lvna_util/ext_ctrl.py
  inform("old inc %d\n", l3inc);
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
  //TODO: now only update hot in startQos
  // l3->tags->updateHotSets();
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
