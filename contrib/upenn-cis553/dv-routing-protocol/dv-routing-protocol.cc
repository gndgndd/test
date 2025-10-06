/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/dv-routing-protocol.h"
#include "ns3/double.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-packet-info-tag.h"
#include "ns3/ipv4-route.h"
#include "ns3/log.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/test-result.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <sstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("DVRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(DVRoutingProtocol);

namespace {
  constexpr uint32_t kSeqMax  = 0xFFFF;
  constexpr uint16_t kWirePort = 698; // on-the-wire UDP port
  constexpr uint32_t kInfCost = 1000000;
}

TypeId
DVRoutingProtocol::GetTypeId(void)
{
  static TypeId tid = TypeId("DVRoutingProtocol")
    .SetParent<PennRoutingProtocol>()
    .AddConstructor<DVRoutingProtocol>()
    .AddAttribute("DVPort", "Listening port for DV packets (per-interface sockets).",
                  UintegerValue(5000),
                  MakeUintegerAccessor(&DVRoutingProtocol::m_dvPort),
                  MakeUintegerChecker<uint16_t>())
    .AddAttribute("PingTimeout","Timeout for PING_REQ in milliseconds.",
                  TimeValue(MilliSeconds(2000)),
                  MakeTimeAccessor(&DVRoutingProtocol::m_pingTimeout),
                  MakeTimeChecker())
    .AddAttribute("MaxTTL","Maximum TTL for DV control-plane packets.",
                  UintegerValue(16),
                  MakeUintegerAccessor(&DVRoutingProtocol::m_maxTTL),
                  MakeUintegerChecker<uint8_t>())
    .AddAttribute("PeriodicInterval","Periodic DV_UPDATE interval.",
                  TimeValue(Seconds(2.0)),
                  MakeTimeAccessor(&DVRoutingProtocol::m_periodicEvery),
                  MakeTimeChecker())
    .AddAttribute("TriggerHold","Triggered update hold-down window.",
                  TimeValue(MilliSeconds(300)),
                  MakeTimeAccessor(&DVRoutingProtocol::m_burstHold),
                  MakeTimeChecker())
    .AddAttribute("PoisonReverse","Advertise ∞ to next-hop neighbor for routes learned via it.",
                  BooleanValue(true),
                  MakeBooleanAccessor(&DVRoutingProtocol::m_poisonReverse),
                  MakeBooleanChecker());
  return tid;
}

DVRoutingProtocol::DVRoutingProtocol()
  : m_auditPingsTimer(Timer::CANCEL_ON_DESTROY),
    m_periodicAdv(Timer::CANCEL_ON_DESTROY),
    m_burstAdv(Timer::CANCEL_ON_DESTROY),
    m_helloDriver(Timer::CANCEL_ON_DESTROY)
{
  m_staticRouting = Create<Ipv4StaticRouting>();
}

DVRoutingProtocol::~DVRoutingProtocol() = default;

void DVRoutingProtocol::DoDispose()
{
  if (m_rxSock) { m_rxSock->Close(); m_rxSock = nullptr; }
  for (auto &kv : m_sockIf) { kv.first->Close(); }
  m_sockIf.clear();
  m_staticRouting = nullptr;
  m_auditPingsTimer.Cancel();
  m_periodicAdv.Cancel();
  m_burstAdv.Cancel();
  m_helloDriver.Cancel();
  m_pingTracker.clear();
  PennRoutingProtocol::DoDispose();
}

void DVRoutingProtocol::SetMainInterface(uint32_t mainInterface)
{
  m_mainAddress = m_ipv4->GetAddress(mainInterface, 0).GetLocal();
}

void DVRoutingProtocol::SetNodeAddressMap(std::map<uint32_t, Ipv4Address> nodeAddressMap)
{
  m_nodeAddressMap = std::move(nodeAddressMap);
}

void DVRoutingProtocol::SetAddressNodeMap(std::map<Ipv4Address, uint32_t> addressNodeMap)
{
  m_addressNodeMap = std::move(addressNodeMap);
}

Ipv4Address
DVRoutingProtocol::ResolveNodeIpAddress(uint32_t nodeNumber)
{
  auto it = m_nodeAddressMap.find(nodeNumber);
  return (it == m_nodeAddressMap.end()) ? Ipv4Address::GetAny() : it->second;
}

std::string
DVRoutingProtocol::ReverseLookup(Ipv4Address ipAddress)
{
  auto it = m_addressNodeMap.find(ipAddress);
  if (it != m_addressNodeMap.end())
  {
    std::ostringstream sin;
    sin << it->second;
    return sin.str();
  }
  return "Unknown";
}

void DVRoutingProtocol::DoInitialize()
{
  if (m_mainAddress == Ipv4Address())
  {
    Ipv4Address loopback("127.0.0.1");
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++)
    {
      Ipv4Address addr = m_ipv4->GetAddress(i, 0).GetLocal();
      if (addr != loopback) { m_mainAddress = addr; break; }
    }
    NS_ASSERT(m_mainAddress != Ipv4Address());
  }

  NS_LOG_DEBUG("[DV/BOOT] up on " << m_mainAddress);

  bool started = false;
  // Create sockets on all non-loopback interfaces
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++)
  {
    Ipv4Address ipAddress = m_ipv4->GetAddress(i, 0).GetLocal();
    if (ipAddress == Ipv4Address::GetLoopback()) continue;

    if (m_rxSock == nullptr)
    {
      m_rxSock = Socket::CreateSocket(GetObject<Node>(), UdpSocketFactory::GetTypeId());
      m_rxSock->SetAllowBroadcast(true);
      InetSocketAddress inetAddr(Ipv4Address::GetAny(), kWirePort);
      m_rxSock->SetRecvCallback(MakeCallback(&DVRoutingProtocol::RecvDVMessage, this));
      if (m_rxSock->Bind(inetAddr)) NS_FATAL_ERROR("Failed to bind() DV RX socket");
      m_rxSock->SetRecvPktInfo(true);
      m_rxSock->ShutdownSend();
    }

    Ptr<Socket> s = Socket::CreateSocket(GetObject<Node>(), UdpSocketFactory::GetTypeId());
    s->SetAllowBroadcast(true);
    InetSocketAddress ia(m_ipv4->GetAddress(i, 0).GetLocal(), m_dvPort);
    s->SetRecvCallback(MakeCallback(&DVRoutingProtocol::RecvDVMessage, this));
    if (s->Bind(ia)) NS_FATAL_ERROR("DVRoutingProtocol::DoInitialize::Failed to bind socket!");
    s->BindToNetDevice(m_ipv4->GetNetDevice(i));
    m_sockIf[s] = m_ipv4->GetAddress(i, 0);
    started = true;
  }

  if (started)
  {
    // neighbor timers mirroring LS infra
    m_neighborTimers = CreateObject<NeighborTimers>();
    m_neighborTimers->Configure(Seconds(1.0), Seconds(1.0));
    m_neighborTimers->SetAuditCallback(MakeCallback(&DVRoutingProtocol::AuditHellos, this));
    m_neighborTimers->Start();

    // periodic DV update
    m_periodicAdv.SetFunction(&DVRoutingProtocol::SendPeriodicUpdate, this);
    m_periodicAdv.Schedule(m_periodicEvery);

    // triggered burst update
    m_burstAdv.SetFunction(&DVRoutingProtocol::SendPeriodicUpdate, this);

    // ping audit
    m_auditPingsTimer.SetFunction(&DVRoutingProtocol::AuditPings, this);
    AuditPings();

    NS_LOG_DEBUG("[DV/BOOT] sockets+timers armed @ " << m_mainAddress);
  }
}

void DVRoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper>, Time::Unit) const {}

Ptr<Ipv4Route>
DVRoutingProtocol::RouteOutput(Ptr<Packet> packet, const Ipv4Header &header, Ptr<NetDevice> outInterface, Socket::SocketErrno &sockerr)
{
  Ptr<Ipv4Route> ipv4Route = m_staticRouting->RouteOutput(packet, header, outInterface, sockerr);
  if (ipv4Route)
    DEBUG_LOG("[DV/ROUTE] hit dst=" << ipv4Route->GetDestination() << " via=" << ipv4Route->GetGateway());
  else
    DEBUG_LOG("[DV/ROUTE] miss dst=" << header.GetDestination());
  return ipv4Route;
}

bool DVRoutingProtocol::RouteInput(Ptr<const Packet> packet, const Ipv4Header &header, Ptr<const NetDevice> inputDev,
                                   UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                                   LocalDeliverCallback lcb, ErrorCallback ecb)
{
  Ipv4Address destinationAddress = header.GetDestination();
  Ipv4Address sourceAddress = header.GetSource();

  if (IsOwnAddress(sourceAddress)) return true;

  uint32_t interfaceNum = m_ipv4->GetInterfaceForDevice(inputDev);
  if (m_ipv4->IsDestinationAddress(destinationAddress, interfaceNum))
  {
    if (!lcb.IsNull()) { lcb(packet, header, interfaceNum); return true; }
    return false;
  }

  if (m_staticRouting->RouteInput(packet, header, inputDev, ucb, mcb, lcb, ecb)) return true;
  DEBUG_LOG("[DV/FWD] no route to " << destinationAddress);
  return false;
}

void DVRoutingProtocol::BroadcastPacket(Ptr<Packet> packet)
{
  for (const auto &kv : m_sockIf)
  {
    Ptr<Packet> pkt = packet->Copy();
    const Ipv4InterfaceAddress &ifa = kv.second;
    Ipv4Address broadcastAddr = ifa.GetLocal().GetSubnetDirectedBroadcast(ifa.GetMask());
    kv.first->SendTo(pkt, 0, InetSocketAddress(broadcastAddr, kWirePort));
  }
}

void DVRoutingProtocol::ProcessCommand(std::vector<std::string> tokens)
{
  if (tokens.empty()) return;
  auto it = tokens.begin();
  std::string command = *it;
  if (command == "PING")
  {
    if (tokens.size() < 3) { ERROR_LOG("Insufficient PING params..."); return; }
    ++it; std::istringstream sin(*it); uint32_t nodeNumber; sin >> nodeNumber;
    ++it; std::string pingMessage = *it;
    Ipv4Address destAddress = ResolveNodeIpAddress(nodeNumber);
    if (destAddress != Ipv4Address::GetAny())
    {
      uint32_t sequenceNumber = GetNextSequenceNumber();
      TRAFFIC_LOG("[DV/PING] to node " << nodeNumber << " ip " << destAddress << " msg '" << pingMessage << "' seq " << sequenceNumber);
      Ptr<PingRequest> pingRequest = Create<PingRequest>(sequenceNumber, Simulator::Now(), destAddress, pingMessage);
      m_pingTracker.insert(std::make_pair(sequenceNumber, pingRequest));
      Ptr<Packet> packet = Create<Packet>();
      DVMessage dvMessage = DVMessage(DVMessage::PING_REQ, sequenceNumber, m_maxTTL, m_mainAddress);
      dvMessage.SetPingReq(destAddress, pingMessage);
      packet->AddHeader(dvMessage);
      BroadcastPacket(packet);
    }
  }
  else if (command == "DUMP")
  {
    if (tokens.size() < 2) { ERROR_LOG("Insufficient Parameters!"); return; }
    ++it; std::string table = *it;
    if (table == "ROUTES" || table == "ROUTING") DumpRoutingTable();
    else if (table == "NEIGHBORS" || table == "NEIGHBOURS") DumpNeighbors();
  }
}

void DVRoutingProtocol::DumpNeighbors()
{
  STATUS_LOG(std::endl
    << "**************** Neighbor List ********************" << std::endl
    << "NeighborNumber\t\tNeighborAddr\t\tInterfaceAddr");
  auto snap = m_neighbors.Snapshot();
  PRINT_LOG(snap.size());
  for (const auto &e : snap)
  {
    auto it = m_addressNodeMap.find(e.neighborAddress);
    if (it != m_addressNodeMap.end())
    {
      PRINT_LOG(it->second << "\t\t\t" << e.neighborAddress << "\t\t" << e.interfaceAddress);
      checkNeighborTableEntry(it->second, e.neighborAddress, e.interfaceAddress);
    }
  }
  PRINT_LOG("");
}

void DVRoutingProtocol::DumpRoutingTable()
{
  STATUS_LOG(std::endl
    << "**************** Route Table ********************" << std::endl
    << "DestNumber\t\tDestAddr\t\tNextHopNumber\t\tNextHopAddr\t\tInterfaceAddr\t\tCost");
  PRINT_LOG("");
  PRINT_LOG(m_routes.size());
  for (const auto &kv : m_routes)
  {
    const DvRouteRow &r = kv.second;
    auto dIt = m_addressNodeMap.find(r.dest);
    auto nIt = m_addressNodeMap.find(r.nextHop);
    if (dIt==m_addressNodeMap.end() || nIt==m_addressNodeMap.end()) continue;
    PRINT_LOG(dIt->second << "\t\t\t" << r.dest << "\t\t" << nIt->second << "\t\t\t" << r.nextHop << "\t\t" << r.oif << "\t\t" << r.cost);
    if (r.cost <= 16u) checkRouteTableEntry(dIt->second, r.dest, nIt->second, r.nextHop, r.oif, r.cost);
  }
  PRINT_LOG("");
}

void DVRoutingProtocol::RecvDVMessage(Ptr<Socket> socket)
{
  Address sourceAddr;
  Ptr<Packet> packet = socket->RecvFrom(sourceAddr);
  DVMessage dvMessage;
  Ipv4PacketInfoTag interfaceInfo;
  if (!packet->RemovePacketTag(interfaceInfo)) NS_ABORT_MSG("No incoming interface on DV message, aborting.");
  uint32_t incomingIf = interfaceInfo.GetRecvIf();

  if (!packet->RemoveHeader(dvMessage)) NS_ABORT_MSG("No DV header, aborting.");

  Ipv4Address interface;
  uint32_t idx = 1;
  for (auto iter = m_sockIf.begin(); iter != m_sockIf.end(); ++iter, ++idx)
  {
    if (idx == incomingIf) { interface = iter->second.GetLocal(); break; }
  }

  // Duplicate filter (origin, seq)
  auto key = std::make_pair(dvMessage.GetOriginatorAddress(), dvMessage.GetSequenceNumber());
  if (dvMessage.GetMessageType() == DVMessage::DV_UPDATE)
  {
    if (!m_seenUpdates.insert(key).second)
    {
      DEBUG_LOG("[DV/DUPE] drop origin=" << key.first << " seq=" << key.second);
      return;
    }
  }

  switch (dvMessage.GetMessageType())
  {
    case DVMessage::PING_REQ:  ProcessPingReq(dvMessage); break;
    case DVMessage::PING_RSP:  ProcessPingRsp(dvMessage); break;
    case DVMessage::HELLO_REQ: ProcessHelloReq(dvMessage); break;
    case DVMessage::HELLO_RSP: ProcessHelloRsp(dvMessage, interface); break;
    case DVMessage::DV_UPDATE: ProcessDvUpdate(dvMessage, interface); break;
    default: ERROR_LOG("Unknown Message Type!"); break;
  }
}

void DVRoutingProtocol::ProcessHelloReq(DVMessage msg)
{
  std::string from = ReverseLookup(msg.GetOriginatorAddress());
  TRAFFIC_LOG("[DV/HELLO] req from " << from << " msg=" << msg.GetHelloReq().helloMessage);

  DVMessage rsp(DVMessage::HELLO_RSP, msg.GetSequenceNumber(), 1, m_mainAddress);
  rsp.SetHelloRsp(msg.GetOriginatorAddress(), msg.GetHelloReq().helloMessage);
  Ptr<Packet> p = Create<Packet>(); p->AddHeader(rsp);
  BroadcastPacket(p);
}

void DVRoutingProtocol::ProcessHelloRsp(DVMessage msg, Ipv4Address localIf)
{
  Ipv4Address neigh = msg.GetOriginatorAddress();
  m_neighbors.ObserveHello(neigh, localIf);
  TRAFFIC_LOG("[DV/HELLO] rsp from " << ReverseLookup(neigh) << " msg=" << msg.GetHelloRsp().helloMessage);
}

void DVRoutingProtocol::ProcessPingReq(DVMessage dvMessage)
{
  if (!IsOwnAddress(dvMessage.GetPingReq().destinationAddress)) return;
  TRAFFIC_LOG("[DV/PING] req from " << ReverseLookup(dvMessage.GetOriginatorAddress())
             << " msg=" << dvMessage.GetPingReq().pingMessage);
  DVMessage dvResp = DVMessage(DVMessage::PING_RSP, dvMessage.GetSequenceNumber(), m_maxTTL, m_mainAddress);
  dvResp.SetPingRsp(dvMessage.GetOriginatorAddress(), dvMessage.GetPingReq().pingMessage);
  Ptr<Packet> packet = Create<Packet>(); packet->AddHeader(dvResp);
  BroadcastPacket(packet);
}

void DVRoutingProtocol::ProcessPingRsp(DVMessage dvMessage)
{
  if (!IsOwnAddress(dvMessage.GetPingRsp().destinationAddress)) return;
  auto iter = m_pingTracker.find(dvMessage.GetSequenceNumber());
  if (iter != m_pingTracker.end())
  {
    TRAFFIC_LOG("[DV/PING] rsp from " << ReverseLookup(dvMessage.GetOriginatorAddress())
               << " msg=" << dvMessage.GetPingRsp().pingMessage);
    m_pingTracker.erase(iter);
  }
  else DEBUG_LOG("[DV/PING] invalid/late PING_RSP");
}

bool DVRoutingProtocol::IsOwnAddress(Ipv4Address originatorAddress)
{
  for (const auto &kv : m_sockIf)
  {
    if (originatorAddress == kv.second.GetLocal()) return true;
  }
  return false;
}

void DVRoutingProtocol::AuditPings()
{
  for (auto it = m_pingTracker.begin(); it != m_pingTracker.end(); )
  {
    Ptr<PingRequest> pingRequest = it->second;
    if (pingRequest->GetTimestamp().GetMilliSeconds() + m_pingTimeout.GetMilliSeconds() <= Simulator::Now().GetMilliSeconds())
    {
      DEBUG_LOG("[DV/PING] expired msg: " << pingRequest->GetPingMessage());
      it = m_pingTracker.erase(it);
    }
    else ++it;
  }
  m_auditPingsTimer.Schedule(m_pingTimeout);
}

uint32_t DVRoutingProtocol::GetNextSequenceNumber()
{
  m_seq = (m_seq + 1) % (kSeqMax + 1);
  return m_seq;
}

void DVRoutingProtocol::NotifyInterfaceUp(uint32_t i)   { m_staticRouting->NotifyInterfaceUp(i); }
void DVRoutingProtocol::NotifyInterfaceDown(uint32_t i) { m_staticRouting->NotifyInterfaceDown(i); }
void DVRoutingProtocol::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) { m_staticRouting->NotifyAddAddress(interface, address); }
void DVRoutingProtocol::NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) { m_staticRouting->NotifyRemoveAddress(interface, address); }

void DVRoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4)
{
  NS_ASSERT(ipv4 != 0);
  NS_ASSERT(m_ipv4 == 0);
  NS_LOG_DEBUG("[DV/INIT] create");
  m_ipv4 = ipv4;
  m_staticRouting->SetIpv4(m_ipv4);
}

void DVRoutingProtocol::AuditHellos()
{
  // Send HELLO_REQ (with small jitter via neighbor timers)
  DVMessage helloReq(DVMessage::HELLO_REQ, GetNextSequenceNumber(), 1, m_mainAddress);
  helloReq.SetHelloReq("hello");
  Ptr<Packet> p = Create<Packet>(); p->AddHeader(helloReq);
  BroadcastPacket(p);

  // Expire neighbors as needed
  m_neighbors.Audit();
  DEBUG_LOG("[DV/HELLO] audit neighbors=" << m_neighbors.Size());
}

void DVRoutingProtocol::SendPeriodicUpdate()
{
  CheckNeighborLoss();

  // collect vector; cap cost to 16 per RIP-ish semantics
  std::vector<DVMessage::DvVectorItem> vec;
  vec.push_back({ m_mainAddress, 0 });
  for (const auto &kv : m_routes)
  {
    const auto &row = kv.second;
    if (row.dest == m_mainAddress) continue;
    DVMessage::DvVectorItem item{row.dest, std::min(16u, row.cost)};
    vec.push_back(item);
  }

  DVMessage msg(DVMessage::DV_UPDATE, GetNextSequenceNumber(), m_maxTTL, m_mainAddress);
  msg.SetDvUpdate(vec);

  // Optionally apply poison reverse when sending out each iface socket: to keep it simple,
  // we broadcast the same message; receivers do next-hop checks in UpdateRoute().
  for (const auto &kv : m_sockIf)
  {
    Ptr<Packet> pkt = Create<Packet>();
    pkt->AddHeader(msg);
    const Ipv4InterfaceAddress &ifa = kv.second;
    Ipv4Address bcast = ifa.GetLocal().GetSubnetDirectedBroadcast(ifa.GetMask());
    kv.first->SendTo(pkt, 0, InetSocketAddress(bcast, kWirePort));
  }

  // re-arm
  m_periodicAdv.Cancel();
  m_periodicAdv.Schedule(m_periodicEvery);
}

void DVRoutingProtocol::TriggerUpdateSoon()
{
  if (!m_burstAdv.IsRunning()) m_burstAdv.Schedule(m_burstHold);
}

void DVRoutingProtocol::CheckNeighborLoss()
{
  for (auto &kv : m_routes)
  {
    if (!m_neighbors.Contains(kv.second.nextHop))
    {
      kv.second.cost = kInfCost; // mark invalid; still advertised as 16 by sender
    }
  }
}

uint32_t DVRoutingProtocol::UpdateRoute(Ipv4Address dst, Ipv4Address via, Ipv4Address viaIf, uint32_t viaCost)
{
  // if neighbor vanished path, invalidate
  if (!m_neighbors.Contains(via) || viaCost >= kInfCost)
  {
    auto it = m_routes.find(dst);
    if (it == m_routes.end()) return 0;
    if (it->second.nextHop == via) { it->second.cost = kInfCost; return 1; }
    return 0;
  }

  auto it = m_routes.find(dst);
  if (it == m_routes.end())
  {
    DvRouteRow row{dst, via, viaIf, viaCost + 1, Simulator::Now()};
    m_routes[dst] = row;
    return 1;
  }
  DvRouteRow &cur = it->second;
  if (cur.cost == kInfCost || viaCost + 1 < cur.cost || cur.nextHop == via)
  {
    cur.nextHop = via; cur.oif = viaIf; cur.cost = viaCost + 1; cur.timestamp = Simulator::Now();
    return 1;
  }
  return 0;
}

void DVRoutingProtocol::ProcessDvUpdate(DVMessage msg, Ipv4Address localIf)
{
  const Ipv4Address neigh = msg.GetOriginatorAddress();
  bool changed = false;
  for (const auto &item : msg.GetDvUpdate().vec)
  {
    if (item.dest == m_mainAddress) continue;
    changed = (UpdateRoute(item.dest, neigh, localIf, item.cost) == 1) || changed;
  }

  if (changed)
  {
    // reflect into ns-3 static routing
    while (m_staticRouting->GetNRoutes() > 0) m_staticRouting->RemoveRoute(0);
    for (const auto &kv : m_routes)
    {
      const DvRouteRow &r = kv.second;
      if (r.cost >= kInfCost || r.cost > 16u) continue;
      uint32_t ifaceIndex = m_ipv4->GetInterfaceForAddress(r.oif);
      m_staticRouting->AddHostRouteTo(r.dest, r.nextHop, ifaceIndex, r.cost);
    }
    TriggerUpdateSoon();
    DEBUG_LOG("[DV/ADV] topology change applied, routes=" << m_routes.size());
  }
}

std::vector<DvRouteRow> DVRoutingProtocol::Snapshot() const
{
  std::vector<DvRouteRow> v;
  v.reserve(m_routes.size());
  for (const auto &kv : m_routes)
  {
    if (kv.second.cost <= 16u) v.push_back(kv.second);
  }
  return v;
}
