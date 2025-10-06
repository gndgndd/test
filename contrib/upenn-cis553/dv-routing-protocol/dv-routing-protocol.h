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

#include "ns3/dv-message.h"
#include "ns3/penn-routing-protocol.h"
#include "ns3/ping-request.h"

#include <vector>
#include <map>

#include "ns3/neighbor-table.h"
#include "ns3/neighbor-timers.h"

using namespace ns3;

/** Route row kept by DV */
struct DvRouteRow
{
  Ipv4Address dest;       // destination
  Ipv4Address nextHop;    // selected next hop
  Ipv4Address oif;        // outgoing interface
  uint32_t    cost;       // hop-count metric (or INVALID)
  Time        timestamp;  // last update
};

class DVRoutingProtocol : public PennRoutingProtocol
{
public:
  static TypeId GetTypeId (void);

  DVRoutingProtocol();
  ~DVRoutingProtocol() override;

  // Scenario/CLI
  void ProcessCommand(std::vector<std::string> tokens) override;

  // Node wiring
  void SetMainInterface(uint32_t mainInterface) override;
  void SetNodeAddressMap(std::map<uint32_t, Ipv4Address> nodeAddressMap) override;
  void SetAddressNodeMap(std::map<Ipv4Address, uint32_t> addressNodeMap) override;

  // Control-plane receive path
  void RecvDVMessage(Ptr<Socket> socket);
  void ProcessPingReq(DVMessage msg);
  void ProcessPingRsp(DVMessage msg);

  // Neighbor maintenance (hello audit + ping audit)
  void AuditPings();
  void AuditHellos();

  // DV update logic (Part 2)
  uint32_t UpdateRoute(Ipv4Address dst, Ipv4Address via, Ipv4Address viaIf, uint32_t viaCost);
  void     CheckNeighborLoss();
  void     ProcessDvUpdate(DVMessage msg, Ipv4Address localIf);

  // Snapshot for safe iteration/printing
  std::vector<DvRouteRow> Snapshot() const;

  // Ipv4RoutingProtocol overrides
  void PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit = Time::S) const override;
  Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr) override;
  bool RouteInput(Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                  UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                  LocalDeliverCallback lcb, ErrorCallback ecb) override;

  void NotifyInterfaceUp(uint32_t interface) override;
  void NotifyInterfaceDown(uint32_t interface) override;
  void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
  void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
  void SetIpv4(Ptr<Ipv4> ipv4) override;

  void DoDispose() override;

protected:
  void DoInitialize(void) override;
  uint32_t GetNextSequenceNumber();
  bool IsOwnAddress(Ipv4Address ip) ;

private:
  // I/O helpers
  void BroadcastPacket(Ptr<Packet> packet);
  Ipv4Address ResolveNodeIpAddress(uint32_t nodeNumber) override;
  std::string ReverseLookup(Ipv4Address ipv4Address) override;

  // dumps (grader-friendly)
  void DumpNeighbors();
  void DumpRoutingTable();

  // neighbor hello handlers
  void ProcessHelloReq(DVMessage msg);
  void ProcessHelloRsp(DVMessage msg, Ipv4Address localIf);

  // DV advertisements (periodic & triggered)
  void SendPeriodicUpdate();
  void TriggerUpdateSoon();

private:
  // sockets per-interface and shared RX
  std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_sockIf;
  Ptr<Socket>  m_rxSock{nullptr};

  // basics
  Ipv4Address             m_mainAddress;
  Ptr<Ipv4StaticRouting>  m_staticRouting;
  Ptr<Ipv4>               m_ipv4{nullptr};

  // attributes
  Time      m_pingTimeout;
  uint8_t   m_maxTTL{16};
  uint16_t  m_dvPort{5000};
  uint32_t  m_seq{0};

  // topo id maps
  std::map<uint32_t, Ipv4Address>  m_nodeToAddr;
  std::map<Ipv4Address, uint32_t>  m_addrToNode;

  // timers
  Timer m_auditPingsTimer;
  Timer m_helloDriver;                 // driven by NeighborTimers
  // Part 1/2 DV timers
  Timer m_periodicAdv;                 // periodic DV advertise
  Timer m_burstAdv;                    // coalesced triggered advertise
  Time  m_periodicEvery{Seconds(2.0)};
  Time  m_burstHold{MilliSeconds(300)};

  // trackers
  std::map<uint32_t, Ptr<PingRequest>> m_pingTracker;

  // neighbors + timers
  NeighborTable            m_neighbors;
  Ptr<NeighborTimers>      m_neighborTimers;

  // DV routing table
  std::map<Ipv4Address, DvRouteRow> m_routes;
};

#endif
