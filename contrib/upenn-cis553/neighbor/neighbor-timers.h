/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef UPENN_CIS553_NEIGHBOR_TIMERS_H
#define UPENN_CIS553_NEIGHBOR_TIMERS_H

#include "ns3/object.h"
#include "ns3/callback.h"
#include "ns3/nstime.h"
#include "ns3/timer.h"

namespace ns3 {

/**
 * Small helper that fires two periodic timers:
 *  - HELLO sender (HelloTick)
 *  - neighbor audit (AuditTick)
 */
class NeighborTimers : public Object
{
public:
  static TypeId GetTypeId (void);

  NeighborTimers();
  virtual ~NeighborTimers();

  /** Configure periodic intervals. */
  void Configure(Time helloInterval, Time auditInterval);

  /** Set callbacks (required before Start). */
  void SetHelloCallback(Callback<void> cb);
  void SetAuditCallback(Callback<void> cb);

  /** Start/stop periodic timers. */
  void Start();
  void Stop();

private:
  void HelloTick();
  void AuditTick();

private:
  Timer m_helloTimer;
  Timer m_auditTimer;
  Time  m_helloInterval { Seconds(1.0) };
  Time  m_auditInterval { Seconds(1.0) };
  Callback<void> m_helloCb;
  Callback<void> m_auditCb;
};
}
#endif
