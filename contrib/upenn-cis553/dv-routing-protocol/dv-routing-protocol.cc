/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

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
#include <ctime>

#include "ns3/neighbor-table.h"
#include "ns3/neighbor-timers.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("DVRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(DVRoutingProtocol);

#define DV_MAX_SEQUENCE_NUMBER 0xFFFF
#define DV_PORT_NUMBER 698
#define ROUTE_NOT_UPDATED 0
#define ROUTE_UPDATED 1
#define INVALIDATED_ROUTE 1000

TypeId
DVRoutingProtocol::GetTypeId(void)
{
  static TypeId tid = TypeId("DVRoutingProtocol")
                          .SetParent<PennRoutingProtocol>()
                          .AddConstructor<DVRoutingProtocol>()
                          .AddAttribute("DVPort",
                                        "Listening port for DV packets",
                                        UintegerValue(5000),
                                        MakeUintegerAccessor(&DVRoutingProtocol::m_dvPort),
                                        MakeUintegerChecker<uint16_t>())
                          .AddAttribute("PingTimeout",
                                        "Timeout value for PING_REQ in milliseconds",
                                        TimeValue(MilliSeconds(2000)),
                                        MakeTimeAccessor(&DVRoutingProtocol::m_pingTimeout),
                                        MakeTimeChecker())
                          .AddAttribute("MaxTTL",
                                        "Maximum TTL value for DV packets",
                                        UintegerValue(16),
                                        MakeUintegerAccessor(&DVRoutingProtocol::m_maxTTL),
                                        MakeUintegerChecker<uint8_t>());
  return tid;
}

DVRoutingProtocol::DVRoutingProtocol()
    : m_auditPingsTimer(Timer::CANCEL_ON_DESTROY),
      m_periodicUpdateTimer(Timer::CANCEL_ON_DESTROY),   // MS2 related: periodic DV advertisements driver
      m_triggeredUpdateTimer(Timer::CANCEL_ON_DESTROY)   // MS2 related: coalesces near-term triggered updates
{

  m_currentSequenceNumber = 0;
  // Setup static routing
  m_staticRouting = Create<Ipv4StaticRouting>();
}

DVRoutingProtocol::~DVRoutingProtocol()
{
}

void DVRoutingProtocol::DoDispose()
{
  if (m_recvSocket)
  {
    m_recvSocket->Close();
    m_recvSocket = 0;
  }

  // Close sockets
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::iterator iter = m_socketAddresses.begin();
       iter != m_socketAddresses.end(); iter++)
  {
    iter->first->Close();
  }
  m_socketAddresses.clear();

  // Clear static routing
  m_staticRouting = 0;

  // Cancel timers
  m_auditPingsTimer.Cancel();
  m_periodicUpdateTimer.Cancel();    // MS2 related: stop periodic DV updates
  m_triggeredUpdateTimer.Cancel();   // MS2 related: stop triggered updates
  m_pingTracker.clear();

  PennRoutingProtocol::DoDispose();
}

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

Ipv4Address
DVRoutingProtocol::ResolveNodeIpAddress(uint32_t nodeNumber)
{
  std::map<uint32_t, Ipv4Address>::iterator iter = m_nodeAddressMap.find(nodeNumber);
  if (iter != m_nodeAddressMap.end())
  {
    return iter->second;
  }
  return Ipv4Address::GetAny();
}

std::string
DVRoutingProtocol::ReverseLookup(Ipv4Address ipAddress)
{
  std::map<Ipv4Address, uint32_t>::iterator iter = m_addressNodeMap.find(ipAddress);
  if (iter != m_addressNodeMap.end())
  {
    std::ostringstream sin;
    uint32_t nodeNumber = iter->second;
    sin << nodeNumber;
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
      // Use primary address, if multiple
      Ipv4Address addr = m_ipv4->GetAddress(i, 0).GetLocal();
      if (addr != loopback)
      {
        m_mainAddress = addr;
        break;
      }
    }

    NS_ASSERT(m_mainAddress != Ipv4Address());
  }

  NS_LOG_DEBUG("Starting DV on node " << m_mainAddress);

  bool canRunDV = false;
  // Create sockets
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++)
  {
    Ipv4Address ipAddress = m_ipv4->GetAddress(i, 0).GetLocal();
    if (ipAddress == Ipv4Address::GetLoopback())
      continue;

    // Create a socket to listen on all the interfaces
    if (m_recvSocket == 0)
    {
      m_recvSocket = Socket::CreateSocket(GetObject<Node>(), UdpSocketFactory::GetTypeId());
      m_recvSocket->SetAllowBroadcast(true);
      InetSocketAddress inetAddr(Ipv4Address::GetAny(), DV_PORT_NUMBER);
      m_recvSocket->SetRecvCallback(MakeCallback(&DVRoutingProtocol::RecvDVMessage, this));
      if (m_recvSocket->Bind(inetAddr))
      {
        NS_FATAL_ERROR("Failed to bind() LS socket");
      }
      m_recvSocket->SetRecvPktInfo(true);
      m_recvSocket->ShutdownSend();
    }

    // Create socket on this interface
    Ptr<Socket> socket = Socket::CreateSocket(GetObject<Node>(), UdpSocketFactory::GetTypeId());
    socket->SetAllowBroadcast(true);
    InetSocketAddress inetAddr(m_ipv4->GetAddress(i, 0).GetLocal(), m_dvPort);
    socket->SetRecvCallback(MakeCallback(&DVRoutingProtocol::RecvDVMessage, this));
    if (socket->Bind(inetAddr))
    {
      NS_FATAL_ERROR("DVRoutingProtocol::DoInitialize::Failed to bind socket!");
    }
    socket->BindToNetDevice(m_ipv4->GetNetDevice(i));
    m_socketAddresses[socket] = m_ipv4->GetAddress(i, 0);
    canRunDV = true;
  }

  if (canRunDV)
  {
    // Configure and start NeighborTimers
    m_neighborTimers = CreateObject<NeighborTimers>();
    m_neighborTimers->Configure(Seconds(1.0), Seconds(1.0));
    //m_neighborTimers->SetHelloCallback(MakeCallback(&DVRoutingProtocol::SendHellos, this));
    m_neighborTimers->SetAuditCallback(MakeCallback(&DVRoutingProtocol::AuditHellos, this));
    m_neighborTimers->Start();

    // ---------------- MS2 related (Part 1) ----------------
    // Periodic DV advertisements: bind the sender and schedule first fire.
    m_periodicUpdateTimer.SetFunction(&DVRoutingProtocol::SendPeriodicUpdate, this);  // MS2 related: hook timer -> sender
    m_periodicUpdateTimer.Schedule(m_periodicInterval);                                // MS2 related: start periodic DV_UPDATE cadence

    // Triggered updates: reuse same sender; fire only when TriggerUpdateSoon() schedules it.
    m_triggeredUpdateTimer.SetFunction(&DVRoutingProtocol::SendPeriodicUpdate, this); // MS2 related: coalesced on-change DV_UPDATE

    AuditPings();
    NS_LOG_DEBUG("Starting DV on node " << m_mainAddress);
  }
}

void DVRoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
  // You can ignore this function
}

Ptr<Ipv4Route>
DVRoutingProtocol::RouteOutput(Ptr<Packet> packet, const Ipv4Header &header, Ptr<NetDevice> outInterface, Socket::SocketErrno &sockerr)
{
  Ptr<Ipv4Route> ipv4Route = m_staticRouting->RouteOutput(packet, header, outInterface, sockerr);
  if (ipv4Route)
  {
    DEBUG_LOG("Found route to: " << ipv4Route->GetDestination() << " via next-hop: " << ipv4Route->GetGateway() << " with source: " << ipv4Route->GetSource() << " and output device " << ipv4Route->GetOutputDevice());
  }
  else
  {
    DEBUG_LOG("No Route to destination: " << header.GetDestination());
  }
  return ipv4Route;
}

bool DVRoutingProtocol::RouteInput(Ptr<const Packet> packet,
                                   const Ipv4Header &header, Ptr<const NetDevice> inputDev,
                                   UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                                   LocalDeliverCallback lcb, ErrorCallback ecb)
{
  Ipv4Address destinationAddress = header.GetDestination();
  Ipv4Address sourceAddress = header.GetSource();

  // Drop if packet was originated by this node
  if (IsOwnAddress(sourceAddress) == true)
  {
    return true;
  }

  // Check for local delivery
  uint32_t interfaceNum = m_ipv4->GetInterfaceForDevice(inputDev);
  if (m_ipv4->IsDestinationAddress(destinationAddress, interfaceNum))
  {
    if (!lcb.IsNull())
    {
      lcb(packet, header, interfaceNum);
      return true;
    }
    else
    {
      return false;
    }
  }

  // Check static routing table
  if (m_staticRouting->RouteInput(packet, header, inputDev, ucb, mcb, lcb, ecb))
  {
    return true;
  }
  DEBUG_LOG("Cannot forward packet. No Route to destination: " << header.GetDestination());
  return false;
}

void DVRoutingProtocol::BroadcastPacket(Ptr<Packet> packet)
{
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator i =
           m_socketAddresses.begin();
       i != m_socketAddresses.end(); i++)
  {
    Ptr<Packet> pkt = packet->Copy();
    Ipv4Address broadcastAddr = i->second.GetLocal().GetSubnetDirectedBroadcast(i->second.GetMask());
    i->first->SendTo(pkt, 0, InetSocketAddress(broadcastAddr, DV_PORT_NUMBER));
  }
}

void DVRoutingProtocol::ProcessCommand(std::vector<std::string> tokens)
{
  std::vector<std::string>::iterator iterator = tokens.begin();
  std::string command = *iterator;
  if (command == "PING")
  {
    if (tokens.size() < 3)
    {
      ERROR_LOG("Insufficient PING params...");
      return;
    }
    iterator++;
    std::istringstream sin(*iterator);
    uint32_t nodeNumber;
    sin >> nodeNumber;
    iterator++;
    std::string pingMessage = *iterator;
    Ipv4Address destAddress = ResolveNodeIpAddress(nodeNumber);
    if (destAddress != Ipv4Address::GetAny())
    {
      uint32_t sequenceNumber = GetNextSequenceNumber();
      TRAFFIC_LOG("Sending PING_REQ to Node: " << nodeNumber << " IP: " << destAddress << " Message: " << pingMessage << " SequenceNumber: " << sequenceNumber);
      Ptr<PingRequest> pingRequest = Create<PingRequest>(sequenceNumber, Simulator::Now(), destAddress, pingMessage);
      // Add to ping-tracker
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
    if (tokens.size() < 2)
    {
      ERROR_LOG("Insufficient Parameters!");
      return;
    }
    iterator++;
    std::string table = *iterator;
    if (table == "ROUTES" || table == "ROUTING")
    {
      DumpRoutingTable();
    }
    else if (table == "NEIGHBORS" || table == "NEIGHBOURS")
    {
      DumpNeighbors();
    }
  }
}

void DVRoutingProtocol::DumpNeighbors()
{
  STATUS_LOG(std::endl
             << "**************** Neighbor List ********************" << std::endl
             << "NeighborNumber\t\tNeighborAddr\t\tInterfaceAddr");
  //PRINT_LOG("");

  std::vector<NeighborTableEntry> neighbors = m_neighbors.Snapshot();
  PRINT_LOG(neighbors.size()); // Print number of neighbors
  for (const auto& entry : neighbors) {
    PRINT_LOG(ReverseLookup(entry.neighborAddress) << "\t\t\t"
               << entry.neighborAddress << "\t\t"
               << entry.interfaceAddress);

    /* NOTE: For purpose of autograding, you should invoke the following function for each
    neighbor table entry. The output format is indicated by parameter name and type.
    */
    //  checkNeighborTableEntry();
    checkNeighborTableEntry(ReverseLookup(entry.neighborAddress), entry.neighborAddress, entry.interfaceAddress);
  }
}

void DVRoutingProtocol::DumpRoutingTable()
{
  STATUS_LOG(std::endl
             << "**************** Route Table ********************" << std::endl
             << "DestNumber\t\tDestAddr\t\tNextHopNumber\t\tNextHopAddr\t\tInterfaceAddr\t\tCost");

  PRINT_LOG(""); //blank line to match output
  
  /*std::vector<RoutingTableEntry> routes = Snapshot();
  PRINT_LOG(routes.size()); // Print number of routes
  for (const auto& entry : routes) {
    PRINT_LOG(ReverseLookup(entry.dest) << "\t\t\t"
               << entry.dest << "\t\t"
               << ReverseLookup(entry.nextHop) << "\t\t\t"
               << entry.nextHop << "\t\t"
               << entry.interface << "\t\t"
               << entry.cost);

  // NOTE: For purpose of autograding, you should invoke the following function for each
  // routing table entry. The output format is indicated by parameter name and type.
  //  checkRouteTableEntry();

      checkRouteTableEntry(ReverseLookup(entry.dest), entry.dest, 
          strtoul(ReverseLookup(entry.nextHop).c_str(), NULL, 10), entry.nextHop, entry.interface, entry.cost);
  }*/

  PRINT_LOG(m_routingTable.size()); // Print number of routes
  for (auto it = m_routingTable.begin(); it != m_routingTable.end(); ++it) {
      const auto& entry = it->second;
      PRINT_LOG(ReverseLookup(entry.dest) << "\t\t\t"
                << entry.dest << "\t\t"
                << ReverseLookup(entry.nextHop) << "\t\t\t"
                << entry.nextHop << "\t\t"
                << entry.interface << "\t\t"
                << entry.cost);

  //NOTE: For purpose of autograding, you should invoke the following function for each
  //routing table entry. The output format is indicated by parameter name and type.
  //  checkRouteTableEntry();

      checkRouteTableEntry(ReverseLookup(entry.dest), entry.dest, 
          strtoul(ReverseLookup(entry.nextHop).c_str(), NULL, 10), entry.nextHop, entry.interface, entry.cost);
  }
}

void DVRoutingProtocol::RecvDVMessage(Ptr<Socket> socket)
{
  Address sourceAddr;
  Ptr<Packet> packet = socket->RecvFrom(sourceAddr);
  DVMessage dvMessage;
  Ipv4PacketInfoTag interfaceInfo;
  if (!packet->RemovePacketTag(interfaceInfo))
  {
    NS_ABORT_MSG("No incoming interface on OLSR message, aborting.");
  }
  uint32_t incomingIf = interfaceInfo.GetRecvIf();

  if (!packet->RemoveHeader(dvMessage))
  {
    NS_ABORT_MSG("No incoming interface on LS message, aborting.");
  }

  Ipv4Address interface;
  uint32_t idx = 1;
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::iterator iter = m_socketAddresses.begin();
       iter != m_socketAddresses.end(); iter++)
  {
    if (idx == incomingIf)
    {
      interface = iter->second.GetLocal(); // find the incoming interface
      break;
    }
    idx++;
  }

  switch (dvMessage.GetMessageType())
  {
  case DVMessage::PING_REQ:
    ProcessPingReq(dvMessage);
    break;
  case DVMessage::PING_RSP:
    ProcessPingRsp(dvMessage);
    break;
  case DVMessage::HELLO_REQ:
    ProcessHelloReq(dvMessage);
    break;
  case DVMessage::HELLO_RSP:
    ProcessHelloRsp(dvMessage, interface);
    break;
  case DVMessage::DV_UPDATE: // MS2 part 2 related
   /* MS2 Part 2 will process */
   ProcessDvUpdate(dvMessage, interface);
   break;  
  default:
    ERROR_LOG("Unknown Message Type!");
    break;
  }
}

// Oliver: Handle and respond to hello_req messages received from a neighbor
void DVRoutingProtocol::ProcessHelloReq(DVMessage dvMessage)
{
  // Use reverse lookup for ease of debug
  std::string fromNode = ReverseLookup(dvMessage.GetOriginatorAddress());
  TRAFFIC_LOG("Received HELLO_REQ, From Node: " << fromNode << ", Message: " << dvMessage.GetHelloReq().helloMessage);
  // Send Hello Response
  DVMessage dvResp = DVMessage(DVMessage::HELLO_RSP, dvMessage.GetSequenceNumber(), 1, m_mainAddress);
  dvResp.SetHelloRsp(dvMessage.GetOriginatorAddress(), dvMessage.GetHelloReq().helloMessage);
  Ptr<Packet> packet = Create<Packet>();
  packet->AddHeader(dvResp);
  BroadcastPacket(packet);
}

// Oliver: Handle to hello_rsp messages received from a neighbor and update neighbor table accordingly
void DVRoutingProtocol::ProcessHelloRsp(DVMessage dvMessage, Ipv4Address localInterfaceAddress)
{
  // Extract information for neighbor table entry
  Ipv4Address neighborAddress = dvMessage.GetOriginatorAddress();
  // Add to Neighbor Table
  m_neighbors.ObserveHello(neighborAddress, localInterfaceAddress);

  // Logging
  std::string fromNode = ReverseLookup(neighborAddress);
  TRAFFIC_LOG("Received HELLO_RSP, From Node: " << fromNode << ", Message: " << dvMessage.GetHelloRsp().helloMessage)
  DEBUG_LOG("Received HELLO_RSP, From: " << neighborAddress << " on Interface: " << localInterfaceAddress);
}

void DVRoutingProtocol::ProcessPingReq(DVMessage dvMessage)
{
  // Check destination address
  if (IsOwnAddress(dvMessage.GetPingReq().destinationAddress))
  {
    // Use reverse lookup for ease of debug
    std::string fromNode = ReverseLookup(dvMessage.GetOriginatorAddress());
    TRAFFIC_LOG("Received PING_REQ, From Node: " << fromNode << ", Message: " << dvMessage.GetPingReq().pingMessage);
    // Send Ping Response
    DVMessage dvResp = DVMessage(DVMessage::PING_RSP, dvMessage.GetSequenceNumber(), m_maxTTL, m_mainAddress);
    dvResp.SetPingRsp(dvMessage.GetOriginatorAddress(), dvMessage.GetPingReq().pingMessage);
    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(dvResp);
    BroadcastPacket(packet);
  }
}

void DVRoutingProtocol::ProcessPingRsp(DVMessage dvMessage)
{
  // Check destination address
  if (IsOwnAddress(dvMessage.GetPingRsp().destinationAddress))
  {
    // Remove from pingTracker
    std::map<uint32_t, Ptr<PingRequest>>::iterator iter;
    iter = m_pingTracker.find(dvMessage.GetSequenceNumber());
    if (iter != m_pingTracker.end())
    {
      std::string fromNode = ReverseLookup(dvMessage.GetOriginatorAddress());
      TRAFFIC_LOG("Received PING_RSP, From Node: " << fromNode << ", Message: " << dvMessage.GetPingRsp().pingMessage);
      m_pingTracker.erase(iter);
    }
    else
    {
      DEBUG_LOG("Received invalid PING_RSP!");
    }
  }
}

bool DVRoutingProtocol::IsOwnAddress(Ipv4Address originatorAddress)
{
  // Check all interfaces
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator i = m_socketAddresses.begin(); i != m_socketAddresses.end(); i++)
  {
    Ipv4InterfaceAddress interfaceAddr = i->second;
    if (originatorAddress == interfaceAddr.GetLocal())
    {
      return true;
    }
  }
  return false;
}

// Oliver: Handle periodic broadcasting of hello_req and checking neighborhood table for expired entries
void DVRoutingProtocol::AuditHellos()
{
  // Send Periodic HelloReq
  DVMessage dvReq = DVMessage(DVMessage::HELLO_RSP, 0, 1, m_mainAddress);
  dvReq.SetHelloReq("Hello!");
  Ptr<Packet> packet = Create<Packet>();
  packet->AddHeader(dvReq);
  BroadcastPacket(packet);

  //Audit Neighbor Table
  m_neighbors.Audit();
  DEBUG_LOG("Audited Neighbor Table at: " << m_mainAddress << ", Current size: " << m_neighbors.Size());
}

void DVRoutingProtocol::AuditPings()
{
  std::map<uint32_t, Ptr<PingRequest>>::iterator iter;
  for (iter = m_pingTracker.begin(); iter != m_pingTracker.end();)
  {
    Ptr<PingRequest> pingRequest = iter->second;
    if (pingRequest->GetTimestamp().GetMilliSeconds() + m_pingTimeout.GetMilliSeconds() <= Simulator::Now().GetMilliSeconds())
    {
      DEBUG_LOG("Ping expired. Message: " << pingRequest->GetPingMessage() << " Timestamp: " << pingRequest->GetTimestamp().GetMilliSeconds() << " CurrentTime: " << Simulator::Now().GetMilliSeconds());

      // Remove stale entries
      m_pingTracker.erase(iter++);
    }
    else
    {
      ++iter;
    }
  }
  // Rechedule timer
  m_auditPingsTimer.Schedule(m_pingTimeout);
}

uint32_t
DVRoutingProtocol::GetNextSequenceNumber()
{
  m_currentSequenceNumber = (m_currentSequenceNumber + 1) % (DV_MAX_SEQUENCE_NUMBER + 1);
  return m_currentSequenceNumber;
}

void DVRoutingProtocol::NotifyInterfaceUp(uint32_t i)
{
  m_staticRouting->NotifyInterfaceUp(i);
}
void DVRoutingProtocol::NotifyInterfaceDown(uint32_t i)
{
  m_staticRouting->NotifyInterfaceDown(i);
}
void DVRoutingProtocol::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
  m_staticRouting->NotifyAddAddress(interface, address);
}
void DVRoutingProtocol::NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
  m_staticRouting->NotifyRemoveAddress(interface, address);
}

void DVRoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4)
{
  NS_ASSERT(ipv4 != 0);
  NS_ASSERT(m_ipv4 == 0);
  NS_LOG_DEBUG("Created dv::RoutingProtocol");
  // Configure timers
  m_auditPingsTimer.SetFunction(&DVRoutingProtocol::AuditPings, this);
  m_ipv4 = ipv4;
  m_staticRouting->SetIpv4(m_ipv4);
}

/* ---------------- MS2 related (Part 1) ---------------- */

// Build and send a DV_UPDATE on all interfaces.
// Part 1: the advertised vector is minimal (only self route with cost=0).
// Part 2 will replace this with real DV routes + split horizon/poison reverse.
void DVRoutingProtocol::SendPeriodicUpdate()
{
  // Minimal vector: advertise reachability to self with cost 0.
  std::vector<DVMessage::DvVectorItem> vec;
  vec.push_back({ m_mainAddress, 0 });

  DVMessage msg(DVMessage::DV_UPDATE, GetNextSequenceNumber(), m_maxTTL, m_mainAddress);
  msg.SetDvUpdate(vec); // MS2 related: attach the DV vector payload (currently only self, cost=0)

  for (const auto& kv : m_socketAddresses)
  {
    Ptr<Socket> sock = kv.first;
    const Ipv4InterfaceAddress& ifAddr = kv.second;
    Ptr<Packet> p = Create<Packet>();
    p->AddHeader(msg);
    Ipv4Address bcast = ifAddr.GetLocal().GetSubnetDirectedBroadcast(ifAddr.GetMask());
    sock->SendTo(p, 0, InetSocketAddress(bcast, DV_PORT_NUMBER));
  }
  // Re-schedule periodic
  m_periodicUpdateTimer.Cancel();
  m_periodicUpdateTimer.Schedule(m_periodicInterval);  // MS2 related: keep the periodic DV_UPDATE cadence running
}

// Coalesce triggered updates; schedule a send soon (reuses periodic sender)
void DVRoutingProtocol::TriggerUpdateSoon()
{
  if (m_triggeredUpdateTimer.IsRunning()) return;
  m_triggeredUpdateTimer.Schedule(m_triggerHold);      // MS2 related: defer briefly so multiple changes collapse into one update
}

uint32_t DVRoutingProtocol::UpdateRoute(Ipv4Address dest, Ipv4Address source, Ipv4Address sourceInterface, uint32_t sourceCost) {
  // Invalid route: If neighbor's route has been invalidated 
  if (m_routingTable.count(dest) == 0) {

    /* ------ WHEN THIS SECTION IS COMMENTED OUT THEN NO SIGIOT IS RECEIVED ------*/
    RoutingTableEntry &e = m_routingTable[dest];
      e.dest = dest;
      e.nextHop = source;
      e.interface = sourceInterface;
      e.cost = sourceCost + 1;
      e.timestamp = Simulator::Now();
    /* -------------------------------------------------------------------------- */

      return ROUTE_UPDATED;
  } else {
    return ROUTE_NOT_UPDATED;
  }
}

void DVRoutingProtocol::ProcessDvUpdate(DVMessage dvMessage, Ipv4Address sourceInterface) {
  //Process message and retrieve information from packet
  Ipv4Address neighbor = dvMessage.GetOriginatorAddress();
  DEBUG_LOG("Received DV_UPDATE, From: " << neighbor);
  bool updated = false;
  for (const auto& entry : dvMessage.GetDvUpdate().vec) {
    if (entry.dest == m_mainAddress) continue;
    if (UpdateRoute(entry.dest, neighbor, sourceInterface, entry.cost)
          == ROUTE_UPDATED) {
            updated = true;
          }
  }
  if (updated) {
    TriggerUpdateSoon();
    DEBUG_LOG("Triggered DV update due to routing change(s)");
  }
}
