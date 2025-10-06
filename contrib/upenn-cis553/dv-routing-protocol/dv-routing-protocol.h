
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef DV_ROUTING_H
#define DV_ROUTING_H

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-static-routing.h"
#include "ns3/ipv4.h"
#include "ns3/node.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"
#include "ns3/timer.h"
#include "ns3/traced-callback.h"
#include "ns3/string.h"

#include "ns3/dv-message.h"
#include "ns3/penn-routing-protocol.h"
#include "ns3/ping-request.h"
#include "ns3/neighbor-table.h"
#include "ns3/neighbor-timers.h"

#include <vector>
#include <map>

using namespace ns3;

class DVRoutingProtocol : public PennRoutingProtocol
{
public:
  static TypeId GetTypeId(void);

  DVRoutingProtocol();
  virtual ~DVRoutingProtocol();

  // scenario / REPL command processor
  virtual void ProcessCommand(std::vector<std::string> tokens);

  // node wiring
  virtual void SetMainInterface(uint32_t mainInterface);
  virtual void SetNodeAddressMap(std::map<uint32_t, Ipv4Address> nodeAddressMap);
  virtual void SetAddressNodeMap(std::map<Ipv4Address, uint32_t> addressNodeMap);

  // control-plane receive
  void RecvDVMessage(Ptr<Socket> socket);
  void ProcessPingReq(DVMessage msg);
  void ProcessPingRsp(DVMessage msg);

  // maintenance
  void AuditPings();
  void AuditHellos();
  void RunMaintenance();

  // DV update sending
  void PushDvUpdate();
  void ScheduleTriggeredPush();

  // ns-3 Ipv4RoutingProtocol
  virtual void PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit = Time::S) const;
  virtual Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr);
  virtual bool RouteInput(Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                          UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                          LocalDeliverCallback lcb, ErrorCallback ecb);
  virtual void NotifyInterfaceUp(uint32_t interface);
  virtual void NotifyInterfaceDown(uint32_t interface);
  virtual void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address);
  virtual void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address);
  virtual void SetIpv4(Ptr<Ipv4> ipv4);

  void DoDispose();

private:
  // send helper
  void SendOnAllInterfaces(Ptr<Packet> packet);

  // address/label helpers
  virtual Ipv4Address ResolveNodeIpAddress(uint32_t nodeNumber);
  virtual std::string ReverseLookup(Ipv4Address ipv4Address);

  // diag
  void DumpNeighbors();
  void DumpRoutingTable();

  // HELLO (LS-like) handlers
  void ProcessHelloReq(DVMessage msg);
  void ProcessHelloRsp(DVMessage msg, Ipv4Address localInterface);

  // DV logic (MS2-like)
  void CheckNeighborLoss();
  uint32_t UpdateRoute(Ipv4Address dest, Ipv4Address via, Ipv4Address viaIf, uint32_t viaCost);
  void ProcessDvUpdate(DVMessage msg, Ipv4Address localInterface);

  // snapshot for printing
  struct DvRouteRow {
    Ipv4Address dest;
    Ipv4Address nextHop;
    Ipv4Address oif;
    uint32_t    cost;
    Time        timestamp;
  };
  std::vector<DvRouteRow> Snapshot() const;

protected:
  virtual void DoInitialize(void);
  uint32_t GetNextSequenceNumber();
  bool IsOwnAddress(Ipv4Address originatorAddress);

private:
  // sockets
  std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_socketAddresses;
  Ptr<Socket> m_recvSocket; //!< RX socket
  Ipv4Address m_mainAddress;

  // routing plumbing
  Ptr<Ipv4StaticRouting> m_staticRouting;
  Ptr<Ipv4>              m_ipv4;

  // config
  Time    m_pingTimeout;
  uint8_t m_maxTTL;
  uint16_t m_dvPort;

  // DV config
  Time  m_periodicEvery{Seconds(2.0)};
  Time  m_burstHold{MilliSeconds(300)};
  Time  m_maintenanceEvery{Seconds(1.0)};
  uint32_t m_dvInfinity{1000};
  uint32_t m_maxHopCost{16};
  std::string m_helloString{"Hello!"};

  uint32_t m_currentSequenceNumber{0};

  std::map<uint32_t, Ipv4Address> m_nodeAddressMap;
  std::map<Ipv4Address, uint32_t> m_addressNodeMap;

  // timers
  Timer m_auditPingsTimer;
  Timer m_periodicAdv;
  Timer m_burstAdv;
  Timer m_maintenance;

  // trackers
  std::map<uint32_t, Ptr<PingRequest>> m_pingTracker;

  // neighbors + timers
  NeighborTable       m_neighbors;
  Ptr<NeighborTimers> m_neighborTimers;

  // DV routing table
  std::map<Ipv4Address, DvRouteRow> m_routes;

  // trace hooks
  TracedCallback<const Ipv4Address&, const DvRouteRow&> m_routeUpdatedCb;
  TracedCallback<const Ipv4Address&>                    m_routeRemovedCb;
  TracedCallback<const Ipv4Address&>                    m_neighborUpCb;
  TracedCallback<const Ipv4Address&>                    m_neighborDownCb;
};

#endif
