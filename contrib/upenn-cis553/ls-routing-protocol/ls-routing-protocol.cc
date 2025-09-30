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


//std::map<uint32_t, RoutingTableEntry> m_routingTable;

//uint32_t m_current_node;  //for debugging purposes

Timer m_auditNeighborsTimer;


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
    //BroadcastHello();
    AuditPings();
    //LSAdvertise();
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
/*
  else if (command == "LINK" || command == "NODELINKS")
  {
    BroadcastHello();
    //LSAdvertise();
  }*/
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
 
  PRINT_LOG(m_neighbors.size());
 
  for (auto itr = m_neighbors.begin(); itr != m_neighbors.end(); itr++){
    uint32_t node_num = itr->first;
    NeighborTableEntry entry = itr->second;
    checkNeighborTableEntry(node_num, entry.neighborAddr, entry.interfaceAddr);
    PRINT_LOG(node_num << '\t' << entry.neighborAddr << '\t' << entry.interfaceAddr);
  }
   
}

void LSRoutingProtocol::DumpRoutingTable()
{
 // LSAdvertise();
  
 // Dijkstra();
  
  STATUS_LOG(std::endl
             << "**************** Route Table ********************" << std::endl
             << "DestNumber\t\tDestAddr\t\tNextHopNumber\t\tNextHopAddr\t\tInterfaceAddr\t\tCost");

  //PRINT_LOG("ROUTING TABLE for Node: "<< m_current_node);
   // PRINT_LOG("ROUTING TABLE for Node: "<< ReverseLookup(m_mainAddress));
 // PRINT_LOG(m_current_node);

  /* NOTE: For purpose of autograding, you should invoke the following function for each
  routing table entry. The output format is indicated by parameter name and type.
  */
  //  checkNeighborTableEntry();
  if (m_neighbors.size() == 0){
    PRINT_LOG(m_neighbors.size());
    return;}
  
  PRINT_LOG(m_routingTable.size());
  for (auto itr = m_routingTable.begin(); itr != m_routingTable.end(); itr++){
    uint32_t dest_node_num = itr->first;
    RoutingTableEntry entry = itr->second;
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

void LSRoutingProtocol::ProcessHelloReq(LSMessage lsMessage){  
  //Send Hello Reply Response
 // PRINT_LOG("enters HelloReq");
  std::string helloMessage = "HELLO_REPLY";
  int m_maxTTL = 1;
  LSMessage helloRsp = LSMessage(LSMessage::HELLO_RSP, lsMessage.GetSequenceNumber(), m_maxTTL, m_mainAddress); 
  // helloRsp.SetHelloRsp(lsMessage.GetOriginatorAddress(), lsMessage.GetHelloReq().helloMessage);
  helloRsp.SetHelloRsp(lsMessage.GetOriginatorAddress(), helloMessage);
  Ptr<Packet> packet = Create<Packet>();
  packet->AddHeader(helloRsp);
  BroadcastPacket(packet); 
  //PRINT_LOG(lsMessage);
  //PRINT_LOG("finishes HelloReq");
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

void LSRoutingProtocol::ProcessHelloRsp(LSMessage lsMessage, Ipv4Address interfaceAd){
   // Check destination address
   //PRINT_LOG("enters HelloRSP"); 
   
  if (IsOwnAddress(lsMessage.GetHelloRsp().destinationAddress)){
  //process the message
  //address of the node whose neighbours are being discovered
   Ipv4Address m_node = lsMessage.GetHelloRsp().destinationAddress;  
   uint32_t current_node;
   std::istringstream sin(ReverseLookup(m_node));
   sin >> current_node;
  // PRINT_LOG("LINE 528");
  // PRINT_LOG(current_node);
  // PRINT_LOG(ReverseLookup(m_mainAddress));
  // m_current_node = current_node;

   //address of the neighbour node of m_node above
   Ipv4Address neighbor_discovered = lsMessage.GetOriginatorAddress();
   std::string neighbourNumStr = ReverseLookup(lsMessage.GetOriginatorAddress());
   uint32_t neighborNum;
   std::istringstream s(neighbourNumStr);
   s >> neighborNum;
   NeighborTableEntry neighbourEntry;
   //neighbourEntry.nodeNumber = neighborNum;
   neighbourEntry.neighborAddr = neighbor_discovered;
   neighbourEntry.t_stamp = Simulator::Now();
   neighbourEntry.interfaceAddr = interfaceAd;

   //PRINT_LOG(neighbourEntry.neighborAddr);
   //PRINT_LOG(neighbourEntry.t_stamp);
   //PRINT_LOG(neighbourEntry.interfaceAddr);

   std::map<uint32_t, NeighborTableEntry>::iterator iter;
   iter = m_neighbors.find(neighborNum);
   if (iter == m_neighbors.end())
   //the current node is not found in the map and is added
    {  
      m_neighbors.insert({neighborNum, neighbourEntry});
    }
   else
    {
        iter->second = neighbourEntry;             
    }
  }
  //LSAdvertise();
 // PRINT_LOG(m_current_node);
 // PRINT_LOG(m_neighbors.size());
 // PRINT_LOG("leaves HelloRSP"); 
  
}

void LSRoutingProtocol::AuditNeighbors()
{
 // PRINT_LOG("enteryingAuditNeighbors");
  m_neighborTimeout = Seconds(5.0);
  std::map<uint32_t, NeighborTableEntry>::iterator iter; 

  for (iter = m_neighbors.begin(); iter != m_neighbors.end();)
  {
    NeighborTableEntry neighbor_entry = iter->second;
    
        if (neighbor_entry.t_stamp.GetMilliSeconds() + m_neighborTimeout.GetMilliSeconds() <= Simulator::Now().GetMilliSeconds())
        //if (neighbor_entries[i].t_stamp.GetMilliSeconds() + 10000 <= Simulator::Now().GetMilliSeconds())
        {
            m_neighbors.erase(iter++);           
        }
        else
        {
          ++iter; 
        }     
  }
  //LSAdvertise();
  BroadcastHello();
  // Rechedule timer //TODO--look into m_auditPingsTimer
 LSAdvertise();
  m_auditNeighborsTimer.Schedule(Seconds(5));
   //PRINT_LOG("LeavingAuditNeighbors");
}

void LSRoutingProtocol::BroadcastHello()
{
  //PRINT_LOG("enters BroadcastHello");
  std::string helloMessage = "HELLO";
  int m_maxTTL = 1;
  uint32_t sequenceNumber = GetNextSequenceNumber();
  Ptr<Packet> pkt = Create<Packet>();
  LSMessage lsMessage = LSMessage(LSMessage::HELLO_REQ, sequenceNumber, m_maxTTL, m_mainAddress);
  lsMessage.SetHelloReq(Ipv4Address::GetAny(), helloMessage);
  pkt->AddHeader(lsMessage);
  BroadcastPacket(pkt);
  
  //PRINT_LOG("finished BroadcastHello");
  // Rechedule timer
  //m_Hello_Timer.Schedule(MilliSeconds(10000));
}


void LSRoutingProtocol::LSAdvertise()
{
  //PRINT_LOG("enters LSAdvertise");
  uint32_t sequenceNumber = GetNextSequenceNumber();
 
  neighborInfo n_nodes;
  uint32_t linkcost = 1;
  int m_maxTTL = 1;
 // PRINT_LOG(m_current_node);
  //PRINT_LOG(m_neighbors.size());
  for (auto itr = m_neighbors.begin(); itr != m_neighbors.end(); itr++){  
    uint32_t node_num = itr->first;
    //PRINT_LOG(node_num);
    n_nodes.push_back(std::make_pair(node_num, linkcost));   
  }
 /* PRINT_LOG(n_nodes.size());
   PRINT_LOG(n_nodes[0].first);
   PRINT_LOG(n_nodes[0].second);
     PRINT_LOG(n_nodes[1].first);
   PRINT_LOG(n_nodes[1].second);
  PRINT_LOG("line 613");*/

  Ptr<Packet> pkt = Create<Packet>();
  LSMessage lsMessage = LSMessage(LSMessage::LSA_m, sequenceNumber, m_maxTTL, m_mainAddress);
  lsMessage.SetLsA(n_nodes);
 
  pkt->AddHeader(lsMessage); 
  BroadcastPacket(pkt);
 
  /*neighborInfo neighborinfoEntry = lsMessage.GetLsA().lsaMessage;
  for (unsigned int i=0; i< neighborinfoEntry.size(); i++){
    PRINT_LOG(neighborinfoEntry[i].first);
    PRINT_LOG(neighborinfoEntry[i].second);
  }*/
  
}


void LSRoutingProtocol::ProcessLsp(LSMessage lsMessage, Ipv4Address interface_a)
{

// node from which current node is receiving the LSP
  //Ipv4Address fromNode = lsMessage.GetOriginatorAddress();
  std::string fromNodeNumstr = ReverseLookup(lsMessage.GetOriginatorAddress());
  uint32_t fromNodeNum;
  std::istringstream sin(fromNodeNumstr);
  sin >> fromNodeNum;
  //PRINT_LOG(fromNodeNum);
  /*
  PRINT_LOG(m_current_node);
  PRINT_LOG(fromNodeNum);
  neighborInfo neighborinfoEntry = lsMessage.GetLsA().lsaMessage;
  for (unsigned int i=0; i< neighborinfoEntry.size(); i++){
    PRINT_LOG(neighborinfoEntry[i].first);
    PRINT_LOG(neighborinfoEntry[i].second);}
    */

//sequence number of the message
  uint32_t seqNum = lsMessage.GetSequenceNumber();
 // PRINT_LOG(seqNum);

//check if LSP is from a new node that is not in the m_validLSP 
  std::map<uint32_t, LSPneighbors>::iterator iter;
  iter = m_validLSP.find(fromNodeNum);
  if (iter == m_validLSP.end())
  {// the LSP is from a new node and is not in the m_validLSP
  LSPneighbors lspEntry;
  lspEntry.seqNumber = seqNum;
  lspEntry.interfaceAd = interface_a;
  lspEntry.neighbornodeandCost = lsMessage.GetLsA().lsaMessage;  //check if this is working right
  m_validLSP.insert({fromNodeNum, lspEntry});
  }

  else
  {// check for the sequence number 
  uint32_t storedSeqNum = iter->second.seqNumber;
    if (storedSeqNum >= seqNum)
     {return;
     }   
  //update the neighborinfo for a node who sent LSA
  LSPneighbors lspEntry;
  lspEntry.seqNumber = seqNum;
  lspEntry.neighbornodeandCost = lsMessage.GetLsA().lsaMessage;
  iter->second = lspEntry;  
   }
  //PRINT_LOG(m_validLSP.size());
  /*
  for (auto j= m_validLSP.begin(); j !=m_validLSP.end(); ++j){
    PRINT_LOG(j->first);
    LSPneighbors neighbornodeandCentry = j->second;
     std::vector <std::pair<uint32_t, uint32_t>> neighbornodeandC = neighbornodeandCentry.neighbornodeandCost;
    for (unsigned int u = 0; u<neighbornodeandC.size(); u++){
      PRINT_LOG("nodenumber:");
      PRINT_LOG(neighbornodeandC[u].first);
      PRINT_LOG("cost:");
      PRINT_LOG(neighbornodeandC[u].second);
    }
   // PRINT_LOG(j->second.neighbornodeandCost.first);
    //PRINT_LOG(j->second.neighbornodeandCost.second);
    //PRINT_LOG(m_validLSP[j].second.first);
  }*/

  Dijkstra();

  //flood the message
  Ptr<Packet> pkt = Create<Packet>();
  lsMessage.SetTTL(lsMessage.GetTTL() - 1);
  pkt->AddHeader(lsMessage);
  //floodLSA(pkt, interface_a);
  BroadcastPacket(pkt);
  //PRINT_LOG("leaves ProcessLsp");  
}

void LSRoutingProtocol::floodLSA(Ptr<Packet> packet, Ipv4Address fromNode)
{
 for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator i = m_socketAddresses.begin();
       i != m_socketAddresses.end(); i++)
  {
    Ptr<Packet> pkt = packet->Copy();
    Ipv4Address broadcastAddr = i->second.GetLocal().GetSubnetDirectedBroadcast(i->second.GetMask());
    if (broadcastAddr != fromNode){
    i->first->SendTo(pkt, 0, InetSocketAddress(broadcastAddr, LS_PORT_NUMBER));
    }
  }
}

void LSRoutingProtocol::Dijkstra()
{ 
  //clear routing table first
  m_routingTable.clear();
  //initialize the confirmed list with an entry for myself with a cost of 0
  std::vector<std::pair<uint32_t, std::pair<uint32_t, uint32_t>>> confirmed;  //destnode, cost, next hop
  std::vector<std::pair<uint32_t, std::pair<uint32_t, uint32_t>>> tentative;

  std::string nodetoaddstr = ReverseLookup(m_mainAddress);
  uint32_t nodetoadd;
  std::istringstream sin(nodetoaddstr);
  sin >> nodetoadd;

  //uint32_t nodetoadd = m_mainAddress;
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
  //bool contExec = true;
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

      //check if p is not in the confirmed or tentative list
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
  
  //find the entry from the Tentative list with the lowest cost and move it to the Confirmed List
  //set that node as next node and update cost_to_node
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
  //nextNode = toConfirmed.first;
  nodetoadd = toConfirmed.first;
  viaNode = toConfirmed.second.second;
  prev_cost = toConfirmed.second.first;
  
  

//iterate through each entry in the confirmed and add it to m_routingTable
//dest node no, dest addr, next hop number,  next hop Addr, next hop interface addr, cost

  for (unsigned int i =1; i < confirmed.size(); i++){
    uint32_t dest_node = confirmed[i].first;  
    Ipv4Address dest_addr = ResolveNodeIpAddress(dest_node);
    uint32_t next_hop = confirmed[i].second.second;
    Ipv4Address next_hopAddr = ResolveNodeIpAddress(next_hop);
    uint32_t cost = confirmed[i].second.first;
    // get next_hop Ipv4 address and interface Addr
    std::map<uint32_t, NeighborTableEntry>::iterator it;   
    it = m_neighbors.find(next_hop);
    /*
    if (it == m_neighbors.end()){
      PRINT_LOG("CANNOT FIND ON 821");
    }*/
    //PRINT_LOG("line 823");
    //PRINT_LOG(m_current_node);
   // PRINT_LOG(next_hop);
    Ipv4Address interAddr =  it->second.interfaceAddr;
   // PRINT_LOG(interAddr);
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
 // m_Hello_Timer.SetFunction(&LSRoutingProtocol::BroadcastHello(), this);
  m_ipv4 = ipv4;
  m_staticRouting->SetIpv4(m_ipv4);
}
