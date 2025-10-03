/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/ls-routing-protocol.h"

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

#include <queue>
#include <limits>
#include <set>
#include <sstream>
#include <cctype>
#include <algorithm>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LSRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(LSRoutingProtocol);

#define LS_MAX_SEQUENCE_NUMBER 0xFFFF
#define LS_PORT_NUMBER 698

// ---------------------------------------------------------------------------
// TypeId / attributes
// ---------------------------------------------------------------------------
TypeId
LSRoutingProtocol::GetTypeId(void)
{
  static TypeId tid = TypeId("LSRoutingProtocol")
    .SetParent<PennRoutingProtocol>()
    .AddConstructor<LSRoutingProtocol>()
    .AddAttribute("LSPort", "Listening port for LS packets", UintegerValue(5000),
                  MakeUintegerAccessor(&LSRoutingProtocol::m_lsPort), MakeUintegerChecker<uint16_t>())
    .AddAttribute("PingTimeout", "Timeout value for PING_REQ in milliseconds", TimeValue(MilliSeconds(2000)),
                  MakeTimeAccessor(&LSRoutingProtocol::m_pingTimeout), MakeTimeChecker())
    .AddAttribute("MaxTTL", "Maximum TTL value for LS packets", UintegerValue(16),
                  MakeUintegerAccessor(&LSRoutingProtocol::m_maxTTL), MakeUintegerChecker<uint8_t>());
  return tid;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
LSRoutingProtocol::LSRoutingProtocol()
  : m_recvSocket(nullptr),
    m_staticRouting(Create<Ipv4StaticRouting>()),
    m_ipv4(nullptr),
    m_updateInterval(Seconds(5.0)),
    m_currentSequenceNumber(0),
    m_auditPingsTimer(Timer::CANCEL_ON_DESTROY),
    m_updateTimer(Timer::CANCEL_ON_DESTROY)
{
}

LSRoutingProtocol::~LSRoutingProtocol() {}

void
LSRoutingProtocol::DoDispose()
{
  if (m_recvSocket)
  {
    m_recvSocket->Close();
    m_recvSocket = 0;
  }

  for (auto it = m_socketAddresses.begin(); it != m_socketAddresses.end(); ++it)
  {
    it->first->Close();
  }
  m_socketAddresses.clear();

  m_staticRouting = 0;

  m_auditPingsTimer.Cancel();
  m_updateTimer.Cancel();

  m_pingTracker.clear();
  m_neighbors.clear();
  m_linkStateDatabase.clear();
  m_routingTable.clear();

  PennRoutingProtocol::DoDispose();
}

void
LSRoutingProtocol::SetMainInterface(uint32_t mainInterface)
{
  m_mainAddress = m_ipv4->GetAddress(mainInterface, 0).GetLocal();
}

void
LSRoutingProtocol::SetNodeAddressMap(std::map<uint32_t, Ipv4Address> nodeAddressMap)
{
  m_nodeAddressMap = nodeAddressMap;
}

void
LSRoutingProtocol::SetAddressNodeMap(std::map<Ipv4Address, uint32_t> addressNodeMap)
{
  m_addressNodeMap = addressNodeMap;
}

Ipv4Address
LSRoutingProtocol::ResolveNodeIpAddress(uint32_t nodeNumber)
{
  auto it = m_nodeAddressMap.find(nodeNumber);
  if (it != m_nodeAddressMap.end())
  {
    return it->second;
  }
  return Ipv4Address::GetAny();
}

std::string
LSRoutingProtocol::ReverseLookup(Ipv4Address ipAddress)
{
  auto it = m_addressNodeMap.find(ipAddress);
  if (it != m_addressNodeMap.end())
  {
    std::ostringstream ss;
    ss << it->second;
    return ss.str();
  }
  return std::string("Unknown");
}

void
LSRoutingProtocol::DoInitialize(void)
{
  if (m_mainAddress == Ipv4Address())
  {
    Ipv4Address loopback("127.0.0.1");
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++)
    {
      Ipv4Address addr = m_ipv4->GetAddress(i, 0).GetLocal();
      if (addr != loopback)
      {
        m_mainAddress = addr;
        break;
      }
    }
    NS_ASSERT(m_mainAddress != Ipv4Address());
  }

  NS_LOG_DEBUG("[LS/BOOT] start on " << m_mainAddress);

  bool canRun = false;

  // Create sockets across interfaces
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++)
  {
    Ipv4Address ipAddress = m_ipv4->GetAddress(i, 0).GetLocal();
    if (ipAddress == Ipv4Address::GetLoopback())
      continue;

    if (m_recvSocket == 0)
    {
      m_recvSocket = Socket::CreateSocket(GetObject<Node>(), UdpSocketFactory::GetTypeId());
      m_recvSocket->SetAllowBroadcast(true);
      InetSocketAddress inetAddr(Ipv4Address::GetAny(), LS_PORT_NUMBER);
      m_recvSocket->SetRecvCallback(MakeCallback(&LSRoutingProtocol::RecvLSMessage, this));
      if (m_recvSocket->Bind(inetAddr))
      {
        NS_FATAL_ERROR("Failed to bind() LS socket");
      }
      m_recvSocket->SetRecvPktInfo(true);
      m_recvSocket->ShutdownSend();
    }

    Ptr<Socket> socket = Socket::CreateSocket(GetObject<Node>(), UdpSocketFactory::GetTypeId());
    socket->SetAllowBroadcast(true);
    InetSocketAddress inetAddr(m_ipv4->GetAddress(i, 0).GetLocal(), m_lsPort);
    socket->SetRecvCallback(MakeCallback(&LSRoutingProtocol::RecvLSMessage, this));
    if (socket->Bind(inetAddr))
    {
      NS_FATAL_ERROR("LSRoutingProtocol::DoInitialize::Failed to bind socket!");
    }
    socket->BindToNetDevice(m_ipv4->GetNetDevice(i));
    m_socketAddresses[socket] = m_ipv4->GetAddress(i, 0);
    canRun = true;
  }

  if (canRun)
  {
    m_auditPingsTimer.Schedule(m_pingTimeout);
    m_updateTimer.Schedule(m_updateInterval);
    NS_LOG_DEBUG("[LS/BOOT] running on " << m_mainAddress);
  }
}

// ---------------------------------------------------------------------------
// Printing
// ---------------------------------------------------------------------------
void
LSRoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper> /*stream*/, Time::Unit /*unit*/) const
{
  // Not required by autograder.
}

// ---------------------------------------------------------------------------
// Ipv4RoutingProtocol
// ---------------------------------------------------------------------------
Ptr<Ipv4Route>
LSRoutingProtocol::RouteOutput(Ptr<Packet> packet, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr)
{
  Ptr<Ipv4Route> r = m_staticRouting->RouteOutput(packet, header, oif, sockerr);
  if (r)
  {
    DEBUG_LOG("[LS/ROUTE] to " << r->GetDestination()
              << " via " << r->GetGateway()
              << " src " << r->GetSource()
              << " out " << r->GetOutputDevice());
  }
  else
  {
    DEBUG_LOG("[LS/ROUTE] none to " << header.GetDestination());
  }
  return r;
}

bool
LSRoutingProtocol::RouteInput(Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                              UnicastForwardCallback ucb, MulticastForwardCallback mcb, LocalDeliverCallback lcb,
                              ErrorCallback ecb)
{
  Ipv4Address dst = header.GetDestination();
  Ipv4Address src = header.GetSource();

  if (IsOwnAddress(src))
  {
    return true;
  }

  uint32_t interfaceNum = m_ipv4->GetInterfaceForDevice(idev);
  if (m_ipv4->IsDestinationAddress(dst, interfaceNum))
  {
    if (!lcb.IsNull())
    {
      lcb(p, header, interfaceNum);
      return true;
    }
    return false;
  }

  if (m_staticRouting->RouteInput(p, header, idev, ucb, mcb, lcb, ecb))
  {
    return true;
  }

  DEBUG_LOG("[LS/ROUTE] drop: no route to " << dst);
  return false;
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static inline std::string
ToLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c){ return std::tolower(c); });
  return s;
}

// Split tokens but preserve a quoted last argument for messages with spaces.
// Example: PING 7 "hello world"
static std::vector<std::string>
NormalizeAndSplit(const std::vector<std::string>& tokens)
{
  if (tokens.empty()) return tokens;
  // If the third token starts with a quote or there are >3 tokens, join [2..end)
  if (tokens.size() >= 3)
  {
    std::string joined;
    for (size_t i = 2; i < tokens.size(); ++i)
    {
      if (i > 2) joined.push_back(' ');
      joined += tokens[i];
    }
    // Trim optional surrounding quotes
    if (joined.size() >= 2 &&
        ((joined.front() == '"'  && joined.back() == '"') ||
         (joined.front() == '\'' && joined.back() == '\'')))
    {
      joined = joined.substr(1, joined.size() - 2);
    }
    return {tokens[0], tokens[1], joined};
  }
  return tokens;
}

// ---------------------------------------------------------------------------
// CLI command processing
// ---------------------------------------------------------------------------
void
LSRoutingProtocol::ProcessCommand(std::vector<std::string> tokens)
{
  if (tokens.empty()) return;

  // Normalize case of the command token; preserve others.
  tokens[0] = ToLower(tokens[0]);

  // Allow quoted message content and collapsing extra args
  tokens = NormalizeAndSplit(tokens);

  auto handlePing = [&](const std::vector<std::string>& args)
  {
    if (args.size() < 3)
    {
      ERROR_LOG("[LS/CLI] ping: need nodeId and message");
      return;
    }
    uint32_t nodeNumber = 0;
    {
      std::istringstream sin(args[1]);
      sin >> nodeNumber;
    }
    const std::string pingMessage = args[2];

    Ipv4Address dest = ResolveNodeIpAddress(nodeNumber);
    if (dest == Ipv4Address::GetAny())
    {
      ERROR_LOG("[LS/CLI] ping: unknown node " << nodeNumber);
      return;
    }

    uint32_t seq = GetNextSequenceNumber();
    TRAFFIC_LOG("[LS/PING] send req -> N:" << nodeNumber
                << " ip=" << dest
                << " msg=\"" << pingMessage << "\""
                << " seq=" << seq);

    Ptr<PingRequest> pr = Create<PingRequest>(seq, Simulator::Now(), dest, pingMessage);
    m_pingTracker.insert(std::make_pair(seq, pr));

    Ptr<Packet> pkt = Create<Packet>();
    LSMessage msg(LSMessage::PING_REQ, seq, m_maxTTL, m_mainAddress);
    msg.SetPingReq(dest, pingMessage);
    pkt->AddHeader(msg);
    BroadcastPacket(pkt);
  };

  auto handleDump = [&](const std::vector<std::string>& args)
  {
    if (args.size() < 2)
    {
      ERROR_LOG("[LS/CLI] dump: need ROUTES|NEIGHBORS|LSA");
      return;
    }
    const std::string what = ToLower(args[1]);
    if (what == "routes" || what == "routing")
    {
      DumpRoutingTable();
    }
    else if (what == "neighbors" || what == "neighborS") // tolerant
    {
      DumpNeighbors();
    }
    else if (what == "lsa")
    {
      DumpLSA();
    }
    else
    {
      ERROR_LOG("[LS/CLI] dump: unknown '" << args[1] << "'");
    }
  };

  const std::string cmd = tokens[0];
  if (cmd == "ping")
  {
    handlePing(tokens);
  }
  else if (cmd == "dump")
  {
    handleDump(tokens);
  }
  // silently ignore unknown commands
}

// ---------------------------------------------------------------------------
// Debug dumps (autograder calls inside) — prints raw node IDs
// ---------------------------------------------------------------------------
void
LSRoutingProtocol::DumpLSA()
{
  STATUS_LOG(std::endl
    << "**************** LSA DUMP ********************" << std::endl
    << "Node\t\tNeighbor(s)");

  PRINT_LOG(m_linkStateDatabase.size());
  for (const auto &kv : m_linkStateDatabase)
  {
    uint32_t originatorId = kv.first;
    const LsaRecord &lsa = kv.second;
    std::string linksString;
    for (const auto &edge : lsa.links)
    {
      // Print pure node IDs
      linksString += std::to_string(edge.first) + "(" + std::to_string(edge.second) + ") ";
    }
    checkLinkStateEntry(originatorId, lsa.sequenceNumber, linksString);
    PRINT_LOG("Node: " << originatorId << " Seq: " << lsa.sequenceNumber << " Links: " << linksString);
  }
}

void
LSRoutingProtocol::DumpNeighbors()
{
  STATUS_LOG(std::endl
    << "**************** Neighbor List ********************" << std::endl
    << "NeighborNumber\t\tNeighborAddr\t\tInterfaceAddr");

  PRINT_LOG(m_neighbors.size());
  for (const auto &kv : m_neighbors)
  {
    uint32_t nid = kv.first;
    const AdjacencyRecord &rec = kv.second;
    checkNeighborTableEntry(nid, rec.address, rec.interface);
    PRINT_LOG(nid << '\t' << rec.address << '\t' << rec.interface);
  }
}

void
LSRoutingProtocol::DumpRoutingTable()
{
  STATUS_LOG(std::endl
    << "**************** Route Table ********************" << std::endl
    << "DestNumber\t\tDestAddr\t\tNextHopNumber\t\tNextHopAddr\t\tInterfaceAddr\t\tCost");

  PRINT_LOG(m_routingTable.size());
  for (const auto &kv : m_routingTable)
  {
    uint32_t destId = kv.first;
    const RouteRow &row = kv.second;
    checkRouteTableEntry(destId, row.destinationAddr, row.nextHopNodeId, row.nextHopAddr, row.outgoingInterface, row.pathCost);
    PRINT_LOG(destId << '\t' << row.destinationAddr << '\t' << row.nextHopNodeId
             << '\t' << row.nextHopAddr << '\t' << row.outgoingInterface << '\t' << row.pathCost);
  }
}

// ---------------------------------------------------------------------------
// Receive / process messages
// ---------------------------------------------------------------------------
void
LSRoutingProtocol::RecvLSMessage(Ptr<Socket> socket)
{
  Address src;
  Ptr<Packet> pkt = socket->RecvFrom(src);

  LSMessage lsMessage;
  Ipv4PacketInfoTag pktInfo;
  if (!pkt->RemovePacketTag(pktInfo))
  {
    NS_ABORT_MSG("Missing interface tag on LS message.");
  }
  uint32_t incomingIf = pktInfo.GetRecvIf();

  if (!pkt->RemoveHeader(lsMessage))
  {
    NS_ABORT_MSG("Failed to remove LS header.");
  }

  Ipv4Address incomingInterface;
  {
    // Map interface index to its Ipv4InterfaceAddress::GetLocal()
    uint32_t idx = 1;
    for (auto it = m_socketAddresses.begin(); it != m_socketAddresses.end(); ++it)
    {
      if (idx == incomingIf)
      {
        incomingInterface = it->second.GetLocal();
        break;
      }
      ++idx;
    }
  }

  switch (lsMessage.GetMessageType())
  {
    case LSMessage::PING_REQ:  ProcessPingReq(lsMessage); break;
    case LSMessage::PING_RSP:  ProcessPingRsp(lsMessage); break;
    case LSMessage::HELLO_REQ:
    case LSMessage::HELLO_RSP: ProcessHello(lsMessage, incomingInterface); break;
    case LSMessage::LSA_m:     ProcessLSP(lsMessage, incomingInterface); break;
    default:
      ERROR_LOG("[LS/RX] unknown message type");
      break;
  }
}

void
LSRoutingProtocol::ProcessPingReq(LSMessage msg)
{
  if (!IsOwnAddress(msg.GetPingReq().destinationAddress)) return;

  std::string fromNode = ReverseLookup(msg.GetOriginatorAddress());
  TRAFFIC_LOG("[LS/PING] RX req from N:" << fromNode << " msg=\"" << msg.GetPingReq().pingMessage << "\"");
  LSMessage rsp(LSMessage::PING_RSP, msg.GetSequenceNumber(), m_maxTTL, m_mainAddress);
  rsp.SetPingRsp(msg.GetOriginatorAddress(), msg.GetPingReq().pingMessage);
  Ptr<Packet> pkt = Create<Packet>();
  pkt->AddHeader(rsp);
  BroadcastPacket(pkt);
}

void
LSRoutingProtocol::ProcessPingRsp(LSMessage msg)
{
  if (!IsOwnAddress(msg.GetPingRsp().destinationAddress)) return;

  auto it = m_pingTracker.find(msg.GetSequenceNumber());
  if (it != m_pingTracker.end())
  {
    std::string fromNode = ReverseLookup(msg.GetOriginatorAddress());
    TRAFFIC_LOG("[LS/PING] RX rsp from N:" << fromNode << " msg=\"" << msg.GetPingRsp().pingMessage << "\"");
    m_pingTracker.erase(it);
  }
  else
  {
    DEBUG_LOG("[LS/PING] stale/unknown response seq=" << msg.GetSequenceNumber());
  }
}

void
LSRoutingProtocol::ProcessHello(LSMessage msg, Ipv4Address incomingInterface)
{
  if (msg.GetMessageType() == LSMessage::HELLO_REQ)
  {
    LSMessage helloRsp(LSMessage::HELLO_RSP, GetNextSequenceNumber(), 1, m_mainAddress);
    helloRsp.SetHelloRsp(msg.GetOriginatorAddress(), "hello");
    Ptr<Packet> pkt = Create<Packet>();
    pkt->AddHeader(helloRsp);
    BroadcastPacket(pkt);
    return;
  }

  // HELLO_RSP
  if (IsOwnAddress(msg.GetHelloRsp().destinationAddress))
  {
    uint32_t neighborId = std::stoul(ReverseLookup(msg.GetOriginatorAddress()));
    AdjacencyRecord rec;
    rec.address    = msg.GetOriginatorAddress();
    rec.interface  = incomingInterface;
    rec.lastHeard  = Simulator::Now();
    rec.cost       = 1;
    m_neighbors[neighborId] = rec;
  }
}

void
LSRoutingProtocol::ProcessLSP(LSMessage msg, Ipv4Address /*incomingInterface*/)
{
  if (msg.GetTTL() == 0) return;

  uint32_t originatorId = std::stoul(ReverseLookup(msg.GetOriginatorAddress()));
  uint32_t seq = msg.GetSequenceNumber();

  auto it = m_linkStateDatabase.find(originatorId);
  const bool isNew   = (it == m_linkStateDatabase.end());
  const bool isNewer = (!isNew && it->second.sequenceNumber < seq);

  if (isNew || isNewer)
  {
    LsaRecord rec;
    rec.sequenceNumber = seq;
    rec.links = msg.GetLsa().linkVector;
    m_linkStateDatabase[originatorId] = rec;

    ComputeShortestPaths();

    Ptr<Packet> pkt = Create<Packet>();
    msg.SetTTL(msg.GetTTL() - 1);
    pkt->AddHeader(msg);
    BroadcastPacket(pkt);
  }
}

// ---------------------------------------------------------------------------
// Periodic maintenance — self-install our LSA and run SPF
// ---------------------------------------------------------------------------
void
LSRoutingProtocol::UpdateNetworkState()
{
  const Time now = Simulator::Now();

  // 1) Decay neighbors with a small grace to avoid timer edge flaps
  const Time keepAlive = m_updateInterval + MilliSeconds(250);
  std::map<uint32_t, AdjacencyRecord> pruned;
  for (const auto &kv : m_neighbors)
  {
    if (kv.second.lastHeard + keepAlive > now)
    {
      pruned.insert(kv);
    }
  }
  m_neighbors.swap(pruned);

  // 2) Send HELLO (TTL=1)
  {
    LSMessage hello(LSMessage::HELLO_REQ, GetNextSequenceNumber(), 1, m_mainAddress);
    hello.SetHelloReq(Ipv4Address::GetAny(), "hello");
    Ptr<Packet> pkt = Create<Packet>();
    pkt->AddHeader(hello);
    BroadcastPacket(pkt);
  }

  // 3) Build local edges from current neighbors (unit costs)
  const uint32_t lsaSeq = GetNextSequenceNumber();
  std::vector<std::pair<uint32_t, uint32_t>> edges;
  edges.reserve(m_neighbors.size());
  for (const auto &kv : m_neighbors)
  {
    edges.emplace_back(kv.first, kv.second.cost);
  }

  // 4) Insert/refresh OUR OWN LSA in LSDB and run SPF immediately
  const uint32_t myId = std::stoul(ReverseLookup(m_mainAddress));
  {
    LsaRecord self;
    self.sequenceNumber = lsaSeq;
    self.links = edges;
    m_linkStateDatabase[myId] = self;
  }
  ComputeShortestPaths();

  // 5) Broadcast the LSA to neighbors
  {
    LSMessage lsa(LSMessage::LSA_m, lsaSeq, m_maxTTL, m_mainAddress);
    lsa.SetLsa(edges);
    Ptr<Packet> pkt = Create<Packet>();
    pkt->AddHeader(lsa);
    BroadcastPacket(pkt);
  }

  // 6) Reschedule
  m_updateTimer.Schedule(m_updateInterval);
}

void
LSRoutingProtocol::ComputeShortestPaths()
{
  // Dijkstra with priority_queue. First hop tracked separately.
  m_routingTable.clear();

  struct NodePath {
    uint32_t node;
    uint32_t cost;
    uint32_t firstHop;
    bool operator>(const NodePath &o) const { return cost > o.cost; }
  };

  std::priority_queue<NodePath, std::vector<NodePath>, std::greater<NodePath>> pq;
  std::map<uint32_t, uint32_t> dist;
  std::map<uint32_t, uint32_t> first;

  uint32_t me = std::stoul(ReverseLookup(m_mainAddress));
  dist[me]  = 0u;
  first[me] = me;
  pq.push({me, 0u, me});

  while (!pq.empty())
  {
    NodePath cur = pq.top(); pq.pop();
    auto dIt = dist.find(cur.node);
    if (dIt == dist.end() || cur.cost != dIt->second) continue;

    auto lsaIt = m_linkStateDatabase.find(cur.node);
    if (lsaIt == m_linkStateDatabase.end()) continue;

    const auto &links = lsaIt->second.links;
    for (const auto &e : links)
    {
      const uint32_t nb = e.first;
      const uint32_t w  = e.second;
      const uint32_t cand = cur.cost + w;

      auto old = dist.find(nb);
      if (old == dist.end() || cand < old->second)
      {
        dist[nb]   = cand;
        first[nb]  = (cur.node == me ? nb : cur.firstHop);
        pq.push({nb, cand, first[nb]});
      }
    }
  }

  for (const auto &kv : dist)
  {
    uint32_t dst = kv.first;
    if (dst == me) continue;

    RouteRow row;
    row.destinationAddr = ResolveNodeIpAddress(dst);
    row.pathCost        = kv.second;
    row.nextHopNodeId   = first[dst];
    row.nextHopAddr     = ResolveNodeIpAddress(row.nextHopNodeId);

    auto nbr = m_neighbors.find(row.nextHopNodeId);
    row.outgoingInterface = (nbr != m_neighbors.end()) ? nbr->second.interface : Ipv4Address();

    m_routingTable[dst] = row;
  }
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------
void
LSRoutingProtocol::BroadcastPacket(Ptr<Packet> packet)
{
  for (const auto &kv : m_socketAddresses)
  {
    Ptr<Packet> copy = packet->Copy();
    Ipv4Address bcast = kv.second.GetLocal().GetSubnetDirectedBroadcast(kv.second.GetMask());
    kv.first->SendTo(copy, 0, InetSocketAddress(bcast, LS_PORT_NUMBER));
  }
}

bool
LSRoutingProtocol::IsOwnAddress(Ipv4Address addr)
{
  for (const auto &kv : m_socketAddresses)
  {
    if (addr == kv.second.GetLocal()) return true;
  }
  return false;
}

void
LSRoutingProtocol::AuditPings()
{
  for (auto it = m_pingTracker.begin(); it != m_pingTracker.end(); )
  {
    Ptr<PingRequest> pr = it->second;
    const auto ts  = pr->GetTimestamp().GetMilliSeconds();
    const auto now = Simulator::Now().GetMilliSeconds();
    const auto ttl = m_pingTimeout.GetMilliSeconds();

    if (ts + ttl <= now)
    {
      DEBUG_LOG("[LS/PING] expire seq=" << it->first
                << " age_ms=" << (now - ts)
                << " dest=" << pr->GetDestinationAddress()
                << " msg=\"" << pr->GetPingMessage() << "\"");
      it = m_pingTracker.erase(it);
    }
    else
    {
      ++it;
    }
  }
  m_auditPingsTimer.Schedule(m_pingTimeout);
}

uint32_t
LSRoutingProtocol::GetNextSequenceNumber()
{
  m_currentSequenceNumber = (m_currentSequenceNumber + 1) % (LS_MAX_SEQUENCE_NUMBER + 1);
  return m_currentSequenceNumber;
}

void LSRoutingProtocol::NotifyInterfaceUp(uint32_t i)    { m_staticRouting->NotifyInterfaceUp(i); }
void LSRoutingProtocol::NotifyInterfaceDown(uint32_t i)  { m_staticRouting->NotifyInterfaceDown(i); }
void LSRoutingProtocol::NotifyAddAddress(uint32_t i, Ipv4InterfaceAddress a) { m_staticRouting->NotifyAddAddress(i, a); }
void LSRoutingProtocol::NotifyRemoveAddress(uint32_t i, Ipv4InterfaceAddress a) { m_staticRouting->NotifyRemoveAddress(i, a); }

void
LSRoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4)
{
  NS_ASSERT(ipv4 != 0);
  NS_ASSERT(m_ipv4 == 0);
  NS_LOG_DEBUG("[LS/BOOT] Created ls::RoutingProtocol");
  m_auditPingsTimer.SetFunction(&LSRoutingProtocol::AuditPings, this);
  m_updateTimer.SetFunction(&LSRoutingProtocol::UpdateNetworkState, this);
  m_ipv4 = ipv4;
  m_staticRouting->SetIpv4(m_ipv4);
}

void LSRoutingProtocol::checkLinkStateEntry(uint32_t, uint32_t, std::string) {}
