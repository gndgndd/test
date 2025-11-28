/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010 University of Pennsylvania
 *
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

#include "penn-chord.h"

#include "ns3/inet-socket-address.h"
#include "ns3/random-variable-stream.h"
#include "ns3/penn-key-helper.h"
#include "ns3/grader-logs.h"
#include <openssl/sha.h>

using namespace ns3;

TypeId
PennChord::GetTypeId ()
{
  static TypeId tid
      = TypeId ("PennChord")
            .SetParent<PennApplication> ()
            .AddConstructor<PennChord> ()
            .AddAttribute ("AppPort", "Listening port for Application", UintegerValue (10001),
                           MakeUintegerAccessor (&PennChord::m_appPort), MakeUintegerChecker<uint16_t> ())
            .AddAttribute ("PingTimeout", "Timeout value for PING_REQ in milliseconds", TimeValue (MilliSeconds (2000)),
                           MakeTimeAccessor (&PennChord::m_pingTimeout), MakeTimeChecker ())
  ;
  return tid;
}

PennChord::PennChord ()
    : m_auditPingsTimer (Timer::CANCEL_ON_DESTROY)
{
  Ptr<UniformRandomVariable> m_uniformRandomVariable = CreateObject<UniformRandomVariable> ();
  m_currentTransactionId = m_uniformRandomVariable->GetValue (0x00000000, 0xFFFFFFFF);

  // set finger table size and resize actual finger table
  m_fingerTableSize = 32;
  m_fingerTable.resize(m_fingerTableSize);

  // set nextFingerToFix to first entry and mark fingerTable as not initialized yet
  m_nextFingerToFix = 0;
  m_fingerTableInitialized = false;
}

PennChord::~PennChord ()
{
  if (m_numLookups > 0)
  {
    std::string nodeId = ReverseLookup(GetLocalAddress());
    double avgHops = static_cast<double>(m_totalHops) / static_cast<double>(m_numLookups);

    CHORD_LOG("Average hops for lookups on node " << nodeId << " is " << avgHops);
    GraderLogs::AverageHopCount(nodeId, m_numLookups, m_totalHops);
  }
}

void
PennChord::DoDispose ()
{
  StopApplication ();
  PennApplication::DoDispose ();
}

void
PennChord::StartApplication (void)
{
  std::cout << "PennChord::StartApplication()!!!!!" << std::endl;
  if (m_socket == 0)
    { 
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny(), m_appPort);
      m_socket->Bind (local);
      m_socket->SetRecvCallback (MakeCallback (&PennChord::RecvMessage, this));
    }  
  
  m_nodeHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  m_predecessor = Ipv4Address::GetAny();
  // m_successor = GetLocalAddress(); // self is its own successor if we create the ring

  // Configure timers
  m_auditPingsTimer.SetFunction (&PennChord::AuditPings, this);
  m_auditPingsTimer.Schedule (m_pingTimeout);

  m_stabilizeTimer.SetFunction(&PennChord::Stabilize, this);
  m_stabilizeTimer.Schedule(Seconds(2));

  // configure and start finger table timer
  m_fixFingerTimer.SetFunction(&PennChord::FixFingerTable, this);
  m_fixFingerTimer.Schedule(Seconds(1));
}

void
PennChord::StopApplication (void)
{
  // Close socket
  if (m_socket)
    {
      m_socket->Close ();
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
      m_socket = 0;
    }

  // Cancel timers and trackers
  m_auditPingsTimer.Cancel ();
  m_stabilizeTimer.Cancel ();
  m_fixFingerTimer.Cancel ();

  m_pingTracker.clear ();
  m_pendingFingers.clear();
}

void
PennChord::ProcessCommand (std::vector<std::string> tokens)
{
  std::vector<std::string>::iterator iterator = tokens.begin();
  // tokens for 0 PENNSEARCH CHORD JOIN 0 = [JOIN, 0]
  std::string command = *iterator;

  if (command == "JOIN")
    {
      iterator++;
      std::string landmarkNode = *iterator;

      std::string currentNode = GetNodeId();

      if (currentNode == landmarkNode)
        {
          ChordCreate();
        }
      else
        {
          Ipv4Address landmarkIp = ResolveNodeIpAddress(landmarkNode);
          Join(landmarkIp);
        }
    }
  else if (command == "RINGSTATE")
    {
      RingState();
    }
  else if (command == "LEAVE")
    {
      Leave();
    } 
}

void
PennChord::SendPing (Ipv4Address destAddress, std::string pingMessage)
{
  if (destAddress != Ipv4Address::GetAny ())
    {
      uint32_t transactionId = GetNextTransactionId ();
      CHORD_LOG ("Sending PING_REQ to Node: " << ReverseLookup(destAddress)
                 << " IP: " << destAddress
                 << " Message: " << pingMessage
                 << " transactionId: " << transactionId);
      Ptr<PingRequest> pingRequest = Create<PingRequest> (transactionId, Simulator::Now(), destAddress, pingMessage);
      // Add to ping-tracker
      m_pingTracker.insert (std::make_pair (transactionId, pingRequest));
      Ptr<Packet> packet = Create<Packet> ();
      PennChordMessage message = PennChordMessage (PennChordMessage::PING_REQ, transactionId);
      message.SetPingReq (pingMessage);
      packet->AddHeader (message);
      m_socket->SendTo (packet, 0 , InetSocketAddress (destAddress, m_appPort));
    }
  else
    {
      // Report failure   
      m_pingFailureFn (destAddress, pingMessage);
    }
}

void
PennChord::RecvMessage (Ptr<Socket> socket)
{
  Address sourceAddr;
  Ptr<Packet> packet = socket->RecvFrom (sourceAddr);
  InetSocketAddress inetSocketAddr = InetSocketAddress::ConvertFrom (sourceAddr);
  Ipv4Address sourceAddress = inetSocketAddr.GetIpv4 ();
  uint16_t sourcePort = inetSocketAddr.GetPort ();
  PennChordMessage message;
  packet->RemoveHeader (message);

  switch (message.GetMessageType ())
    {
      case PennChordMessage::PING_REQ:
        ProcessPingReq (message, sourceAddress, sourcePort);
        break;
      case PennChordMessage::PING_RSP:
        ProcessPingRsp (message, sourceAddress, sourcePort);
        break;
      case PennChordMessage::FIND_SUCCESSOR_REQ:
        ProcessFindSuccessorReq(message);
        break;
      case PennChordMessage::FIND_SUCCESSOR_RSP:
        ProcessFindSuccessorRsp(message);
        break;
      case PennChordMessage::STABILIZE_REQ:
        ProcessStabilizeReq(message);
        break;
      case PennChordMessage::STABILIZE_RSP:
        ProcessStabilizeRsp(message);
        break;
      case PennChordMessage::NOTIFY_PKT:
        ProcessNotifcationPkt(message);
        break;
      case PennChordMessage::RINGSTATE_PKT:
        ProcessRingStatePtk(message);
        break;
      case PennChordMessage::LEAVE_SUCCESSOR:
        ProcessLeaveSuccessor(message);
        break;
      case PennChordMessage::LEAVE_PREDECESSOR:
        ProcessLeavePredecessor(message);
        break;
      default:
        ERROR_LOG ("Unknown Message Type!");
        break;
    }
}

void
PennChord::ProcessPingReq (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort)
{
  std::string fromNode = ReverseLookup (sourceAddress);
  CHORD_LOG ("Received PING_REQ, From Node: " << fromNode
             << ", Message: " << message.GetPingReq().pingMessage);

  PennChordMessage resp = PennChordMessage (PennChordMessage::PING_RSP,
                                            message.GetTransactionId());
  resp.SetPingRsp (message.GetPingReq().pingMessage);
  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (resp);
  m_socket->SendTo (packet, 0 , InetSocketAddress (sourceAddress, sourcePort));

  m_pingRecvFn (sourceAddress, message.GetPingReq().pingMessage);
}

void
PennChord::ProcessPingRsp (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort)
{
  std::map<uint32_t, Ptr<PingRequest> >::iterator iter;
  iter = m_pingTracker.find (message.GetTransactionId ());
  if (iter != m_pingTracker.end ())
    {
      std::string fromNode = ReverseLookup (sourceAddress);
      CHORD_LOG ("Received PING_RSP, From Node: " << fromNode
                 << ", Message: " << message.GetPingRsp().pingMessage);
      m_pingTracker.erase (iter);
      m_pingSuccessFn (sourceAddress, message.GetPingRsp().pingMessage);
    }
  else
    {
      DEBUG_LOG ("Received invalid PING_RSP!");
    }
}

void
PennChord::AuditPings ()
{
  std::map<uint32_t, Ptr<PingRequest> >::iterator iter;
  for (iter = m_pingTracker.begin () ; iter != m_pingTracker.end();)
    {
      Ptr<PingRequest> pingRequest = iter->second;
      if (pingRequest->GetTimestamp().GetMilliSeconds()
            + m_pingTimeout.GetMilliSeconds()
          <= Simulator::Now().GetMilliSeconds())
        {
          DEBUG_LOG ("Ping expired. Message: " << pingRequest->GetPingMessage ()
                    << " Timestamp: " << pingRequest->GetTimestamp().GetMilliSeconds ()
                    << " CurrentTime: " << Simulator::Now().GetMilliSeconds ());
          m_pingTracker.erase (iter++);
          m_pingFailureFn (pingRequest->GetDestinationAddress(),
                           pingRequest->GetPingMessage ());
        }
      else
        {
          ++iter;
        }
    }
  m_auditPingsTimer.Schedule (m_pingTimeout); 
}

/** CHORD LOGIC **/

void
PennChord::ChordCreate()
{
  Ipv4Address selfIp = GetLocalAddress();
  m_predecessor = Ipv4Address::GetAny();
  m_successor   = selfIp;
  m_nodeHash    = PennKeyHelper::CreateShaKey(selfIp);
  
  InitFingerTable();
}

void
PennChord::Join(Ipv4Address landmark)
{
  Ipv4Address selfIp = GetLocalAddress();
  uint32_t myId      = PennKeyHelper::CreateShaKey(selfIp);
  m_successor        = GetLocalAddress();
  m_predecessor      = Ipv4Address::GetAny();
  
  uint32_t transactionId = GetNextTransactionId();

  if (m_leftChord)
  {
    m_joinTransactionId = transactionId;
    m_leftChord = false;
  }

  PennChordMessage msg (PennChordMessage::FIND_SUCCESSOR_REQ, transactionId);
  msg.SetFindSuccessorReq(myId, selfIp);

  Ptr<Packet> pkt = Create<Packet>();
  pkt->AddHeader(msg);
  m_socket->SendTo(pkt, 0, InetSocketAddress(landmark, m_appPort));
}

void
PennChord::ProcessFindSuccessorReq(PennChordMessage message)
{
  if (message.GetIsLookup())
  {
    CHORD_LOG("Received FIND_SUCCESSOR_REQ for id"
              << message.GetFindSuccessorReq().idToFind);
    GraderLogs::GetLookupIssueLogStr(m_nodeHash,
                                     message.GetFindSuccessorReq().idToFind);
  }

  PennChordMessage::FindSuccessorReq req = message.GetFindSuccessorReq();
  uint32_t idToFind   = req.idToFind;
  Ipv4Address requestorIp = req.requestorIp;

  uint32_t selfId      = m_nodeHash;
  uint32_t successorId = PennKeyHelper::CreateShaKey(m_successor);

  bool replyTriggered = false;

  if (IsInBetween(selfId, idToFind, successorId) || selfId == successorId)
  {
    replyTriggered = true;
  }

  if (replyTriggered)
  {
    uint32_t transactionId = message.GetTransactionId();
    PennChordMessage resp (PennChordMessage::FIND_SUCCESSOR_RSP, transactionId);

    resp.SetIsLookup(message.GetIsLookup());
    resp.SetFindSuccessorRsp(m_successor);

    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(resp);
    m_socket->SendTo(packet, 0, InetSocketAddress(requestorIp, m_appPort));
  }
  else
  {
    if (message.GetIsLookup())
    {
      uint32_t tx = message.GetTransactionId();
      m_hopsPerLookup[tx] += 1;
      GraderLogs::GetLookupForwardingLogStr(
        m_nodeHash,
        ReverseLookup(m_successor),
        PennKeyHelper::CreateShaKey(m_successor),
        idToFind);
    }

    int fingerTableIndex = ClosestPrecedingFinger(idToFind);
    Ipv4Address nextHopIp =
      (fingerTableIndex != -1) ? m_fingerTable[fingerTableIndex].finger_ip
                               : m_successor;

    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(message);
    m_socket->SendTo(packet, 0, InetSocketAddress(nextHopIp, m_appPort));
  }
}

void
PennChord::ProcessFindSuccessorRsp(PennChordMessage message)
{
  if (message.GetIsLookup())
  {
    uint32_t tx = message.GetTransactionId();
    auto it = m_hopsPerLookup.find(tx);
    if (it != m_hopsPerLookup.end())
    {
      m_totalHops += it->second;
      m_numLookups++;
      m_hopsPerLookup.erase(it);
    }
    CHORD_LOG("Received FIND_SUCCESSOR_RSP for id"
              << message.GetFindSuccessorRsp().successorIp);
  }

  PennChordMessage::FindSuccessorRsp rsp = message.GetFindSuccessorRsp();
  Ipv4Address successorIp = rsp.successorIp;

  uint32_t tx = message.GetTransactionId();

  auto txId = m_pendingFingers.find(tx);
  
  if (txId != m_pendingFingers.end())
  {
    uint32_t idx = txId->second;
    m_fingerTable[idx].finger_ip = successorIp;
    m_fingerTable[idx].finger_id = PennKeyHelper::CreateShaKey(successorIp);
    m_pendingFingers.erase(txId);
  }
  else
  {
    auto lookupIt = m_pendingLookups.find(tx);
    if (lookupIt != m_pendingLookups.end())
    {
      if (!m_lookupSuccessFn.IsNull())
      {
        m_lookupSuccessFn(tx, successorIp);
      }
    }
    else
    {
      m_successor = successorIp;
      if (tx == m_joinTransactionId && !m_rejoinCallback.IsNull())
      {
        Simulator::Schedule(MilliSeconds(1500),
                            &PennChord::TriggerRejoinCallback,
                            this);
      }
    }
  }
}

void
PennChord::Stabilize()
{
  Ipv4Address sender   = GetLocalAddress();
  Ipv4Address receiver = m_successor;

  uint32_t transactionId = GetNextTransactionId();
  PennChordMessage msg (PennChordMessage::STABILIZE_REQ, transactionId);
  msg.SetStabilizeReq(sender, receiver);

  Ptr<Packet> pkt = Create<Packet>();
  pkt->AddHeader(msg);
  m_socket->SendTo(pkt, 0, InetSocketAddress(receiver, m_appPort));

  m_stabilizeTimer.Schedule(MilliSeconds(750));
}

bool
PennChord::IsInBetween(uint32_t start, uint32_t target, uint32_t end) const
{
  if (start < end)
  {
    return (start < target && target < end);
  }
  else if (start > end)
  {
    return (target > start || target < end);
  }
  else if (start == end)
  {
    return true;
  }
  else
  {
    return false;
  }
}

void
PennChord::ProcessStabilizeReq(PennChordMessage message)
{
  PennChordMessage::StabilizeReq req = message.GetStabilizeReq();
  Ipv4Address senderIp    = req.sender;
  Ipv4Address nodeToNotify = req.receiver;

  uint32_t n        = PennKeyHelper::CreateShaKey(senderIp);
  uint32_t successor = m_nodeHash;
  uint32_t x        = PennKeyHelper::CreateShaKey(m_predecessor);

  if (IsInBetween(n, x, successor) &&
      x != PennKeyHelper::CreateShaKey(Ipv4Address::GetAny()))
  {
    if (senderIp != GetLocalAddress() &&
        m_predecessor != Ipv4Address::GetAny())
    {
      nodeToNotify = m_predecessor;
      Ipv4Address updated_successor = m_predecessor;

      uint32_t transactionId = GetNextTransactionId();
      PennChordMessage msg (PennChordMessage::STABILIZE_RSP, transactionId);
      msg.SetStabilizeRsp(updated_successor);
      
      Ptr<Packet> pkt = Create<Packet>();
      pkt->AddHeader(msg);
      m_socket->SendTo(pkt, 0, InetSocketAddress(senderIp, m_appPort));
    }
    else
    {
      m_successor = m_predecessor;
    }
  }

  Ipv4Address newPredecessor = senderIp;
  uint32_t transactionId = GetNextTransactionId();
  PennChordMessage msg (PennChordMessage::NOTIFY_PKT, transactionId);
  msg.SetNotifyPkt(newPredecessor);
  Ptr<Packet> pkt = Create<Packet>();
  pkt->AddHeader(msg);
  m_socket->SendTo(pkt, 0, InetSocketAddress(nodeToNotify, m_appPort));
}

void
PennChord::ProcessStabilizeRsp(PennChordMessage message)
{
  PennChordMessage::StabilizeRsp msg = message.GetStabilizeRsp();
  Ipv4Address newSuccessor = msg.sender;

  if (newSuccessor != Ipv4Address::GetAny() &&
     (m_successor != newSuccessor || m_successor == Ipv4Address::GetAny()))
  {
    m_successor = newSuccessor;
  }
}

void
PennChord::ProcessNotifcationPkt(PennChordMessage message)
{
  PennChordMessage::NotifyPkt notification = message.GetNotifyPkt();
  Ipv4Address updatePredecessor = notification.newPredecessor;
  uint32_t nPrime = PennKeyHelper::CreateShaKey(updatePredecessor);

  if (m_predecessor == Ipv4Address::GetAny())
  {
    m_predecessor = updatePredecessor;
  }
  else
  {
    uint32_t currentPredHash = PennKeyHelper::CreateShaKey(m_predecessor);
    if (IsInBetween(currentPredHash, nPrime, m_nodeHash) &&
        currentPredHash != PennKeyHelper::CreateShaKey(updatePredecessor))
    {
      m_predecessor = updatePredecessor;
    }
  }
}

void
PennChord::RingState()
{
  Ipv4Address localIp = GetLocalAddress();
  std::string localId = ReverseLookup(localIp);
  uint32_t localHash  = m_nodeHash;

  Ipv4Address predIp = m_predecessor;
  std::string predId = ReverseLookup(predIp);
  uint32_t predHash  = PennKeyHelper::CreateShaKey(predIp);

  Ipv4Address succIp = m_successor;
  std::string succId = ReverseLookup(succIp);
  uint32_t succHash  = PennKeyHelper::CreateShaKey(succIp);

  GraderLogs::RingState(localIp, localId, localHash,
                        predIp,  predId,  predHash,
                        succIp,  succId,  succHash);

  uint32_t transactionId = GetNextTransactionId();
  PennChordMessage msg (PennChordMessage::RINGSTATE_PKT, transactionId);
  msg.SetRingstatePkt(localIp);

  Ptr<Packet> pkt = Create<Packet>();
  pkt->AddHeader(msg);
  m_socket->SendTo(pkt, 0, InetSocketAddress(succIp, m_appPort));
}

void
PennChord::ProcessRingStatePtk(PennChordMessage message)
{
  PennChordMessage::RingstatePkt mssg = message.GetRingstatePkt();
  Ipv4Address ringStateEnd = mssg.endRingState; 

  if (ringStateEnd == GetLocalAddress())
  {
    GraderLogs::EndOfRingState();
  } 
  else 
  {
    Ipv4Address localIp = GetLocalAddress();
    std::string localId = ReverseLookup(localIp);
    uint32_t localHash  = m_nodeHash;

    Ipv4Address predIp = m_predecessor;
    std::string predId = ReverseLookup(predIp);
    uint32_t predHash  = PennKeyHelper::CreateShaKey(predIp);

    Ipv4Address succIp = m_successor;
    std::string succId = ReverseLookup(succIp);
    uint32_t succHash  = PennKeyHelper::CreateShaKey(succIp);

    GraderLogs::RingState(localIp, localId, localHash,
                          predIp,  predId,  predHash,
                          succIp,  succId,  succHash);

    uint32_t transactionId = GetNextTransactionId();
    PennChordMessage msg (PennChordMessage::RINGSTATE_PKT, transactionId);
    msg.SetRingstatePkt(ringStateEnd);
    Ptr<Packet> pkt = Create<Packet>();
    pkt->AddHeader(msg);
    m_socket->SendTo(pkt, 0, InetSocketAddress(succIp, m_appPort));
  }
}

void
PennChord::Leave()
{
  uint32_t transactionId = GetNextTransactionId();
  PennChordMessage msg (PennChordMessage::LEAVE_SUCCESSOR, transactionId);
  msg.SetLeaveSuccessor(GetLocalAddress(), m_predecessor);
  Ptr<Packet> pkt = Create<Packet>();
  pkt->AddHeader(msg);
  m_socket->SendTo(pkt, 0, InetSocketAddress(m_successor, m_appPort));

  uint32_t transactionId2 = GetNextTransactionId();
  PennChordMessage msg2 (PennChordMessage::LEAVE_PREDECESSOR, transactionId2);
  msg2.SetLeavePredecessor(GetLocalAddress(), m_successor);
  Ptr<Packet> pkt2 = Create<Packet>();
  pkt2->AddHeader(msg2);
  m_socket->SendTo(pkt2, 0, InetSocketAddress(m_predecessor, m_appPort));

  if (!m_leaveCallback.IsNull()){
    m_leaveCallback(m_successor);
  }

  m_predecessor = Ipv4Address::GetAny();
  m_successor   = Ipv4Address::GetAny();
  m_leftChord   = true;
}

void
PennChord::ProcessLeaveSuccessor(PennChordMessage message)
{
  PennChordMessage::LeaveSuccessor msg = message.GetLeaveSuccessor();
  Ipv4Address newPredecessor = msg.newPred;
  Ipv4Address sender         = msg.sender;

  if (sender == m_predecessor) {
    m_predecessor = newPredecessor;
  }
}

void
PennChord::ProcessLeavePredecessor(PennChordMessage message)
{
  PennChordMessage::LeavePredecessor msg = message.GetLeavePredecessor();
  Ipv4Address newSuccessor = msg.newSucc;
  Ipv4Address sender       = msg.sender;

  if (sender == m_successor) {
    m_successor = newSuccessor;
  }
}

/* Finger Table Methods */

void
PennChord::InitFingerTable()
{
  if (m_fingerTableInitialized)
  {
    return;
  }

  for (uint32_t i = 0; i < m_fingerTableSize; i++)
  {
    uint32_t start = m_nodeHash + (1U << i);
    m_fingerTable[i].start     = start;
    m_fingerTable[i].finger_ip = Ipv4Address::GetAny();
    m_fingerTable[i].finger_id = m_nodeHash;
  }

  m_nextFingerToFix = 0;
  m_fingerTableInitialized = true;
}

void
PennChord::FixFingerTable()
{
  if (!m_fingerTableInitialized)
    {
      InitFingerTable();
      m_fixFingerTimer.Schedule(MilliSeconds(100));
      return;
    }

  uint32_t nextFinger = m_nextFingerToFix;

  if (nextFinger >= m_fingerTableSize)
    {
      m_nextFingerToFix = 0;
      nextFinger = 0;
    }
  
  uint32_t target = m_fingerTable[nextFinger].start;
  uint32_t tx = GetNextTransactionId();

  m_pendingFingers[tx] = nextFinger;

  PennChordMessage msg(PennChordMessage::FIND_SUCCESSOR_REQ, tx);
  msg.SetFindSuccessorReq(target, GetLocalAddress());
  Ptr<Packet> pkt = Create<Packet>();
  pkt->AddHeader(msg);
  m_socket->SendTo(pkt, 0, InetSocketAddress(m_successor, m_appPort));

  m_nextFingerToFix = (nextFinger + 1) % m_fingerTableSize;
  m_fixFingerTimer.Schedule(MilliSeconds(100));
}

int
PennChord::ClosestPrecedingFinger(uint32_t idToFind) const
{
  for (int i = int(m_fingerTableSize) - 1; i >= 0; i--)
    {
      if (m_fingerTable[i].finger_ip == Ipv4Address::GetAny())
        {
          continue;
        }

      uint32_t fid = m_fingerTable[i].finger_id;

      if (IsInBetween(m_nodeHash, fid, idToFind))
        {
          return i;
        }
    }
  return -1;
}

void
PennChord::ChordLookup(uint32_t transactionId, uint32_t idToFind)
{
  m_pendingLookups[transactionId] = idToFind;

  PennChordMessage msg (PennChordMessage::FIND_SUCCESSOR_REQ, transactionId);

  msg.SetIsLookup(true);
  m_hopsPerLookup[transactionId] = 0;

  msg.SetFindSuccessorReq(idToFind, GetLocalAddress());
  Ptr<Packet> pkt = Create<Packet>();
  pkt->AddHeader(msg);

  Ipv4Address nextHop;
  
  int fingerIndex = ClosestPrecedingFinger(idToFind);
  if (fingerIndex >= 0)
  {
    nextHop = m_fingerTable[fingerIndex].finger_ip;
  }
  else
  {
    nextHop = m_successor;
  }

  if (msg.GetIsLookup())
  {
    m_hopsPerLookup[transactionId] += 1;
  }

  m_socket->SendTo(pkt, 0, InetSocketAddress(nextHop, m_appPort));

  CHORD_LOG(GraderLogs::GetLookupIssueLogStr(m_nodeHash, idToFind));
}

/** CALLBACKS */

void 
PennChord::SetLookUpCallback(Callback<void, Ipv4Address, uint32_t> lookupCb)
{
  m_lookupCallback = lookupCb;
}

uint32_t
PennChord::GetNextTransactionId ()
{
  return m_currentTransactionId++;
}

void
PennChord::StopChord ()
{
  StopApplication ();
}

void
PennChord::SetPingSuccessCallback (Callback <void, Ipv4Address, std::string> pingSuccessFn)
{
  m_pingSuccessFn = pingSuccessFn;
}

void
PennChord::SetPingFailureCallback (Callback <void, Ipv4Address, std::string> pingFailureFn)
{
  m_pingFailureFn = pingFailureFn;
}

void
PennChord::SetPingRecvCallback (Callback <void, Ipv4Address, std::string> pingRecvFn)
{
  m_pingRecvFn = pingRecvFn;
}

void
PennChord::SetLookupSuccessCallback(Callback <void, uint32_t, Ipv4Address> lookupSuccessFn)
{
  m_lookupSuccessFn = lookupSuccessFn;
}

void
PennChord::SetLookupFailureCallback(Callback <void, uint32_t> lookupFailureFn)
{
  m_lookupFailureFn = lookupFailureFn;
}

void
PennChord::SetLeaveCallback(Callback<void, Ipv4Address> leaveCB)
{
  m_leaveCallback = leaveCB;
}

void
PennChord::SetRejoinCallback(Callback<void, Ipv4Address> rejoinCB)
{
  m_rejoinCallback = rejoinCB;
}

void
PennChord::TriggerRejoinCallback()
{
  if (!m_rejoinCallback.IsNull()) {
    m_rejoinCallback(m_successor);
  }
}
