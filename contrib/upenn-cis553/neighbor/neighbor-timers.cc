/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "neighbor-timers.h"
#include "ns3/type-id.h"

namespace ns3 {

  /**
   * Registers this class with ns-3 TypeId system
   * Needed so that ns-3 can create and configure NeighborTimers objects
   */
TypeId
NeighborTimers::GetTypeId (void)
{
  static TypeId tid = TypeId ("NeighborTimers")
                        .SetParent<Object> ();
  return tid;
}

/**
 * Constructor
 * Initializes two periodic timers:
 *  - m_helloTimer: triggers sending HELLO messages
 *  - m_auditTimer: triggers neighbor table cleanup
 * Both timers are set to cancel automatically if this object is destroyed
 */
NeighborTimers::NeighborTimers()
  : m_helloTimer(Timer::CANCEL_ON_DESTROY),
    m_auditTimer(Timer::CANCEL_ON_DESTROY)
{
}
/**
 * Ensures all timers are stopped cleanly
 */
NeighborTimers::~NeighborTimers()
{
  Stop();
}

/**
 * Configure the periodic intervals for HELLO and Audit timers
 * @param helloInterval Interval between HELLO messages
 * @param auditInterval Interval between Audit executions
 */
void NeighborTimers::Configure(Time helloInterval, Time auditInterval)
{
  m_helloInterval = helloInterval;
  m_auditInterval = auditInterval;
}

/**
 * Set the callback to be fired when the HELLO timer ticks
 * @param cb A void() callback provided by routing protocol
 */
void NeighborTimers::SetHelloCallback(Callback<void> cb)
{
  m_helloCb = cb;
}

/**
 * Set the callback to be fired when the Audit timer ticks
 * @param cb A void() callback provided by routing protocol
 */
void NeighborTimers::SetAuditCallback(Callback<void> cb)
{
  m_auditCb = cb;
}

/**
 * Start both periodic timers
 * This function binds HelloTick() and AuditTick() to the timers
 * and schedules the first executions after their respective intervals
 */
void NeighborTimers::Start()
{
  // Bind timer functions
  m_helloTimer.SetFunction(&NeighborTimers::HelloTick, this);
  m_auditTimer.SetFunction(&NeighborTimers::AuditTick, this);
  // Kick off both loops
  m_helloTimer.Schedule(m_helloInterval);
  m_auditTimer.Schedule(m_auditInterval);
}
/**
 * Stop both timers immediately
 * Cancels any future scheduled HELLO or Audit ticks
 */
void NeighborTimers::Stop()
{
  m_helloTimer.Cancel();
  m_auditTimer.Cancel();
}

/**
 * Internal handler for HELLO timer
 * Invokes the registered callback (e.g., SendHelloAllInterfaces)
 * and reschedules itself for the next HELLO interval
 */
void NeighborTimers::HelloTick()
{
  if (!m_helloCb.IsNull()) m_helloCb();
  m_helloTimer.Schedule(m_helloInterval);
}

/**
 * Internal handler for Audit timer
 * Invokes the registered callback (NeighborTable::Audit)
 * and reschedules itself for the next Audit interval.
 */
void NeighborTimers::AuditTick()
{
  if (!m_auditCb.IsNull()) m_auditCb();
  m_auditTimer.Schedule(m_auditInterval);
}
}
