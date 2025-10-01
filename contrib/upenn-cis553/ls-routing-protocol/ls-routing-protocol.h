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

#ifndef LS_ROUTING_H
#define LS_ROUTING_H

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-static-routing.h"
#include "ns3/ipv4.h"
#include "ns3/node.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"
#include "ns3/timer.h"

#include "ns3/ls-message.h"
#include "ns3/penn-routing-protocol.h"
#include "ns3/ping-request.h"

#include <map>
#include <vector>

using namespace ns3;

class LSRoutingProtocol : public PennRoutingProtocol
{
public:
  static TypeId GetTypeId(void);

  LSRoutingProtocol();
  virtual ~LSRoutingProtocol();

  virtual void ProcessCommand(std::vector<std::string> tokens);

  virtual void SetMainInterface(uint32_t mainInterface);

  virtual void SetNodeAddressMap(std::map<uint32_t, Ipv4Address> nodeAddressMap);

  virtual void SetAddressNodeMap(std::map<Ipv4Address, uint32_t> addressNodeMap);

  // Message Handling
  void RecvLSMessage(Ptr<Socket> socket);
  void ProcessPingReq(LSMessage lsMessage);
  void ProcessPingRsp(LSMessage lsMessage);
  void ProcessHello(LSMessage lsMessage, Ipv4Address incomingInterface);
  void ProcessLSP(LSMessage lsMessage, Ipv4Address incomingInterface);

  // Periodic Tasks
  void CheckExpiredPings();
  void UpdateNetworkState();
  
  // From Ipv4RoutingProtocol
  virtual void PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit = Time::S) const;
  virtual Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr);
  virtual bool RouteInput(Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                          UnicastForwardCallback ucb, MulticastForwardCallback mcb, LocalDeliverCallback lcb,
                          ErrorCallback ecb);
  virtual void NotifyInterfaceUp(uint32_t interface);
  virtual void NotifyInterfaceDown(uint32_t interface);
  virtual void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address);
  virtual void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address);
  virtual void SetIpv4(Ptr<Ipv4> ipv4);
  void DoDispose();

private:
  void BroadcastPacket(Ptr<Packet> packet);
  virtual Ipv4Address ResolveNodeIpAddress(uint32_t nodeNumber);
  virtual std::string ReverseLookup(Ipv4Address ipv4Address);
  void DumpLSA();
  void DumpNeighbors();
  void DumpRoutingTable();
protected:
  virtual void DoInitialize(void);
  uint32_t GetNextSequenceNumber();
  bool IsOwnAddress(Ipv4Address originatorAddress);
  void CalculateRoutes();

private:
  std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_socketAddresses;
  Ptr<Socket> m_recvSocket;
  Ipv4Address m_mainAddress;
  Ptr<Ipv4StaticRouting> m_staticRouting;
  Ptr<Ipv4> m_ipv4;
  Time m_pingTimeout;
  Time m_updateInterval;
  uint8_t m_maxTTL;
  uint16_t m_lsPort;
  uint32_t m_currentSequenceNumber;
  std::map<uint32_t, Ipv4Address> m_nodeAddressMap;
  std::map<Ipv4Address, uint32_t> m_addressNodeMap;

  // Timers
  Timer m_pingAuditTimer;
  Timer m_updateTimer;

  // Ping tracker
  std::map<uint32_t, Ptr<PingRequest>> m_pingTracker;
  
  // New Data Structures
  struct LinkStateEntry {
    uint32_t sequenceNumber;
    std::vector<std::pair<uint32_t, uint32_t>> links;
    Ipv4Address incomingInterface;
  };

  struct LocalLinkEntry {
    Ipv4Address neighborAddress;
    Ipv4Address localInterface;
    Time lastSeen;
    uint32_t cost;
  };
  
  struct RouteEntry {
    Ipv4Address destinationAddr;
    uint32_t nextHopNodeId;
    Ipv4Address nextHopAddr;
    Ipv4Address outgoingInterface;
    uint32_t pathCost;
  };

  std::map<uint32_t, LocalLinkEntry> m_localLinks;
  std::map<uint32_t, LinkStateEntry> m_linkStateDatabase;
  std::map<uint32_t, RouteEntry> m_routingTable;
};
#endif
