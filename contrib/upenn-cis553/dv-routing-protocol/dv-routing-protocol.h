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

#include "ns3/neighbor-table.h"
#include "ns3/neighbor-timers.h"

#include <vector>
#include <map>
#include <set>

using namespace ns3;

// Route table entry mirrors LS style
struct DvRouteRow
{
  Ipv4Address dest;
  Ipv4Address nextHop;
  Ipv4Address oif;
  uint32_t    cost;
  Time        timestamp;
};

class DVRoutingProtocol : public PennRoutingProtocol
{
public:
  static TypeId GetTypeId(void);

  DVRoutingProtocol();
  virtual ~DVRoutingProtocol();

  // CLI / commands
  virtual void ProcessCommand(std::vector<std::string> tokens);

  // Setup
  virtual void SetMainInterface(uint32_t mainInterface);
  virtual void SetNodeAddressMap(std::map<uint32_t, Ipv4Address> nodeAddressMap);
  virtual void SetAddressNodeMap(std::map<Ipv4Address, uint32_t> addressNodeMap);

  // Control-plane RX
  void RecvDVMessage(Ptr<Socket> socket);
  void ProcessPingReq(DVMessage msg);
  void ProcessPingRsp(DVMessage msg);

  // Maintenance
  void AuditPings();
  void AuditHellos(); // LS-style hello driver

  // MS2 routing ops
  uint32_t UpdateRoute(Ipv4Address dest, Ipv4Address via, Ipv4Address viaIf, uint32_t viaCost);
  void     CheckNeighborLoss();
  void     ProcessDvUpdate(DVMessage msg, Ipv4Address localIf);
  std::vector<DvRouteRow> Snapshot() const;

  // Ipv4RoutingProtocol
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

protected:
  virtual void DoInitialize(void);
  uint32_t GetNextSequenceNumber();
  bool IsOwnAddress(Ipv4Address originatorAddress);

private:
  // I/O helpers
  void BroadcastPacket(Ptr<Packet> packet);
  Ipv4Address ResolveNodeIpAddress(uint32_t nodeNumber);
  std::string ReverseLookup(Ipv4Address ipv4Address);

  // Status
  void DumpNeighbors();
  void DumpRoutingTable();

  // Hello helpers
  void ProcessHelloReq(DVMessage msg);
  void ProcessHelloRsp(DVMessage msg, Ipv4Address localIf);

  // DV update helpers
  void SendPeriodicUpdate();
  void TriggerUpdateSoon();

  // ——— State ———
  std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_socketAddresses;
  Ptr<Socket> m_recvSocket {nullptr};
  Ipv4Address m_mainAddress;
  Ptr<Ipv4StaticRouting> m_staticRouting;
  Ptr<Ipv4> m_ipv4 {nullptr};

  // attributes
  Time     m_pingTimeout;
  uint8_t  m_maxTTL {16};
  uint16_t m_dvPort {5000};
  uint32_t m_currentSequenceNumber {0};

  std::map<uint32_t, Ipv4Address> m_nodeAddressMap;
  std::map<Ipv4Address, uint32_t> m_addressNodeMap;

  // timers
  Timer m_auditPingsTimer;
  Timer m_periodicAdv;     // periodic DV_UPDATE
  Timer m_burstAdv;        // triggered (coalesced) DV_UPDATE
  Timer m_helloDriver;     // LS-style hello tick

  // knobs
  Time m_periodicEvery { Seconds(2.0) };
  Time m_burstHold     { MilliSeconds(300) };
  bool m_poisonReverse { true }; // optional LS-like toggle
  double m_jitterPct   { 0.1 };  // +-10% jitter on periodic

  // neighbor infra
  NeighborTable m_neighbors;
  Ptr<NeighborTimers> m_neighborTimers;

  // DV route table
  std::map<Ipv4Address, DvRouteRow> m_routes;

  // ping tracker
  std::map<uint32_t, Ptr<PingRequest>> m_pingTracker;

  // duplicate filter for control-plane (origin, seq) -> seen
  std::set<std::pair<Ipv4Address,uint32_t>> m_seenUpdates;

  // sockets map shorthand used by code that previously referenced m_recvSocket
  std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_sockIf;

  // RX socket for wire port
  Ptr<Socket> m_rxSock {nullptr};

  // seq
  uint32_t m_seq {0};
};

#endif
