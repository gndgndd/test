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
#include <ctime>
#include <iostream>
#include <string>
#include <unistd.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LSRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(LSRoutingProtocol);

/********** Miscellaneous constants **********/

/// Maximum allowed sequence number
#define LS_MAX_SEQUENCE_NUMBER 0xFFFF
#define LS_PORT_NUMBER 698





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

LSRoutingProtocol::LSRoutingProtocol()
    : m_auditPingsTimer(Timer::CANCEL_ON_DESTROY),
    m_auditNeighborsTimer(Timer:: CANCEL_ON_DESTROY)
{

  m_currentSequenceNumber = 0;
  // Setup static routing
  m_staticRouting = Create<Ipv4StaticRouting>();
}

LSRoutingProtocol::~LSRoutingProtocol() {}

void LSRoutingProtocol::DoDispose()
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
  m_pingTracker.clear();
  m_auditNeighborsTimer.Cancel();
  //m_pingTracker.clear();

  PennRoutingProtocol::DoDispose();
}

void LSRoutingProtocol::SetMainInterface(uint32_t mainInterface)
{
  m_mainAddress = m_ipv4->GetAddress(mainInterface, 0).GetLocal();
}

void LSRoutingProtocol::SetNodeAddressMap(std::map<uint32_t, Ipv4Address> nodeAddressMap)
{
  m_nodeAddressMap = nodeAddressMap;
}

void LSRoutingProtocol::SetAddressNodeMap(std::map<Ipv4Address, uint32_t> addressNodeMap)
{
  m_addressNodeMap = addressNodeMap;
}

Ipv4Address
LSRoutingProtocol::ResolveNodeIpAddress(uint32_t nodeNumber)
{
  std::map<uint32_t, Ipv4Address>::iterator iter = m_nodeAddressMap.find(nodeNumber);
  if (iter != m_nodeAddressMap.end())
  {
    return iter->second;
  }
  return Ipv4Address::GetAny();
}

std::string
LSRoutingProtocol::ReverseLookup(Ipv4Address ipAddress)
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

void LSRoutingProtocol::DoInitialize()
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

  NS_LOG_DEBUG("Starting LS on node " << m_mainAddress);

  bool canRunLS = false;
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
      InetSocketAddress inetAddr(Ipv4Address::GetAny(), LS_PORT_NUMBER);
      m_recvSocket->SetRecvCallback(MakeCallback(&LSRoutingProtocol::RecvLSMessage, this));
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
    InetSocketAddress inetAddr(m_ipv4->GetAddress(i, 0).GetLocal(), m_lsPort);
    socket->SetRecvCallback(MakeCallback(&LSRoutingProtocol::RecvLSMessage, this));
    if (socket->Bind(inetAddr))
    {
      NS_FATAL_ERROR("LSRoutingProtocol::DoInitialize::Failed to bind socket!");
    }
    socket->BindToNetDevice(m_ipv4->GetNetDevice(i));
    m_socketAddresses[socket] = m_ipv4->GetAddress(i, 0);
    canRunLS = true;
  }

  if (canRunLS)
  {
    AuditNeighbors();
    AuditPings();
    NS_LOG_DEBUG("Starting LS on node " << m_mainAddress);
  }
}

void LSRoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
  // You can ignore this function
}

Ptr<Ipv4Route>
LSRoutingProtocol::RouteOutput(Ptr<Packet> packet, const Ipv4Header &header, Ptr<NetDevice> outInterface,
                               Socket::SocketErrno &sockerr)
{
  Ptr<Ipv4Route> ipv4Route = m_staticRouting->RouteOutput(packet, header, outInterface, sockerr);
  if (ipv4Route)
  {
    DEBUG_LOG("Found route to: " << ipv4Route->GetDestination() << " via next-hop: " << ipv4Route->GetGateway()
                                 << " with source: " << ipv4Route->GetSource() << " and output device "
                                 << ipv4Route->GetOutputDevice());
  }
  else
  {
    DEBUG_LOG("No Route to destination: " << header.GetDestination());
  }
  return ipv4Route;
}

bool LSRoutingProtocol::RouteInput(Ptr<const Packet> packet, const Ipv4Header &header, Ptr<const NetDevice> inputDev,
                                   UnicastForwardCallback ucb, MulticastForwardCallback mcb, LocalDeliverCallback lcb,
                                   ErrorCallback ecb)
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

void LSRoutingProtocol::BroadcastPacket(Ptr<Packet> packet)
{
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator i = m_socketAddresses.begin();
       i != m_socketAddresses.end(); i++)
  {
    Ptr<Packet> pkt = packet->Copy();
    Ipv4Address broadcastAddr = i->second.GetLocal().GetSubnetDirectedBroadcast(i->second.GetMask());
    i->first->SendTo(pkt, 0, InetSocketAddress(broadcastAddr, LS_PORT_NUMBER));
  }
}

void LSRoutingProtocol::ProcessCommand(std::vector<std::string> tokens)
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
      TRAFFIC_LOG("Sending PING_REQ to Node: " << nodeNumber << " IP: " << destAddress << " Message: "
                                               << pingMessage << " SequenceNumber: " << sequenceNumber);
      Ptr<PingRequest> pingRequest = Create<PingRequest>(sequenceNumber, Simulator::Now(), destAddress, pingMessage);
      // Add to ping-tracker
      m_pingTracker.insert(std::make_pair(sequenceNumber, pingRequest));
      Ptr<Packet> packet = Create<Packet>();
      LSMessage lsMessage = LSMessage(LSMessage::PING_REQ, sequenceNumber, m_maxTTL, m_mainAddress);
      lsMessage.SetPingReq(destAddress, pingMessage);
      packet->AddHeader(lsMessage);
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
    else if (table == "NEIGHBORS" || table == "neighborS")
    {
      DumpNeighbors();
    }
    else if (table == "LSA")
    {
      DumpLSA();
    }
  }
}

void LSRoutingProtocol::DumpLSA()
{
  STATUS_LOG(std::endl
             << "**************** LSA DUMP ********************" << std::endl
             << "Node\t\tNeighbor(s)");
  PRINT_LOG("");
}

void LSRoutingProtocol::DumpNeighbors()
{
  STATUS_LOG(std::endl
             << "**************** Neighbor List ********************" << std::endl
             << "NeighborNumber\t\tNeighborAddr\t\tInterfaceAddr");

  // Print the number of entries (for the grader)
  PRINT_LOG(m_neighbors.size());

  for (const auto &kv : m_neighbors)
  {
    const uint32_t neighborNum = kv.first;
    const NeighborTableEntry &entry = kv.second;

    // autograder hook
    checkNeighborTableEntry(neighborNum, entry.neighborAddr, entry.interfaceAddr);

    // keep column order the same
    PRINT_LOG(neighborNum << '\t' << entry.neighborAddr << '\t' << entry.interfaceAddr);
  }
}


void LSRoutingProtocol::DumpRoutingTable()
{
  STATUS_LOG(std::endl
             << "**************** Route Table ********************" << std::endl
             << "DestNumber\t\tDestAddr\t\tNextHopNumber\t\tNextHopAddr\t\tInterfaceAddr\t\tCost");

  if (m_neighbors.empty()){
    PRINT_LOG(m_neighbors.size());
    return;
  }
  
  PRINT_LOG(m_routingTable.size());
  for (const auto& pair : m_routingTable){
    uint32_t dest_node_num = pair.first;
    const RoutingTableEntry& entry = pair.second;
    checkRouteTableEntry(dest_node_num, entry.destAddr, entry.nextHopNum, entry.nextHopAddr, entry.interfaceAddr, entry.cost);
    PRINT_LOG(dest_node_num << '\t' << entry.destAddr << '\t' << entry.nextHopNum << '\t' << entry.nextHopAddr
     << '\t' << entry.interfaceAddr << '\t' << entry.cost);
  }
}


void LSRoutingProtocol::RecvLSMessage(Ptr<Socket> socket)
{
  Address sourceAddr;
  Ptr<Packet> packet = socket->RecvFrom(sourceAddr);
  LSMessage lsMessage;
  Ipv4PacketInfoTag interfaceInfo;
  if (!packet->RemovePacketTag(interfaceInfo))
  {
    NS_ABORT_MSG("No incoming interface on OLSR message, aborting.");
  }
  uint32_t incomingIf = interfaceInfo.GetRecvIf();

  if (!packet->RemoveHeader(lsMessage))
  {
    NS_ABORT_MSG("No incoming interface on LS message, aborting.");
  }

  Ipv4Address interface = Ipv4Address::GetAny();
  if (incomingIf < m_ipv4->GetNInterfaces())
  {
    interface = m_ipv4->GetAddress(incomingIf, 0).GetLocal();
  }

  switch (lsMessage.GetMessageType())
  {
  case LSMessage::PING_REQ:
    ProcessPingReq(lsMessage);
    break;
  case LSMessage::PING_RSP:
    ProcessPingRsp(lsMessage);
    break;
  case LSMessage::HELLO_REQ:
    ProcessHelloReq(lsMessage);
    break;
  case LSMessage::HELLO_RSP:
    ProcessHelloRsp(lsMessage, interface);
    break;
  case LSMessage::LSA_m:
   ProcessLsp(lsMessage, interface);
   break;  
  default:
    ERROR_LOG("Unknown Message Type!");
    break;
  }
}

void LSRoutingProtocol::ProcessPingReq(LSMessage lsMessage)
{
  // Check destination address
  if (IsOwnAddress(lsMessage.GetPingReq().destinationAddress))
  {
    // Use reverse lookup for ease of debug
    std::string fromNode = ReverseLookup(lsMessage.GetOriginatorAddress());
    TRAFFIC_LOG("Received PING_REQ, From Node: " << fromNode
                                                 << ", Message: " << lsMessage.GetPingReq().pingMessage);
    // Send Ping Response
    LSMessage lsResp = LSMessage(LSMessage::PING_RSP, lsMessage.GetSequenceNumber(), m_maxTTL, m_mainAddress);
    lsResp.SetPingRsp(lsMessage.GetOriginatorAddress(), lsMessage.GetPingReq().pingMessage);
    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(lsResp);
    BroadcastPacket(packet);
  }
}


void LSRoutingProtocol::ProcessPingRsp(LSMessage lsMessage)
{
  // Check destination address
  
  if (IsOwnAddress(lsMessage.GetPingRsp().destinationAddress))
  {
    // Remove from pingTracker
    std::map<uint32_t, Ptr<PingRequest>>::iterator iter;
    iter = m_pingTracker.find(lsMessage.GetSequenceNumber());
    if (iter != m_pingTracker.end())
    {
      std::string fromNode = ReverseLookup(lsMessage.GetOriginatorAddress());
      TRAFFIC_LOG("Received PING_RSP, From Node: " << fromNode
                                                   << ", Message: " << lsMessage.GetPingRsp().pingMessage);
      m_pingTracker.erase(iter);
    }
    else
    {
      PRINT_LOG("Received invalid PING_RSP!");
    }
  }
}

void LSRoutingProtocol::ProcessHelloReq(LSMessage lsMessage) {
    const std::string kHelloReply = "HELLO_REPLY";
    const int max_ttl = 1;
    
    LSMessage helloRsp(LSMessage::HELLO_RSP, lsMessage.GetSequenceNumber(), max_ttl, m_mainAddress);
    helloRsp.SetHelloRsp(lsMessage.GetOriginatorAddress(), kHelloReply);
    
    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(helloRsp);
    
    BroadcastPacket(packet);
}


void LSRoutingProtocol::ProcessHelloRsp(LSMessage lsMessage, Ipv4Address interfaceAd){
    // Check destination address
    if (IsOwnAddress(lsMessage.GetHelloRsp().destinationAddress)){
        //address of the neighbour node of m_node above
        Ipv4Address neighbor_discovered = lsMessage.GetOriginatorAddress();
        std::string neighbourNumStr = ReverseLookup(lsMessage.GetOriginatorAddress());
        uint32_t neighborNum;
        std::istringstream s(neighbourNumStr);
        s >> neighborNum;
        NeighborTableEntry neighbourEntry;
        neighbourEntry.neighborAddr = neighbor_discovered;
        neighbourEntry.t_stamp = Simulator::Now();
        neighbourEntry.interfaceAddr = interfaceAd;

        // Correct C++11 compatible logic to insert or update the map
        auto iter = m_neighbors.find(neighborNum);
        if (iter == m_neighbors.end()) {
            m_neighbors.insert({neighborNum, neighbourEntry});
        } else {
            iter->second = neighbourEntry;
        }
    }
}


void LSRoutingProtocol::AuditNeighbors()
{
    m_neighborTimeout = Seconds(5.0);
    auto iter = m_neighbors.begin();
    
    while (iter != m_neighbors.end()) {
        NeighborTableEntry neighbor_entry = iter->second;
        if ((neighbor_entry.t_stamp + m_neighborTimeout).GetMilliSeconds() <= Simulator::Now().GetMilliSeconds()) {
            iter = m_neighbors.erase(iter); // Erase and get the next valid iterator
        } else {
            ++iter;
        }
    }
    
    BroadcastHello();
    LSAdvertise();
    m_auditNeighborsTimer.Schedule(Seconds(5));
}

void LSRoutingProtocol::BroadcastHello()
{
    const std::string kHelloMessage = "HELLO";
    const int maxTTL = 1;
    uint32_t sequenceNumber = GetNextSequenceNumber();

    LSMessage lsMessage(LSMessage::HELLO_REQ, sequenceNumber, maxTTL, m_mainAddress);
    lsMessage.SetHelloReq(Ipv4Address::GetAny(), kHelloMessage);

    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(lsMessage);
    
    BroadcastPacket(packet);
}


void LSRoutingProtocol::LSAdvertise()
{
    uint32_t sequenceNumber = GetNextSequenceNumber();
    neighborInfo n_nodes;
    uint32_t linkcost = 1;
    int m_maxTTL = 1;

    for (const auto& pair : m_neighbors) {
        uint32_t node_num = pair.first;
        n_nodes.push_back(std::make_pair(node_num, linkcost));
    }

    Ptr<Packet> pkt = Create<Packet>();
    LSMessage lsMessage(LSMessage::LSA_m, sequenceNumber, m_maxTTL, m_mainAddress);
    lsMessage.SetLsA(n_nodes);

    pkt->AddHeader(lsMessage);
    BroadcastPacket(pkt);
}


void LSRoutingProtocol::ProcessLsp(LSMessage lsMessage, Ipv4Address interface_a) {
    // Extract information from the message
    uint32_t fromNodeNum;
    std::istringstream(ReverseLookup(lsMessage.GetOriginatorAddress())) >> fromNodeNum;
    uint32_t seqNum = lsMessage.GetSequenceNumber();

    // Look for the entry in the map
    auto iter = m_validLSP.find(fromNodeNum);

    // If the node is not in the map, insert a new entry.
    if (iter == m_validLSP.end()) {
        LSPneighbors newEntry;
        newEntry.seqNumber = seqNum;
        newEntry.interfaceAd = interface_a;
        newEntry.neighbornodeandCost = lsMessage.GetLsA().lsaMessage;
        m_validLSP.insert({fromNodeNum, newEntry});
    } 
    // If the node is found, check if the sequence number is newer.
    else {
        if (iter->second.seqNumber >= seqNum) {
            return; // Outdated message, discard.
        }
        
        // Update the existing entry with the new information.
        LSPneighbors updatedEntry;
        updatedEntry.seqNumber = seqNum;
        updatedEntry.neighbornodeandCost = lsMessage.GetLsA().lsaMessage;
        iter->second = updatedEntry;
    }

    // After processing the LSP, run Dijkstra's algorithm and re-broadcast the packet.
    Dijkstra();

    Ptr<Packet> pkt = Create<Packet>();
    lsMessage.SetTTL(lsMessage.GetTTL() - 1);
    pkt->AddHeader(lsMessage);
    BroadcastPacket(pkt);
}


void LSRoutingProtocol::floodLSA(Ptr<Packet> packet, Ipv4Address fromNode)
{
    for (const auto& pair : m_socketAddresses) {
        Ptr<Packet> pkt = packet->Copy();
        Ipv4Address broadcastAddr = pair.second.GetLocal().GetSubnetDirectedBroadcast(pair.second.GetMask());
        if (broadcastAddr != fromNode) {
            pair.first->SendTo(pkt, 0, InetSocketAddress(broadcastAddr, LS_PORT_NUMBER));
        }
    }
}

#include <queue>
#include <map>
#include <limits>

void LSRoutingProtocol::Dijkstra()
{ 
  m_routingTable.clear();
  std::vector<std::pair<uint32_t, std::pair<uint32_t, uint32_t>>> confirmed;  
  std::vector<std::pair<uint32_t, std::pair<uint32_t, uint32_t>>> tentative;

  std::string nodetoaddstr = ReverseLookup(m_mainAddress);
  uint32_t nodetoadd;
  std::istringstream sin(nodetoaddstr);
  sin >> nodetoadd;

  uint32_t cost_to_node = 0;
  confirmed.push_back(make_pair(nodetoadd, std::make_pair(cost_to_node, nodetoadd)));
  uint32_t prev_cost = cost_to_node;
  if (m_neighbors.size() ==0){
    return;
  }
  while (1){
  uint32_t nextNode = nodetoadd;   
  uint32_t viaNode;
  std::map<uint32_t, LSPneighbors>::iterator iter;
  iter = m_validLSP.find(nextNode);
  if (iter == m_validLSP.end())
 { return;}
    LSPneighbors neighborInfoent = iter->second;
    std::vector <std::pair<uint32_t, uint32_t>> neighborInfoentry = neighborInfoent.neighbornodeandCost;
 
    for (unsigned int i =0; i < neighborInfoentry.size(); i++){
      uint32_t node_num = neighborInfoentry[i].first;
      uint32_t cost = neighborInfoentry[i].second;
      uint32_t new_cost = cost + prev_cost;
      std::pair<uint32_t, std::pair<uint32_t, uint32_t>> p;
      if (prev_cost == 0){
         p =std::make_pair(node_num, std::make_pair(new_cost, node_num));
      }
      else
      {p =std::make_pair(node_num, std::make_pair(new_cost, viaNode));}

      bool notinConfirmed = true;
      for (unsigned int k = 0; k <confirmed.size(); k++)
      { if (confirmed[k].first == node_num){
        notinConfirmed = false;
        }
      }
      if (notinConfirmed) {     
          unsigned int count  = 0;
          for (unsigned int j =0; j < tentative.size(); j++){                        
            uint32_t node_tentative = tentative[j].first;
            uint32_t storedcost = tentative[j].second.first;
            if (node_tentative == node_num)
            {
              count = count+1;
              if (storedcost > new_cost){
                tentative[j] = p;
              }                
            }
          }
          if (count == 0)
          {
            tentative.push_back(p);
          }      
      }
    }
  

  if (tentative.empty()){break;}
  uint32_t min_cost;
  unsigned int index = 0;
  min_cost = tentative[index].second.first;
  for (unsigned int i =1; i < tentative.size(); i++){
    if (tentative[i].second.first < min_cost){
      index = i;
      min_cost = tentative[index].second.first;
    }   
  }
  std::pair<uint32_t, std::pair<uint32_t, uint32_t>> toConfirmed = tentative[index];
  confirmed.push_back(toConfirmed);
  tentative.erase(tentative.begin() + index);
  nodetoadd = toConfirmed.first;
  viaNode = toConfirmed.second.second;
  prev_cost = toConfirmed.second.first;
  
  for (unsigned int i =1; i < confirmed.size(); i++){
    uint32_t dest_node = confirmed[i].first;  
    Ipv4Address dest_addr = ResolveNodeIpAddress(dest_node);
    uint32_t next_hop = confirmed[i].second.second;
    Ipv4Address next_hopAddr = ResolveNodeIpAddress(next_hop);
    uint32_t cost = confirmed[i].second.first;
    std::map<uint32_t, NeighborTableEntry>::iterator it;   
    it = m_neighbors.find(next_hop);

    Ipv4Address interAddr =  it->second.interfaceAddr;
    RoutingTableEntry r = {dest_addr, next_hop, next_hopAddr, interAddr, cost};
    m_routingTable.insert({dest_node, r});
  }
  }
   
}

bool LSRoutingProtocol::IsOwnAddress(Ipv4Address originatorAddress)
{
  // Check all interfaces
  for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator i = m_socketAddresses.begin();
       i != m_socketAddresses.end(); i++)
  {
    Ipv4InterfaceAddress interfaceAddr = i->second;
    if (originatorAddress == interfaceAddr.GetLocal())
    {
      return true;
    }
  }
  return false;
}

void LSRoutingProtocol::AuditPings()
{
  std::map<uint32_t, Ptr<PingRequest>>::iterator iter;
  for (iter = m_pingTracker.begin(); iter != m_pingTracker.end();)
  {
    Ptr<PingRequest> pingRequest = iter->second;
    if (pingRequest->GetTimestamp().GetMilliSeconds() + m_pingTimeout.GetMilliSeconds() <= Simulator::Now().GetMilliSeconds())
    {
      DEBUG_LOG("Ping expired. Message: " << pingRequest->GetPingMessage()
                                          << " Timestamp: " << pingRequest->GetTimestamp().GetMilliSeconds()
                                          << " CurrentTime: " << Simulator::Now().GetMilliSeconds());
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
LSRoutingProtocol::GetNextSequenceNumber()
{
  m_currentSequenceNumber = (m_currentSequenceNumber + 1) % (LS_MAX_SEQUENCE_NUMBER + 1);
  return m_currentSequenceNumber;
}

void LSRoutingProtocol::NotifyInterfaceUp(uint32_t i)
{
  m_staticRouting->NotifyInterfaceUp(i);
}
void LSRoutingProtocol::NotifyInterfaceDown(uint32_t i)
{
  m_staticRouting->NotifyInterfaceDown(i);
}
void LSRoutingProtocol::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
  m_staticRouting->NotifyAddAddress(interface, address);
}
void LSRoutingProtocol::NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
  m_staticRouting->NotifyRemoveAddress(interface, address);
}

void LSRoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4)
{
  NS_ASSERT(ipv4 != 0);
  NS_ASSERT(m_ipv4 == 0);
  NS_LOG_DEBUG("Created ls::RoutingProtocol");
  // Configure timers
  m_auditPingsTimer.SetFunction(&LSRoutingProtocol::AuditPings, this);
  m_auditNeighborsTimer.SetFunction(&LSRoutingProtocol::AuditNeighbors, this);
  m_ipv4 = ipv4;
  m_staticRouting->SetIpv4(m_ipv4);
}
