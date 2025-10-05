/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * DV routing protocol header
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 */

#ifndef DV_ROUTING_PROTOCOL_H
#define DV_ROUTING_PROTOCOL_H

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-static-routing.h"
#include "ns3/ipv4.h"
#include "ns3/node.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"
#include "ns3/timer.h"

#include "ns3/dv-message.h"
#include "ns3/penn-routing-protocol.h"
#include "ns3/ping-request.h"

#include <vector>
#include <map>

#include "ns3/neighbor-table.h"
#include "ns3/neighbor-timers.h"

namespace ns3 {

/********** Routing Table Entry **********/
struct RoutingTableEntry
{
  Ipv4Address dest;
  Ipv4Address nextHop;
  Ipv4Address interface;
  uint32_t    cost;
  Time        timestamp;
};

class DVRoutingProtocol : public PennRoutingProtocol
{
public:
  static TypeId GetTypeId (void);

  DVRoutingProtocol();
  virtual ~DVRoutingProtocol();

  /********** Wiring & lifecycle **********/
  virtual void SetIpv4(Ptr<Ipv4> ipv4);
  virtual void DoDispose();
  virtual void DoInitialize(void);
  virtual void Start () { /* kept for compatibility if simulator calls it */ }

  /********** Scenario command handling **********/
  virtual void ProcessCommand(std::vector<std::string> tokens);

  /********** Ipv4RoutingProtocol (pass-through to static routing where needed) **********/
  virtual void PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit = Time::S) const;
  virtual Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr);
  virtual bool RouteInput(Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                          UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                          LocalDeliverCallback lcb, ErrorCallback ecb);
  virtual void NotifyInterfaceUp(uint32_t interface);
  virtual void NotifyInterfaceDown(uint32_t interface);
  virtual void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address);
  virtual void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address);

  /********** Address mapping helpers **********/
  virtual void SetMainInterface(uint32_t mainInterface);
  virtual void SetNodeAddressMap(std::map<uint32_t, Ipv4Address> nodeAddressMap);
  virtual void SetAddressNodeMap(std::map<Ipv4Address, uint32_t> addressNodeMap);

  /********** DV control-plane **********/
  void RecvDVMessage(Ptr<Socket> socket);
  void SendPeriodicUpdate();
  void TriggerUpdateSoon();

  /********** MS2: Routing table / DV updates **********/
  uint32_t UpdateRoute(Ipv4Address dest, Ipv4Address source, Ipv4Address sourceInterface, uint32_t sourceCost);
  void     ProcessDvUpdate(DVMessage dvMessage, Ipv4Address sourceInterface);
  void     CheckNeighborLoss();
  std::vector<RoutingTableEntry> Snapshot() const;

  /********** Status dumps (autograder calls) **********/
  void DumpNeighbors();
  void DumpRoutingTable();

  /********** Ping/Hello (already present in project skeletons) **********/
  void ProcessPingReq(DVMessage msg);
  void ProcessPingRsp(DVMessage msg);
  void ProcessHelloReq(DVMessage msg);
  void ProcessHelloRsp(DVMessage msg, Ipv4Address localIf);

  /********** Misc helpers **********/
  uint32_t    GetNextSequenceNumber();
  Ipv4Address ResolveNodeIpAddress(uint32_t nodeNumber);
  std::string ReverseLookup(Ipv4Address ip);
  bool        IsOwnAddress(Ipv4Address originatorAddress);
  void        AuditPings();
  void        AuditHellos();
  void        BroadcastPacket(Ptr<Packet> packet);

private:
  /********** Sockets / addressing **********/
  std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_socketAddresses;
  Ptr<Socket>            m_recvSocket {0};
  Ipv4Address            m_mainAddress;
  Ptr<Ipv4StaticRouting> m_staticRouting;
  Ptr<Ipv4>              m_ipv4;

  /********** Timers and config **********/
  Time     m_pingTimeout { MilliSeconds(2000) };

  // *** Keep the following three in this order; .cc initializer list must match ***
  uint8_t   m_maxTTL;
  uint16_t  m_dvPort;
  uint32_t  m_currentSequenceNumber;

  /********** Node maps **********/
  std::map<uint32_t, Ipv4Address>  m_nodeAddressMap;
  std::map<Ipv4Address, uint32_t>  m_addressNodeMap;

  /********** Timers **********/
  Timer m_auditPingsTimer;
  Timer m_periodicUpdateTimer;
  Timer m_triggeredUpdateTimer;
  Time  m_periodicInterval { Seconds(2.0) };
  Time  m_triggerHold      { MilliSeconds(300) };

  /********** Pings **********/
  std::map<uint32_t, Ptr<PingRequest>> m_pingTracker;

  /********** Neighbors **********/
  NeighborTable       m_neighbors;
  Ptr<NeighborTimers> m_neighborTimers;

  /********** Routing table **********/
  std::map<Ipv4Address, RoutingTableEntry> m_routingTable;
};

} // namespace ns3

#endif /* DV_ROUTING_PROTOCOL_H */
