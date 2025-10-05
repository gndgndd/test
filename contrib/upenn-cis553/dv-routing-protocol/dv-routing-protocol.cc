/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * DV routing protocol implementation
 */

#include "ns3/dv-routing-protocol.h"

#include "ns3/double.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-packet-info-tag.h"
#include "ns3/log.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/test-result.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

#include "ns3/neighbor-table.h"
#include "ns3/neighbor-timers.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("DVRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(DVRoutingProtocol);

/********** Constants **********/
#define DV_MAX_SEQUENCE_NUMBER 0xFFFF
#define DV_PORT_NUMBER 5000           // keep consistent everywhere
#define ROUTE_NOT_UPDATED 0
#define ROUTE_UPDATED 1
#define INVALIDATED_ROUTE 16          // RIP-style infinity
#define DV_MAX_TTL 16

TypeId
DVRoutingProtocol::GetTypeId(void)
{
  static TypeId tid = TypeId("DVRoutingProtocol")
      .SetParent<PennRoutingProtocol>()
      .AddConstructor<DVRoutingProtocol>()
      .AddAttribute("DVPort",
                    "Listening port for DV packets",
                    UintegerValue(DV_PORT_NUMBER),
                    MakeUintegerAccessor(&DVRoutingProtocol::m_dvPort),
                    MakeUintegerChecker<uint16_t>())
      .AddAttribute("PingTimeout",
                    "Timeout value for PING_REQ in milliseconds",
                    TimeValue(MilliSeconds(2000)),
                    MakeTimeAccessor(&DVRoutingProtocol::m_pingTimeout),
                    MakeTimeChecker())
      .AddAttribute("MaxTTL",
                    "Maximum TTL value for DV packets",
                    UintegerValue(DV_MAX_TTL),
                    MakeUintegerAccessor(&DVRoutingProtocol::m_maxTTL),
                    MakeUintegerChecker<uint8_t>());
  return tid;
}

DVRoutingProtocol::DVRoutingProtocol()
  : m_recvSocket (0),
    m_mainAddress (),
    m_staticRouting (Create<Ipv4StaticRouting>()),
    m_ipv4 (),
    m_pingTimeout (MilliSeconds(2000)),
    // *** match header order: m_maxTTL, m_dvPort, m_currentSequenceNumber
    m_maxTTL (DV_MAX_TTL),
    m_dvPort (DV_PORT_NUMBER),
    m_currentSequenceNumber (0)
{
  m_auditPingsTimer      = Timer(Timer::CANCEL_ON_DESTROY);
  m_periodicUpdateTimer  = Timer(Timer::CANCEL_ON_DESTROY);
  m_triggeredUpdateTimer = Timer(Timer::CANCEL_ON_DESTROY);
}

DVRoutingProtocol::~DVRoutingProtocol() = default;

void DVRoutingProtocol::DoDispose()
{
  if (m_recvSocket) {
    m_recvSocket->Close();
    m_recvSocket = 0;
  }

  for (auto &kv : m_socketAddresses) {
    kv.first->Close();
  }
  m_socketAddresses.clear();

  m_staticRouting = 0;

  m_auditPingsTimer.Cancel();
  m_periodicUpdateTimer.Cancel();
  m_triggeredUpdateTimer.Cancel();
  m_pingTracker.clear();

  PennRoutingProtocol::DoDispose();
}

void DVRoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4)
{
  NS_ASSERT (ipv4 != 0);
  NS_ASSERT (m_ipv4 == 0);
  m_auditPingsTimer.SetFunction(&DVRoutingProtocol::AuditPings, this);
  m_ipv4 = ipv4;
  m_staticRouting->SetIpv4 (m_ipv4);
}

void DVRoutingProtocol::DoInitialize()
{
  // Pick main address if not already set
  if (m_mainAddress == Ipv4Address()) {
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); ++i) {
      Ipv4Address addr = m_ipv4->GetAddress(i, 0).GetLocal();
      if (addr != Ipv4Address::GetLoopback()) { m_mainAddress = addr; break; }
    }
  }
  NS_ASSERT(m_mainAddress != Ipv4Address());

  // Global receive socket
  if (m_recvSocket == 0) {
    m_recvSocket = Socket::CreateSocket(GetObject<Node>(), UdpSocketFactory::GetTypeId());
    m_recvSocket->SetAllowBroadcast(true);
    m_recvSocket->SetRecvCallback(MakeCallback(&DVRoutingProtocol::RecvDVMessage, this));
    if (m_recvSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), DV_PORT_NUMBER))) {
      NS_FATAL_ERROR("Failed to bind DV socket");
    }
    m_recvSocket->SetRecvPktInfo(true);
    m_recvSocket->ShutdownSend();
  }

  // Per-interface send sockets (exclude loopback)
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces (); ++i)
  {
    Ipv4InterfaceAddress ifAddr = m_ipv4->GetAddress (i, 0);
    Ipv4Address local = ifAddr.GetLocal ();
    if (local == Ipv4Address::GetLoopback ()) continue;

    Ptr<Socket> s = Socket::CreateSocket (GetObject<Node> (), UdpSocketFactory::GetTypeId ());
    s->SetAllowBroadcast (true);
    s->SetRecvCallback (MakeCallback (&DVRoutingProtocol::RecvDVMessage, this));
    if (s->Bind (InetSocketAddress (local, DV_PORT_NUMBER))) {
      NS_FATAL_ERROR("Failed to bind DV per-interface socket");
    }
    s->BindToNetDevice(m_ipv4->GetNetDevice(i));
    m_socketAddresses[s] = ifAddr;
  }

  // Neighbor timers
  m_neighborTimers = CreateObject<NeighborTimers>();
  m_neighborTimers->Configure(Seconds(1.0), Seconds(1.0));
  m_neighborTimers->SetAuditCallback(MakeCallback(&DVRoutingProtocol::AuditHellos, this));
  m_neighborTimers->Start();

  // DV timers
  m_periodicUpdateTimer.SetFunction(&DVRoutingProtocol::SendPeriodicUpdate, this);
  m_periodicUpdateTimer.Schedule(m_periodicInterval);
  m_triggeredUpdateTimer.SetFunction(&DVRoutingProtocol::SendPeriodicUpdate, this);

  // Start ping audit
  AuditPings();
}

/********** Ipv4RoutingProtocol passthroughs **********/
void DVRoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper>, Time::Unit) const {}

Ptr<Ipv4Route>
DVRoutingProtocol::RouteOutput(Ptr<Packet> p, const Ipv4Header &h, Ptr<NetDevice> oif, Socket::SocketErrno &err)
{
  return m_staticRouting->RouteOutput(p, h, oif, err);
}

bool DVRoutingProtocol::RouteInput(Ptr<const Packet> p, const Ipv4Header &h, Ptr<const NetDevice> idev,
                                   UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                                   LocalDeliverCallback lcb, ErrorCallback ecb)
{
  Ipv4Address dst = h.GetDestination();
  Ipv4Address src = h.GetSource();

  if (IsOwnAddress(src)) return true;

  uint32_t inIf = m_ipv4->GetInterfaceForDevice(idev);
  if (m_ipv4->IsDestinationAddress(dst, inIf)) {
    if (!lcb.IsNull()) { lcb(p, h, inIf); return true; } else { return false; }
  }
  if (m_staticRouting->RouteInput(p, h, idev, ucb, mcb, lcb, ecb)) return true;
  return false;
}

void DVRoutingProtocol::NotifyInterfaceUp(uint32_t i)    { m_staticRouting->NotifyInterfaceUp(i); }
void DVRoutingProtocol::NotifyInterfaceDown(uint32_t i)  { m_staticRouting->NotifyInterfaceDown(i); }
void DVRoutingProtocol::NotifyAddAddress(uint32_t i, Ipv4InterfaceAddress a) { m_staticRouting->NotifyAddAddress(i, a); }
void DVRoutingProtocol::NotifyRemoveAddress(uint32_t i, Ipv4InterfaceAddress a) { m_staticRouting->NotifyRemoveAddress(i, a); }

/********** Address mapping helpers **********/
void DVRoutingProtocol::SetMainInterface(uint32_t mainInterface)
{
  m_mainAddress = m_ipv4->GetAddress(mainInterface, 0).GetLocal();
}

void DVRoutingProtocol::SetNodeAddressMap(std::map<uint32_t, Ipv4Address> nodeAddressMap)
{
  m_nodeAddressMap = nodeAddressMap;
}

void DVRoutingProtocol::SetAddressNodeMap(std::map<Ipv4Address, uint32_t> addressNodeMap)
{
  m_addressNodeMap = addressNodeMap;
}

Ipv4Address DVRoutingProtocol::ResolveNodeIpAddress(uint32_t nodeNumber)
{
  auto it = m_nodeAddressMap.find(nodeNumber);
  return (it == m_nodeAddressMap.end()) ? Ipv4Address::GetAny() : it->second;
}

std::string DVRoutingProtocol::ReverseLookup(Ipv4Address ip)
{
  auto it = m_addressNodeMap.find(ip);
  if (it == m_addressNodeMap.end()) return "0";
  return std::to_string(it->second);
}

bool DVRoutingProtocol::IsOwnAddress(Ipv4Address addr)
{
  for (const auto &kv : m_socketAddresses)
    if (kv.second.GetLocal() == addr) return true;
  return false;
}

/********** Command handling **********/
void DVRoutingProtocol::ProcessCommand(std::vector<std::string> tokens)
{
  if (tokens.empty()) return;
  auto it = tokens.begin();
  std::string cmd = *it;

  if (cmd == "PING") {
    if (tokens.size() < 3) { ERROR_LOG("Insufficient PING params"); return; }
    ++it; uint32_t nodeNumber = 0; { std::istringstream s(*it); s >> nodeNumber; }
    ++it; std::string msg = *it;
    Ipv4Address dest = ResolveNodeIpAddress(nodeNumber);
    if (dest == Ipv4Address::GetAny()) return;

    uint32_t seq = GetNextSequenceNumber();
    TRAFFIC_LOG("Sending PING_REQ to Node: " << nodeNumber << " IP: " << dest << " Message: " << msg << " SequenceNumber: " << seq);

    Ptr<PingRequest> req = Create<PingRequest>(seq, Simulator::Now(), dest, msg);
    m_pingTracker.insert({seq, req});

    DVMessage dv(DVMessage::PING_REQ, seq, m_maxTTL, m_mainAddress);
    dv.SetPingReq(dest, msg);

    Ptr<Packet> p = Create<Packet>();
    p->AddHeader(dv);
    BroadcastPacket(p);
  } else if (cmd == "DUMP") {
    if (tokens.size() < 2) { ERROR_LOG("Insufficient Parameters!"); return; }
    ++it; std::string table = *it;
    if (table == "ROUTES" || table == "ROUTING") DumpRoutingTable();
    else if (table == "NEIGHBORS" || table == "NEIGHBOURS") DumpNeighbors();
  }
}

/********** Status dumps (autograder hooks) **********/
void DVRoutingProtocol::DumpNeighbors()
{
  STATUS_LOG(std::endl
             << "**************** Neighbor List ********************" << std::endl
             << "NeighborNumber\t\tNeighborAddr\t\tInterfaceAddr");
  auto neighbors = m_neighbors.Snapshot();
  PRINT_LOG(neighbors.size());
  for (const auto &e : neighbors) {
    PRINT_LOG(ReverseLookup(e.neighborAddress) << "\t\t\t"
             << e.neighborAddress << "\t\t" << e.interfaceAddress);
    checkNeighborTableEntry(ReverseLookup(e.neighborAddress), e.neighborAddress, e.interfaceAddress);
  }
}

void DVRoutingProtocol::DumpRoutingTable()
{
  STATUS_LOG(std::endl
             << "**************** Route Table ********************" << std::endl
             << "DestNumber\t\tDestAddr\t\tNextHopNumber\t\tNextHopAddr\t\tInterfaceAddr\t\tCost");
  PRINT_LOG("");

  PRINT_LOG(m_routingTable.size());
  for (const auto &kv : m_routingTable) {
    const RoutingTableEntry &e = kv.second;
    std::string dstStr = ReverseLookup(e.dest);
    std::string nhStr  = ReverseLookup(e.nextHop);
    uint32_t nh = 0; try { nh = static_cast<uint32_t>(std::stoul(nhStr)); } catch (...) { nh = 0; }

    PRINT_LOG(dstStr << "\t\t\t" << e.dest << "\t\t"
             << nhStr  << "\t\t\t" << e.nextHop << "\t\t"
             << e.interface << "\t\t" << e.cost);

    checkRouteTableEntry(dstStr, e.dest, nh, e.nextHop, e.interface, e.cost);
  }
}

/********** Ping/Hello **********/
void DVRoutingProtocol::ProcessPingReq(DVMessage dv)
{
  if (!IsOwnAddress(dv.GetPingReq().destinationAddress)) return;
  std::string from = ReverseLookup(dv.GetOriginatorAddress());
  TRAFFIC_LOG("Received PING_REQ, From Node: " << from << ", Message: " << dv.GetPingReq().pingMessage);
  DVMessage rsp(DVMessage::PING_RSP, dv.GetSequenceNumber(), m_maxTTL, m_mainAddress);
  rsp.SetPingRsp(dv.GetOriginatorAddress(), dv.GetPingReq().pingMessage);
  Ptr<Packet> p = Create<Packet>(); p->AddHeader(rsp); BroadcastPacket(p);
}

void DVRoutingProtocol::ProcessPingRsp(DVMessage dv)
{
  if (!IsOwnAddress(dv.GetPingRsp().destinationAddress)) return;
  auto it = m_pingTracker.find(dv.GetSequenceNumber());
  if (it != m_pingTracker.end()) {
    TRAFFIC_LOG("Received PING_RSP, From Node: " << ReverseLookup(dv.GetOriginatorAddress())
                << ", Message: " << dv.GetPingRsp().pingMessage);
    m_pingTracker.erase(it);
  }
}

void DVRoutingProtocol::ProcessHelloReq(DVMessage dv)
{
  std::string from = ReverseLookup(dv.GetOriginatorAddress());
  TRAFFIC_LOG("Received HELLO_REQ, From Node: " << from << ", Message: " << dv.GetHelloReq().helloMessage);
  DVMessage rsp(DVMessage::HELLO_RSP, dv.GetSequenceNumber(), 1, m_mainAddress);
  rsp.SetHelloRsp(dv.GetOriginatorAddress(), dv.GetHelloReq().helloMessage);
  Ptr<Packet> p = Create<Packet>(); p->AddHeader(rsp); BroadcastPacket(p);
}

void DVRoutingProtocol::ProcessHelloRsp(DVMessage dv, Ipv4Address localIf)
{
  Ipv4Address neigh = dv.GetOriginatorAddress();
  m_neighbors.ObserveHello(neigh, localIf);
  TRAFFIC_LOG("Received HELLO_RSP, From Node: " << ReverseLookup(neigh)
              << ", Message: " << dv.GetHelloRsp().helloMessage);
}

/********** Recv **********/
void DVRoutingProtocol::RecvDVMessage (Ptr<Socket> socket)
{
  Address srcAddr;
  Ptr<Packet> pkt = socket->RecvFrom(srcAddr);

  DVMessage dv;
  Ipv4PacketInfoTag tag;
  if (!pkt->RemovePacketTag(tag)) { NS_ABORT_MSG("No incoming interface on DV message"); }
  uint32_t incomingIf = tag.GetRecvIf();

  if (!pkt->RemoveHeader(dv)) { NS_ABORT_MSG("Failed to remove DV header"); }

  // Use incomingIf to determine local interface address (avoids -Werror=unused-variable)
  Ipv4Address localIf = Ipv4Address();
  if (incomingIf < m_ipv4->GetNInterfaces()) {
    // This uses the IPv4 interface index (best mapping when tag is present)
    localIf = m_ipv4->GetAddress(incomingIf, 0).GetLocal();
  }
  // Fallback if something goes odd (shouldn’t normally happen)
  if (localIf == Ipv4Address() || localIf == Ipv4Address::GetAny()) {
    for (const auto &kv : m_socketAddresses) {
      if (kv.second.GetLocal() != Ipv4Address::GetLoopback()) {
        localIf = kv.second.GetLocal();
        break;
      }
    }
  }

  switch (dv.GetMessageType())
  {
    case DVMessage::PING_REQ:  ProcessPingReq(dv); break;
    case DVMessage::PING_RSP:  ProcessPingRsp(dv); break;
    case DVMessage::HELLO_REQ: ProcessHelloReq(dv); break;
    case DVMessage::HELLO_RSP: ProcessHelloRsp(dv, localIf); break;
    case DVMessage::DV_UPDATE: ProcessDvUpdate(dv, localIf); break;
    default: ERROR_LOG("Unknown Message Type!"); break;
  }
}

/********** Send **********/
void DVRoutingProtocol::BroadcastPacket(Ptr<Packet> pkt)
{
  for (const auto &kv : m_socketAddresses) {
    Ptr<Packet> copy = pkt->Copy();
    Ipv4Address bcast = kv.second.GetLocal().GetSubnetDirectedBroadcast(kv.second.GetMask());
    kv.first->SendTo(copy, 0, InetSocketAddress(bcast, DV_PORT_NUMBER));
  }
}

void DVRoutingProtocol::SendPeriodicUpdate()
{
  CheckNeighborLoss();

  // Build advertised vector: include self at cost 0
  std::vector<DVMessage::DvVectorItem> vec;
  vec.push_back({ m_mainAddress, 0 });

  for (const auto &kv : m_routingTable) {
    const RoutingTableEntry &e = kv.second;
    if (e.dest == m_mainAddress) continue;
    uint32_t advCost = (e.cost > INVALIDATED_ROUTE) ? INVALIDATED_ROUTE : e.cost;
    vec.push_back({ e.dest, advCost });
  }

  DVMessage msg(DVMessage::DV_UPDATE, GetNextSequenceNumber(), m_maxTTL, m_mainAddress);
  msg.SetDvUpdate(vec);

  for (const auto &kv : m_socketAddresses) {
    Ptr<Socket> sock = kv.first;
    const Ipv4InterfaceAddress &ifAddr = kv.second;
    Ptr<Packet> p = Create<Packet>(); p->AddHeader(msg);
    Ipv4Address bcast = ifAddr.GetLocal().GetSubnetDirectedBroadcast(ifAddr.GetMask());
    sock->SendTo(p, 0, InetSocketAddress(bcast, DV_PORT_NUMBER));
  }

  m_periodicUpdateTimer.Cancel();
  m_periodicUpdateTimer.Schedule(m_periodicInterval);
}

void DVRoutingProtocol::TriggerUpdateSoon()
{
  if (!m_triggeredUpdateTimer.IsRunning())
    m_triggeredUpdateTimer.Schedule(m_triggerHold);
}

/********** DV core **********/
void DVRoutingProtocol::CheckNeighborLoss()
{
  // Use your NeighborTable API (Contains) to invalidate routes whose next hop vanished
  for (auto &kv : m_routingTable) {
    if (!m_neighbors.Contains(kv.second.nextHop)) {
      kv.second.cost = INVALIDATED_ROUTE;
    }
  }
}

uint32_t DVRoutingProtocol::UpdateRoute(Ipv4Address dest, Ipv4Address source,
                                        Ipv4Address sourceInterface, uint32_t sourceCost)
{
  if (dest == m_mainAddress) return ROUTE_NOT_UPDATED;

  if (sourceCost >= INVALIDATED_ROUTE) {
    auto it = m_routingTable.find(dest);
    if (it != m_routingTable.end() && it->second.nextHop == source) {
      it->second.cost = INVALIDATED_ROUTE;
      it->second.timestamp = Simulator::Now();
      return ROUTE_UPDATED;
    }
    return ROUTE_NOT_UPDATED;
  }

  uint32_t candidate = std::min<uint32_t>(INVALIDATED_ROUTE, sourceCost + 1);

  auto it = m_routingTable.find(dest);
  if (it == m_routingTable.end()) {
    // new route (this is the block your teammate flagged)
    RoutingTableEntry &e = m_routingTable[dest];
    e.dest      = dest;
    e.nextHop   = source;
    e.interface = sourceInterface;
    e.cost      = candidate;
    e.timestamp = Simulator::Now();
    return ROUTE_UPDATED;
  }

  RoutingTableEntry &cur = it->second;
  if (cur.cost == INVALIDATED_ROUTE || candidate < cur.cost || cur.nextHop == source) {
    cur.nextHop   = source;
    cur.interface = sourceInterface;
    cur.cost      = candidate;
    cur.timestamp = Simulator::Now();
    return ROUTE_UPDATED;
  }

  return ROUTE_NOT_UPDATED;
}

void DVRoutingProtocol::ProcessDvUpdate(DVMessage dv, Ipv4Address sourceInterface)
{
  Ipv4Address neighbor = dv.GetOriginatorAddress();
  bool changed = false;

  for (const auto &item : dv.GetDvUpdate().vec) {
    if (item.dest == m_mainAddress) continue;
    if (UpdateRoute(item.dest, neighbor, sourceInterface, item.cost) == ROUTE_UPDATED)
      changed = true;
  }

  if (changed) {
    TriggerUpdateSoon();
  }
}

/********** Misc **********/
uint32_t DVRoutingProtocol::GetNextSequenceNumber()
{
  m_currentSequenceNumber = (m_currentSequenceNumber + 1) % (DV_MAX_SEQUENCE_NUMBER + 1);
  return m_currentSequenceNumber;
}

void DVRoutingProtocol::AuditPings()
{
  for (auto it = m_pingTracker.begin(); it != m_pingTracker.end(); ) {
    Ptr<PingRequest> pr = it->second;
    if (pr->GetTimestamp().GetMilliSeconds() + m_pingTimeout.GetMilliSeconds() <= Simulator::Now().GetMilliSeconds())
      it = m_pingTracker.erase(it);
    else
      ++it;
  }
  m_auditPingsTimer.Schedule(m_pingTimeout);
}

void DVRoutingProtocol::AuditHellos()
{
  m_neighbors.Audit();
}

} // namespace ns3
