
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
#include "ns3/string.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("DVRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(DVRoutingProtocol);

namespace {
  constexpr uint16_t kWirePort = 698;
  constexpr uint32_t kSeqMax  = 0xFFFF;
}

TypeId
DVRoutingProtocol::GetTypeId(void)
{
  static TypeId tid = TypeId("DVRoutingProtocol")
    .SetParent<PennRoutingProtocol>()
    .AddConstructor<DVRoutingProtocol>()
    .AddAttribute("DVPort",
      "Listening port for DV packets (per-interface sockets).",
      UintegerValue(5000),
      MakeUintegerAccessor(&DVRoutingProtocol::m_dvPort),
      MakeUintegerChecker<uint16_t>())
    .AddAttribute("PingTimeout",
      "Timeout for PING_REQ in milliseconds.",
      TimeValue(MilliSeconds(2000)),
      MakeTimeAccessor(&DVRoutingProtocol::m_pingTimeout),
      MakeTimeChecker())
    .AddAttribute("MaxTTL",
      "Maximum TTL for DV control-plane packets.",
      UintegerValue(16),
      MakeUintegerAccessor(&DVRoutingProtocol::m_maxTTL),
      MakeUintegerChecker<uint8_t>())
    .AddAttribute("MaintenanceInterval",
      "Periodic maintenance tick.",
      TimeValue(Seconds(1.0)),
      MakeTimeAccessor(&DVRoutingProtocol::m_maintenanceEvery),
      MakeTimeChecker())
    .AddAttribute("AdvertInterval",
      "Periodic DV advertisement interval.",
      TimeValue(Seconds(2.0)),
      MakeTimeAccessor(&DVRoutingProtocol::m_periodicEvery),
      MakeTimeChecker())
    .AddAttribute("TriggerHoldDown",
      "Hold-down to coalesce rapid triggered DV updates.",
      TimeValue(MilliSeconds(300)),
      MakeTimeAccessor(&DVRoutingProtocol::m_burstHold),
      MakeTimeChecker())
    .AddAttribute("DvInfinity",
      "Cost used to mark invalid routes.",
      UintegerValue(1000),
      MakeUintegerAccessor(&DVRoutingProtocol::m_dvInfinity),
      MakeUintegerChecker<uint32_t>())
    .AddAttribute("MaxHopCost",
      "Max hop cost considered deliverable (e.g., RIP=16).",
      UintegerValue(16),
      MakeUintegerAccessor(&DVRoutingProtocol::m_maxHopCost),
      MakeUintegerChecker<uint32_t>())
    .AddAttribute("HelloString",
      "Hello payload string used for neighbor discovery.",
      StringValue("Hello!"),
      MakeStringAccessor(&DVRoutingProtocol::m_helloString),
      MakeStringChecker())
  ;
  return tid;
}

DVRoutingProtocol::DVRoutingProtocol()
  : m_auditPingsTimer(Timer::CANCEL_ON_DESTROY),
    m_periodicAdv(Timer::CANCEL_ON_DESTROY),
    m_burstAdv(Timer::CANCEL_ON_DESTROY),
    m_maintenance(Timer::CANCEL_ON_DESTROY)
{
  m_staticRouting = Create<Ipv4StaticRouting>();
}

DVRoutingProtocol::~DVRoutingProtocol() = default;

void DVRoutingProtocol::DoDispose()
{
  if (m_recvSocket) { m_recvSocket->Close(); m_recvSocket = nullptr; }
  for (auto &kv : m_socketAddresses) { kv.first->Close(); }
  m_socketAddresses.clear();

  m_staticRouting = nullptr;

  m_auditPingsTimer.Cancel();
  m_periodicAdv.Cancel();
  m_burstAdv.Cancel();
  m_maintenance.Cancel();
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

Ipv4Address DVRoutingProtocol::ResolveNodeIpAddress(uint32_t nodeNumber)
{
  auto it = m_nodeAddressMap.find(nodeNumber);
  return (it == m_nodeAddressMap.end()) ? Ipv4Address::GetAny() : it->second;
}

std::string DVRoutingProtocol::ReverseLookup(Ipv4Address ip)
{
  auto it = m_addressNodeMap.find(ip);
  if (it == m_addressNodeMap.end()) return "Unknown";
  std::ostringstream os; os << it->second; return os.str();
}

void DVRoutingProtocol::DoInitialize()
{
  if (m_mainAddress == Ipv4Address())
  {
    const Ipv4Address loopback("127.0.0.1");
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); ++i) {
      Ipv4Address addr = m_ipv4->GetAddress(i, 0).GetLocal();
      if (addr != loopback) { m_mainAddress = addr; break; }
    }
    NS_ASSERT(m_mainAddress != Ipv4Address());
  }

  NS_LOG_DEBUG("[DV/BOOT] up on " << m_mainAddress);

  bool started = false;
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); ++i) {
    Ipv4Address ip = m_ipv4->GetAddress(i, 0).GetLocal();
    if (ip == Ipv4Address::GetLoopback()) continue;

    if (m_recvSocket == nullptr) {
      m_recvSocket = Socket::CreateSocket(GetObject<Node>(), UdpSocketFactory::GetTypeId());
      m_recvSocket->SetAllowBroadcast(true);
      InetSocketAddress rxAny(Ipv4Address::GetAny(), kWirePort);
      m_recvSocket->SetRecvCallback(MakeCallback(&DVRoutingProtocol::RecvDVMessage, this));
      if (m_recvSocket->Bind(rxAny)) { NS_FATAL_ERROR("Failed to bind() DV RX socket"); }
      m_recvSocket->SetRecvPktInfo(true);
      m_recvSocket->ShutdownSend();
    }

    Ptr<Socket> s = Socket::CreateSocket(GetObject<Node>(), UdpSocketFactory::GetTypeId());
    s->SetAllowBroadcast(true);
    InetSocketAddress bindAddr(m_ipv4->GetAddress(i, 0).GetLocal(), m_dvPort);
    s->SetRecvCallback(MakeCallback(&DVRoutingProtocol::RecvDVMessage, this));
    if (s->Bind(bindAddr)) { NS_FATAL_ERROR("DV: per-if socket bind failed"); }
    s->BindToNetDevice(m_ipv4->GetNetDevice(i));
    m_socketAddresses[s] = m_ipv4->GetAddress(i, 0);
    started = true;
  }

  if (started) {
    // neighbor timers
    m_neighborTimers = CreateObject<NeighborTimers>();
    m_neighborTimers->Configure(Seconds(1.0), Seconds(1.0));
    m_neighborTimers->SetAuditCallback(MakeCallback(&DVRoutingProtocol::AuditHellos, this));
    m_neighborTimers->Start();

    // DV timers
    m_periodicAdv.SetFunction(&DVRoutingProtocol::PushDvUpdate, this);
    m_periodicAdv.Schedule(m_periodicEvery);
    m_burstAdv.SetFunction(&DVRoutingProtocol::PushDvUpdate, this);

    // pings
    m_auditPingsTimer.SetFunction(&DVRoutingProtocol::AuditPings, this);
    AuditPings();

    // maintenance
    m_maintenance.SetFunction(&DVRoutingProtocol::RunMaintenance, this);
    m_maintenance.Schedule(m_maintenanceEvery);

    NS_LOG_DEBUG("[DV/BOOT] sockets+timers armed @ " << m_mainAddress);
  }
}

void DVRoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper>, Time::Unit) const
{
  
}

Ptr<Ipv4Route>
DVRoutingProtocol::RouteOutput(Ptr<Packet> p, const Ipv4Header &h, Ptr<NetDevice> oif, Socket::SocketErrno &err)
{
  Ptr<Ipv4Route> r = m_staticRouting->RouteOutput(p, h, oif, err);
  if (r) {
    DEBUG_LOG("[DV/ROUTE] dst=" << r->GetDestination() << " nh=" << r->GetGateway()
              << " src=" << r->GetSource() << " dev=" << r->GetOutputDevice());
  } else {
    DEBUG_LOG("[DV/ROUTE] miss dst=" << h.GetDestination());
  }
  return r;
}

bool DVRoutingProtocol::RouteInput(Ptr<const Packet> p, const Ipv4Header &h, Ptr<const NetDevice> idev,
                                   UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                                   LocalDeliverCallback lcb, ErrorCallback ecb)
{
  Ipv4Address dst = h.GetDestination();
  Ipv4Address src = h.GetSource();

  if (IsOwnAddress(src)) return true;

  uint32_t ifIndex = m_ipv4->GetInterfaceForDevice(idev);
  if (m_ipv4->IsDestinationAddress(dst, ifIndex)) {
    if (!lcb.IsNull()) { lcb(p, h, ifIndex); return true; }
    return false;
  }

  if (m_staticRouting->RouteInput(p, h, idev, ucb, mcb, lcb, ecb)) return true;

  DEBUG_LOG("[DV/FWD] no route to " << dst);
  return false;
}

void DVRoutingProtocol::SendOnAllInterfaces(Ptr<Packet> pkt)
{
  for (const auto &kv : m_socketAddresses) {
    Ptr<Packet> copy = pkt->Copy();
    const Ipv4InterfaceAddress &ifa = kv.second;
    Ipv4Address bcast = ifa.GetLocal().GetSubnetDirectedBroadcast(ifa.GetMask());
    kv.first->SendTo(copy, 0, InetSocketAddress(bcast, kWirePort));
  }
}

void DVRoutingProtocol::ProcessCommand(std::vector<std::string> tokens)
{
  if (tokens.empty()) return;
  auto it = tokens.begin();
  const std::string cmd = *it;

  if (cmd == "PING") {
    if (tokens.size() < 3) { ERROR_LOG("PING: need <node> <msg>"); return; }
    uint32_t nodeId; { ++it; std::istringstream s(*it); s >> nodeId; }
    ++it; std::string payload = *it;
    Ipv4Address dst = ResolveNodeIpAddress(nodeId);
    if (dst == Ipv4Address::GetAny()) return;

    uint32_t seq = GetNextSequenceNumber();
    TRAFFIC_LOG("[DV/PING] to node " << nodeId << " ip " << dst << " msg '" << payload << "' seq " << seq);

    Ptr<PingRequest> pr = Create<PingRequest>(seq, Simulator::Now(), dst, payload);
    m_pingTracker.emplace(seq, pr);

    DVMessage msg(DVMessage::PING_REQ, seq, m_maxTTL, m_mainAddress);
    msg.SetPingReq(dst, payload);

    Ptr<Packet> p = Create<Packet>();
    p->AddHeader(msg);
    SendOnAllInterfaces(p);
  }
  else if (cmd == "DUMP") {
    if (tokens.size() < 2) { ERROR_LOG("DUMP: need ROUTES|NEIGHBORS"); return; }
    ++it; std::string table = *it;
    if (table == "ROUTES" || table == "ROUTING")      DumpRoutingTable();
    else if (table == "NEIGHBORS" || table == "NEIGHBOURS") DumpNeighbors();
  }
}

void DVRoutingProtocol::DumpNeighbors()
{
  STATUS_LOG(std::endl
    << "**************** Neighbor List ********************" << std::endl
    << "NeighborNumber\t\tNeighborAddr\t\tInterfaceAddr");

  std::vector<NeighborTableEntry> snap = m_neighbors.Snapshot();
  PRINT_LOG(snap.size());

  for (const auto &e : snap)
  {
    auto it = m_addressNodeMap.find(e.neighborAddress);
    bool haveId = (it != m_addressNodeMap.end());
    uint32_t nid = haveId ? it->second : 0;

    PRINT_LOG((haveId ? std::to_string(nid) : ReverseLookup(e.neighborAddress))
              << "\t\t\t" << e.neighborAddress << "\t\t" << e.interfaceAddress);

    if (haveId) {
      checkNeighborTableEntry(nid, e.neighborAddress, e.interfaceAddress);
    }
  }
  PRINT_LOG("");
}

void DVRoutingProtocol::DumpRoutingTable()
{
  STATUS_LOG(std::endl
    << "**************** Route Table ********************" << std::endl
    << "DestNumber\t\tDestAddr\t\tNextHopNumber\t\tNextHopAddr\t\tInterfaceAddr\t\tCost");

  PRINT_LOG(m_routes.size());

  for (const auto &kv : m_routes)
  {
    const DvRouteRow &r = kv.second;

    auto dIt = m_addressNodeMap.find(r.dest);
    auto nIt = m_addressNodeMap.find(r.nextHop);
    bool haveD = (dIt != m_addressNodeMap.end());
    bool haveN = (nIt != m_addressNodeMap.end());
    uint32_t did = haveD ? dIt->second : 0;
    uint32_t nid = haveN ? nIt->second : 0;

    PRINT_LOG((haveD ? std::to_string(did) : ReverseLookup(r.dest)) << "\t\t\t"
             << r.dest << "\t\t"
             << (haveN ? std::to_string(nid) : ReverseLookup(r.nextHop)) << "\t\t\t"
             << r.nextHop << "\t\t"
             << r.oif << "\t\t"
             << r.cost);

    if (haveD && haveN && r.cost <= m_maxHopCost) {
      checkRouteTableEntry(did, r.dest, nid, r.nextHop, r.oif, r.cost);
    }
  }
  PRINT_LOG("");
}

void DVRoutingProtocol::RecvDVMessage(Ptr<Socket> socket)
{
  Address from;
  Ptr<Packet> pkt = socket->RecvFrom(from);
  DVMessage msg;

  Ipv4PacketInfoTag info;
  if (!pkt->RemovePacketTag(info)) { NS_ABORT_MSG("No incoming interface on DV message"); }
  uint32_t incomingIf = info.GetRecvIf();

  if (!pkt->RemoveHeader(msg))   { NS_ABORT_MSG("DV header missing"); }

  Ipv4Address localIf;
  uint32_t idx = 1;
  for (auto it = m_socketAddresses.begin(); it != m_socketAddresses.end(); ++it, ++idx) {
    if (idx == incomingIf) { localIf = it->second.GetLocal(); break; }
  }

  switch (msg.GetMessageType()) {
    case DVMessage::PING_REQ:  ProcessPingReq(msg);                break;
    case DVMessage::PING_RSP:  ProcessPingRsp(msg);                break;
    case DVMessage::HELLO_REQ: ProcessHelloReq(msg);               break;
    case DVMessage::HELLO_RSP: ProcessHelloRsp(msg, localIf);      break;
    case DVMessage::DV_UPDATE: ProcessDvUpdate(msg, localIf);      break;
    default: ERROR_LOG("Unknown DV control type");                 break;
  }
}

/* ------------ HELLO handling (neighbor discovery) ------------ */

void DVRoutingProtocol::ProcessHelloReq(DVMessage msg)
{
  std::string from = ReverseLookup(msg.GetOriginatorAddress());
  TRAFFIC_LOG("[DV/HELLO] req from " << from << " msg=" << msg.GetHelloReq().helloMessage);

  DVMessage rsp(DVMessage::HELLO_RSP, msg.GetSequenceNumber(), 1, m_mainAddress);
  rsp.SetHelloRsp(msg.GetOriginatorAddress(), msg.GetHelloReq().helloMessage);

  Ptr<Packet> p = Create<Packet>(); p->AddHeader(rsp);
  SendOnAllInterfaces(p);
}

void DVRoutingProtocol::ProcessHelloRsp(DVMessage msg, Ipv4Address localIf)
{
  Ipv4Address neigh = msg.GetOriginatorAddress();
  m_neighbors.ObserveHello(neigh, localIf);

  std::string from = ReverseLookup(neigh);
  TRAFFIC_LOG("[DV/HELLO] rsp from " << from << " msg=" << msg.GetHelloRsp().helloMessage);
  DEBUG_LOG("[DV/HELLO] learned neighbor " << neigh << " on " << localIf);
}

/* --------------- PING handling  ---------------- */

void DVRoutingProtocol::ProcessPingReq(DVMessage msg)
{
  if (!IsOwnAddress(msg.GetPingReq().destinationAddress)) return;

  std::string from = ReverseLookup(msg.GetOriginatorAddress());
  TRAFFIC_LOG("[DV/PING] req from " << from << " msg=" << msg.GetPingReq().pingMessage);

  DVMessage rsp(DVMessage::PING_RSP, msg.GetSequenceNumber(), m_maxTTL, m_mainAddress);
  rsp.SetPingRsp(msg.GetOriginatorAddress(), msg.GetPingReq().pingMessage);

  Ptr<Packet> p = Create<Packet>(); p->AddHeader(rsp);
  SendOnAllInterfaces(p);
}

void DVRoutingProtocol::ProcessPingRsp(DVMessage msg)
{
  if (!IsOwnAddress(msg.GetPingRsp().destinationAddress)) return;

  auto it = m_pingTracker.find(msg.GetSequenceNumber());
  if (it != m_pingTracker.end()) {
    std::string from = ReverseLookup(msg.GetOriginatorAddress());
    TRAFFIC_LOG("[DV/PING] rsp from " << from << " msg=" << msg.GetPingRsp().pingMessage);
    m_pingTracker.erase(it);
  } else {
    DEBUG_LOG("[DV/PING] late/unknown response dropped");
  }
}

/* --------------- Periodic hello audit --------------- */

void DVRoutingProtocol::AuditHellos()
{
  DVMessage helloReq(DVMessage::HELLO_REQ, GetNextSequenceNumber(), 1, m_mainAddress);
  helloReq.SetHelloReq(m_helloString);

  Ptr<Packet> p = Create<Packet>(); p->AddHeader(helloReq);
  SendOnAllInterfaces(p);

  m_neighbors.Audit();
  DEBUG_LOG("[DV/HELLO] audit @ " << m_mainAddress << " neighbors=" << m_neighbors.Size());
}

/* --------------- Ping tracker audit --------------- */

void DVRoutingProtocol::AuditPings()
{
  for (auto it = m_pingTracker.begin(); it != m_pingTracker.end(); ) {
    Ptr<PingRequest> pr = it->second;
    if (pr->GetTimestamp().GetMilliSeconds() + m_pingTimeout.GetMilliSeconds() <= Simulator::Now().GetMilliSeconds()) {
      DEBUG_LOG("[DV/PING] expired msg='" << pr->GetPingMessage() << "'");
      it = m_pingTracker.erase(it);
    } else {
      ++it;
    }
  }
  m_auditPingsTimer.Schedule(m_pingTimeout);
}

uint32_t DVRoutingProtocol::GetNextSequenceNumber()
{
  m_currentSequenceNumber = (m_currentSequenceNumber + 1) % (kSeqMax + 1);
  return m_currentSequenceNumber;
}

/* ------------ ns-3 boilerplate ------------ */

void DVRoutingProtocol::NotifyInterfaceUp(uint32_t i)             { m_staticRouting->NotifyInterfaceUp(i); }
void DVRoutingProtocol::NotifyInterfaceDown(uint32_t i)           { m_staticRouting->NotifyInterfaceDown(i); }
void DVRoutingProtocol::NotifyAddAddress(uint32_t i, Ipv4InterfaceAddress a) { m_staticRouting->NotifyAddAddress(i, a); }
void DVRoutingProtocol::NotifyRemoveAddress(uint32_t i, Ipv4InterfaceAddress a) { m_staticRouting->NotifyRemoveAddress(i, a); }

void DVRoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4)
{
  NS_ASSERT(ipv4 != 0);
  NS_ASSERT(m_ipv4 == 0);
  NS_LOG_DEBUG("[DV/INIT] create");
  m_ipv4 = ipv4;
  m_staticRouting->SetIpv4(m_ipv4);
}

/* -------------------- DV advertisements -------------------- */

void DVRoutingProtocol::PushDvUpdate()
{
  CheckNeighborLoss();

  DEBUG_LOG("[DV/ADV] advertise");
  std::vector<DVMessage::DvVectorItem> vec;
  vec.push_back({ m_mainAddress, 0 });

  for (const auto &kv : m_routes) {
    const auto &e = kv.second;
    if (e.dest == m_mainAddress) continue;
    vec.push_back({ e.dest, std::min(m_maxHopCost, e.cost) });
  }

  DVMessage msg(DVMessage::DV_UPDATE, GetNextSequenceNumber(), m_maxTTL, m_mainAddress);
  msg.SetDvUpdate(vec);

  Ptr<Packet> p = Create<Packet>(); p->AddHeader(msg);
  SendOnAllInterfaces(p);

  m_periodicAdv.Cancel();
  m_periodicAdv.Schedule(m_periodicEvery);
}

void DVRoutingProtocol::ScheduleTriggeredPush()
{
  if (m_burstAdv.IsRunning()) return;
  m_burstAdv.Schedule(m_burstHold);
}

/* -------------------- DV logic -------------------- */

void DVRoutingProtocol::CheckNeighborLoss()
{
  for (auto &kv : m_routes) {
    if (!m_neighbors.Contains(kv.second.nextHop)) {
      kv.second.cost = m_dvInfinity;
      m_routeRemovedCb(kv.first);
    }
  }
}

uint32_t DVRoutingProtocol::UpdateRoute(Ipv4Address dst, Ipv4Address via, Ipv4Address viaIf, uint32_t viaCost)
{
  // inoperable neighbor path → invalidate our route if we depended on it
  if (viaCost >= m_dvInfinity) {
    auto it = m_routes.find(dst);
    if (it == m_routes.end()) return 0;
    if (it->second.nextHop == via) {
      it->second.cost = m_dvInfinity;
      m_routeRemovedCb(dst);
      return 1;
    }
    return 0;
  }

  auto it = m_routes.find(dst);
  if (it == m_routes.end()) {
    DvRouteRow row;
    row.dest = dst; row.nextHop = via; row.oif = viaIf;
    row.cost = viaCost + 1; row.timestamp = Simulator::Now();
    m_routes[dst] = row;
    m_routeUpdatedCb(dst, row);
    return 1;
  }

  DvRouteRow &cur = it->second;
  if (cur.cost >= m_dvInfinity || viaCost + 1 < cur.cost) {
    cur.nextHop = via; cur.oif = viaIf; cur.cost = viaCost + 1; cur.timestamp = Simulator::Now();
    m_routeUpdatedCb(dst, cur);
    return 1;
  } else if (cur.nextHop == via) {
    cur.cost = viaCost + 1;
    m_routeUpdatedCb(dst, cur);
    return 1;
  }
  return 0;
}

void DVRoutingProtocol::ProcessDvUpdate(DVMessage msg, Ipv4Address localIf)
{
  const Ipv4Address neigh = msg.GetOriginatorAddress();
  DEBUG_LOG("[DV/ADV] recv from " << neigh);

  bool changed = false;
  for (const auto &item : msg.GetDvUpdate().vec) {
    if (item.dest == m_mainAddress) continue;
    changed = (UpdateRoute(item.dest, neigh, localIf, item.cost) == 1) || changed;
  }
  if (changed) {
    ScheduleTriggeredPush();
    DEBUG_LOG("[DV/ADV] topology change → trigger");
  }
}

std::vector<DVRoutingProtocol::DvRouteRow> DVRoutingProtocol::Snapshot() const
{
  std::vector<DvRouteRow> v;
  v.reserve(m_routes.size());
  for (const auto &kv : m_routes) {
    if (kv.second.cost <= m_maxHopCost) v.push_back(kv.second);
  }
  return v;
}

bool DVRoutingProtocol::IsOwnAddress(Ipv4Address ip)
{
  for (const auto &kv : m_socketAddresses) {
    if (kv.second.GetLocal() == ip) return true;
  }
  return false;
}

void DVRoutingProtocol::RunMaintenance()
{
  AuditHellos();
  CheckNeighborLoss();
  ScheduleTriggeredPush();
  m_maintenance.Schedule(m_maintenanceEvery);
}
