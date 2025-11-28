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
#include "ns3/ipv4.h"
#include "ns3/random-variable-stream.h"
#include "ns3/penn-key-helper.h"
#include "ns3/grader-logs.h"
#include <openssl/sha.h>
#include <algorithm>   // for std::sort
#include <sstream>
#include <iomanip>
#include <vector>

// Static member initialization
std::map<Ipv4Address, Ipv4Address> PennChord::m_successorPredecessor;
std::set<Ipv4Address> PennChord::s_joined;

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("PennChord");

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
}

PennChord::~PennChord ()
{

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
      // std::cout << "reset m_socket to not null, now is " << m_socket << std::endl;
    }

  // Configure timers
  m_auditPingsTimer.SetFunction (&PennChord::AuditPings, this);
  // Start timers
  m_auditPingsTimer.Schedule (m_pingTimeout);
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

  // Cancel timers
  m_auditPingsTimer.Cancel ();

  m_pingTracker.clear ();
}

void
PennChord::ProcessCommand (std::vector<std::string> tokens)
{
  if (tokens.size() < 1)
    return;

  std::string command = tokens[0];

  // Log received command for debug visibility
  CHORD_LOG ("[ProcessCommand] Received command: " << command
             << " on node " <<  ReverseLookup(GetLocalAddress()));

  // Handle JOIN command
  if (command == "JOIN")
    {
      if (tokens.size() < 2)
        {
          ERROR_LOG("Insufficient JOIN params...");
          return;
        }
      std::cout << "[TEST] JOIN triggered on "
                << ReverseLookup(GetLocalAddress())
                << " reference node is "
                << tokens[1]
                << std::endl;
      if (ReverseLookup(GetLocalAddress()) == tokens[1])
      {
        CreateChord();
        return;
      }
      else
      {
        Ipv4Address referenceNode = ResolveNodeIpAddress(tokens[1]);
        JoinChord(referenceNode);
        return;
      }
    }
  // Handle STABILIZE command
  else if (command == "STABILIZE")
    {
      std::cout << "[TEST] STABILIZE triggered on "
                << ReverseLookup(GetLocalAddress()) << std::endl;
      Stabilize();
    }
  // Handle LEAVE command
  else if (command == "LEAVE")
    {
      std::cout << "[TEST] LEAVE triggered on "
                << ReverseLookup(GetLocalAddress()) << std::endl;
      LeaveChord();
    }
  // Handle RINGSTATE command
  else if (command == "RINGSTATE")
    {
      std::cout << "[TEST] RINGSTATE requested on "
                << ReverseLookup(GetLocalAddress()) << std::endl;
                Ringstate();
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
      //MS2A ADDITION: Handle Lookup Messages
      case PennChordMessage::LOOKUP_REQ:
        ProcessLookupReq(message, sourceAddress);
        break;
      case PennChordMessage::LOOKUP_FORWARD:
        ProcessLookupForward(message, sourceAddress);
        break;
      case PennChordMessage::LOOKUP_RSP:
        ProcessLookupRsp(message, sourceAddress);
        break;
      default:
        ERROR_LOG ("Unknown Message Type!");
        break;
    }
}

void
PennChord::ProcessPingReq (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort)
{
    // Use reverse lookup for ease of debug
    std::string fromNode = ReverseLookup (sourceAddress);
    CHORD_LOG ("Received PING_REQ, From Node: " << fromNode << ", Message: " << message.GetPingReq().pingMessage);
    // Send Ping Response
    PennChordMessage resp = PennChordMessage (PennChordMessage::PING_RSP, message.GetTransactionId());
    resp.SetPingRsp (message.GetPingReq().pingMessage);
    Ptr<Packet> packet = Create<Packet> ();
    packet->AddHeader (resp);
    m_socket->SendTo (packet, 0 , InetSocketAddress (sourceAddress, sourcePort));
    // Send indication to application layer
    m_pingRecvFn (sourceAddress, message.GetPingReq().pingMessage);
}

void
PennChord::ProcessPingRsp (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort)
{
  // Remove from pingTracker
  std::map<uint32_t, Ptr<PingRequest> >::iterator iter;
  iter = m_pingTracker.find (message.GetTransactionId ());
  if (iter != m_pingTracker.end ())
    {
      std::string fromNode = ReverseLookup (sourceAddress);
      CHORD_LOG ("Received PING_RSP, From Node: " << fromNode << ", Message: " << message.GetPingRsp().pingMessage);
      m_pingTracker.erase (iter);
      // Send indication to application layer
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
      if (pingRequest->GetTimestamp().GetMilliSeconds() + m_pingTimeout.GetMilliSeconds() <= Simulator::Now().GetMilliSeconds())
        {
          DEBUG_LOG ("Ping expired. Message: " << pingRequest->GetPingMessage ()
                    << " Timestamp: " << pingRequest->GetTimestamp().GetMilliSeconds ()
                    << " CurrentTime: " << Simulator::Now().GetMilliSeconds ());
          // Remove stale entries
          m_pingTracker.erase (iter++);
          // Send indication to application layer
          m_pingFailureFn (pingRequest->GetDestinationAddress(), pingRequest->GetPingMessage ());
        }
      else
        {
          ++iter;
        }
    }
  // Reschedule timer
  m_auditPingsTimer.Schedule (m_pingTimeout);
}

//MS2A: Lookup Callback Registration
void
PennChord::SetLookupResultCallback(Callback<void, uint32_t, Ipv4Address> lookupResultFn)
{
  m_lookupResultFn = lookupResultFn;
}
void
PennChord::StartSearchLookup(std::string contextKey, uint32_t keyHash)
{
  uint32_t txn = GetNextTransactionId();
  m_lookupHopCounter[txn] = 0;

  uint32_t myKey = PennKeyHelper::CreateShaKey(GetLocalAddress());

  CHORD_LOG(
    GraderLogs::GetLookupIssueLogStr(myKey, keyHash)
  );

  // Save context key for response handling
  m_searchContext[txn] = contextKey;

  PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
  msg.SetLookupReq(keyHash, GetLocalAddress(), GetLocalAddress());

  Ptr<Packet> packet = Create<Packet>();
  packet->AddHeader(msg);

  m_socket->SendTo(packet, 0, InetSocketAddress(m_successor, m_appPort));
}
void
PennChord::StartPublishLookup(const std::string &keyword,
                              const std::string &docId,
                              uint32_t keyHash)
{
  uint32_t txn = GetNextTransactionId();
  m_lookupHopCounter[txn] = 0;

  uint32_t myKey = PennKeyHelper::CreateShaKey(GetLocalAddress());

  CHORD_LOG(
    GraderLogs::GetLookupIssueLogStr(myKey, keyHash)
  );

  // Save keyword and docId
  m_publishContext[txn] = {keyword, docId};

  PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
  msg.SetLookupReq(keyHash, GetLocalAddress(), GetLocalAddress());

  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(msg);
  m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));
}

//MS2A: Issue a Chord Lookup (PennSearch calls this)
void
PennChord::IssueChordLookup(uint32_t keyHash, Ipv4Address originator)
{
  uint32_t txn = GetNextTransactionId();
  m_lookupHopCounter[txn] = 0;

  uint32_t myKey = PennKeyHelper::CreateShaKey(GetLocalAddress());

  std::string logLine = GraderLogs::GetLookupIssueLogStr(myKey, keyHash);
  CHORD_LOG(logLine);

  PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
  msg.SetLookupReq(keyHash, originator, GetLocalAddress());

  Ptr<Packet> packet = Create<Packet>();
  packet->AddHeader(msg);

  m_socket->SendTo(packet, 0, InetSocketAddress(m_successor, m_appPort));
}

static bool IsBetweenHash(uint32_t target, uint32_t start, uint32_t end)
{
    if (start < end)
        return (target > start && target < end);
    else if (start > end)
        return (target > start || target < end);
    else
        return (target != start);
}
//MS2A: Process LOOKUP_REQ
void
PennChord::ProcessLookupReq(PennChordMessage message, Ipv4Address sourceAddress)
{
  uint32_t key = message.GetLookupReq().lookupKey;
  uint32_t keyHash = key;
  Ipv4Address originator = message.GetLookupReq().originator;
  // Increment hop count
  m_lookupHopCounter[message.GetTransactionId()]++;

  uint32_t localHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t predHash  = PennKeyHelper::CreateShaKey(m_predecessor);

  bool amOwner = false;

  // First node in ring owns all keys
  if (m_predecessor == Ipv4Address::GetAny())
    {
      amOwner = true;
    }
  else
    {
      amOwner = IsBetweenHash(keyHash, predHash, localHash);
    }

    if (amOwner)
      {
        uint32_t myKey        = localHash;
        uint32_t requesterKey = PennKeyHelper::CreateShaKey(originator);

        // MS2A lookup-result log format
        CHORD_LOG(
          GraderLogs::GetLookupResultLogStr(
            myKey,          // this node's key (owner)
            keyHash,        // target key
            ReverseLookup(originator),
            requesterKey
          )
        );

        // Send lookup response directly to originator
        PennChordMessage rsp(PennChordMessage::LOOKUP_RSP, message.GetTransactionId());
        rsp.SetLookupRsp(keyHash, GetLocalAddress());

        Ptr<Packet> pkt = Create<Packet>();
        pkt->AddHeader(rsp);
        m_socket->SendTo(pkt, 0, InetSocketAddress(originator, m_appPort));
        return;
      }

    // ============================================================
    // FORWARD CASE
    // ============================================================
    uint32_t myKey   = localHash;
    uint32_t succKey = PennKeyHelper::CreateShaKey(m_successor);

    // MS2A forwarding log format
    CHORD_LOG(
      GraderLogs::GetLookupForwardingLogStr(
        myKey,                    // current node's key
        ReverseLookup(m_successor),
        succKey,                  // successor key
        keyHash                   // target key
      )
    );

    // Forward LOOKUP to successor
    PennChordMessage fwd(PennChordMessage::LOOKUP_FORWARD, message.GetTransactionId());
    fwd.SetLookupForward(keyHash, originator, GetLocalAddress());

    Ptr<Packet> pkt = Create<Packet>();
    pkt->AddHeader(fwd);
    m_socket->SendTo(pkt, 0, InetSocketAddress(m_successor, m_appPort));
  }
//MS2A: Process LOOKUP_FORWARD
void
PennChord::ProcessLookupForward(PennChordMessage message, Ipv4Address sourceAddress)
{
  uint32_t keyHash   = message.GetLookupForward().lookupKey;
  Ipv4Address originator = message.GetLookupForward().originator;

  // Hop count increases each forward
  m_lookupHopCounter[message.GetTransactionId()]++;

  uint32_t localHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t predHash  = PennKeyHelper::CreateShaKey(m_predecessor);

  bool amOwner = false;

  // Case: first node in ring owns everything
  if (m_predecessor == Ipv4Address::GetAny())
    {
      amOwner = true;
    }
  else
    {
      amOwner = IsBetweenHash(keyHash, predHash, localHash);
    }

  // ============================================================
  // OWNERSHIP CASE
  // ============================================================
  if (amOwner)
    {
      uint32_t myKey        = localHash;
      uint32_t requesterKey = PennKeyHelper::CreateShaKey(originator);

      CHORD_LOG(
        GraderLogs::GetLookupResultLogStr(
          myKey,          // owner node key
          keyHash,        // target key
          ReverseLookup(originator),
          requesterKey
        )
      );

      PennChordMessage rsp(PennChordMessage::LOOKUP_RSP, message.GetTransactionId());
      rsp.SetLookupRsp(keyHash, GetLocalAddress());

      Ptr<Packet> pkt = Create<Packet>();
      pkt->AddHeader(rsp);
      m_socket->SendTo(pkt, 0, InetSocketAddress(originator, m_appPort));
      return;
    }

  // ============================================================
  // FORWARD CASE
  // ============================================================
  uint32_t myKey   = localHash;
  uint32_t succKey = PennKeyHelper::CreateShaKey(m_successor);

  CHORD_LOG(
    GraderLogs::GetLookupForwardingLogStr(
      myKey,                       // current node key
      ReverseLookup(m_successor),  // successor ID
      succKey,                     // successor key
      keyHash                      // target key
    )
  );

  PennChordMessage fwd(PennChordMessage::LOOKUP_FORWARD, message.GetTransactionId());
  fwd.SetLookupForward(keyHash, originator, GetLocalAddress());

  Ptr<Packet> pkt = Create<Packet>();
  pkt->AddHeader(fwd);
  m_socket->SendTo(pkt, 0, InetSocketAddress(m_successor, m_appPort));
}

// MS2A: Process LOOKUP_RSP
void
PennChord::ProcessLookupRsp(PennChordMessage message, Ipv4Address sourceAddress)
{
  uint32_t txn = message.GetTransactionId();
  uint32_t key = message.GetLookupRsp().lookupKey;
  Ipv4Address owner = message.GetLookupRsp().ownerNode;

  // Handle publish lookup context first
  auto itPub = m_publishContext.find(txn);
  if (itPub != m_publishContext.end())
  {
    auto pair = itPub->second;
    std::string keyword = pair.first;
    std::string docId   = pair.second;
    if (!m_publishLookupFn.IsNull())
    {
      m_publishLookupFn(keyword, docId, owner);
    }
      m_publishContext.erase(itPub);
      return;
}
  // First handle MS2A search context
  auto it = m_searchContext.find(txn);
  if (it != m_searchContext.end())
  {
    std::string ctx = it->second;

    if (!m_searchLookupFn.IsNull())
      m_searchLookupFn(ctx, owner);

    m_searchContext.erase(it);
    return;
  }

  // Fall back to original lookup callback for MS2A keyword ownership
  if (!m_lookupResultFn.IsNull())
    m_lookupResultFn(key, owner);
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
PennChord::SetSearchLookupCallback(Callback<void, std::string, Ipv4Address> cb)
{
  m_searchLookupFn = cb;
}
void
PennChord::SetPublishLookupCallback(Callback<void, std::string, std::string, Ipv4Address> cb)
{
  m_publishLookupFn = cb;
}



// ===============================================================
// Milestone 1 – Ring Creation and Join
// ===============================================================

void
PennChord::CreateChord()
{
  m_successor = GetLocalAddress();
  m_predecessor = Ipv4Address::GetAny();
  m_successorPredecessor[GetLocalAddress()] = GetLocalAddress();
  s_joined.insert(GetLocalAddress());
}

Ipv4Address
PennChord::FindSuccessor(uint32_t id)
{
  if (s_joined.empty())
    return GetLocalAddress();

  std::vector<Ipv4Address> nodes(s_joined.begin(), s_joined.end());
  std::sort(nodes.begin(), nodes.end(),
            [](Ipv4Address a, Ipv4Address b){
              return PennKeyHelper::CreateShaKey(a) < PennKeyHelper::CreateShaKey(b);
            });

  for (size_t i = 0; i < nodes.size(); ++i)
    {
      uint32_t curr = PennKeyHelper::CreateShaKey(nodes[i]);
      uint32_t next = PennKeyHelper::CreateShaKey(nodes[(i + 1) % nodes.size()]);
      if ((curr < id && id <= next) || (curr > next && (id > curr || id <= next)))
        return nodes[(i + 1) % nodes.size()];
    }
  return nodes.front();
}

void
PennChord::JoinChord(Ipv4Address referenceNode)
{
  // Mark this node as joined
  s_joined.insert(GetLocalAddress());
  m_successor = referenceNode;
  m_predecessor = Ipv4Address::GetAny();

  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  m_successor = FindSuccessor(myHash);
  Stabilize();
}

void
PennChord::LeaveChord()
{
  m_successor = Ipv4Address::GetAny();
  m_predecessor = Ipv4Address::GetAny();

  m_successorPredecessor.erase(GetLocalAddress());

  // also remove any reverse links
  for (auto it = m_successorPredecessor.begin(); it != m_successorPredecessor.end(); )
    {
      if (it->second == GetLocalAddress()) it = m_successorPredecessor.erase(it);
      else ++it;
    }
}

// ===============================================================
// Milestone 1 – Stabilization and Notify
// ===============================================================

void
PennChord::Stabilize ()
{
  if (m_successor == Ipv4Address::GetAny ()) return;

  if (m_successorPredecessor.count(m_successor) == 0) return;

  Ipv4Address successorPred = m_successorPredecessor[m_successor];
  if (IsBetween(successorPred, GetLocalAddress(), m_successor))
    {
      m_successor = successorPred;
    }

  Notify(m_successor);
}

void PennChord::Notify(Ipv4Address potentialPred)
{
  if (m_predecessor == Ipv4Address::GetAny() ||
      IsBetween(potentialPred, m_predecessor, GetLocalAddress()))
    m_predecessor = potentialPred;

  m_successorPredecessor[GetLocalAddress()] = m_predecessor;

  if (m_successor != Ipv4Address::GetAny())
    m_successorPredecessor[m_successor] = GetLocalAddress();
}

// ===============================================================
// Milestone 1 – Ringstate
// ===============================================================

std::string PennChord::ToHexKey(uint32_t value)
{
  std::ostringstream oss;
  oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << value;
  return oss.str();
}

void
PennChord::Ringstate()
{
  if (s_joined.empty())
    {
      uint32_t hash = PennKeyHelper::CreateShaKey(GetLocalAddress());
      std::string key = ToHexKey(hash);

      GraderLogs::RingState(
          GetLocalAddress(), ReverseLookup(GetLocalAddress()), hash,
          Ipv4Address::GetAny(), "", 0,
          Ipv4Address::GetAny(), "", 0);
      GraderLogs::EndOfRingState();
      return;
    }

  std::vector<Ipv4Address> nodes(s_joined.begin(), s_joined.end());
  std::sort(nodes.begin(), nodes.end(),
            [](Ipv4Address a, Ipv4Address b){
              return PennKeyHelper::CreateShaKey(a) < PennKeyHelper::CreateShaKey(b);
            });

  size_t startIdx = 0;
  for (size_t i = 0; i < nodes.size(); ++i)
    if (nodes[i] == GetLocalAddress()) { startIdx = i; break; }

  for (size_t k = 0; k < nodes.size(); ++k)
    {
      size_t i   = (startIdx + k) % nodes.size();
      size_t ip1 = (i + 1) % nodes.size();
      size_t im1 = (i + nodes.size() - 1) % nodes.size();

      Ipv4Address curr = nodes[i];
      Ipv4Address succ = nodes[ip1];
      Ipv4Address pred = nodes[im1];

      uint32_t currHash = PennKeyHelper::CreateShaKey(curr);
      uint32_t succHash = PennKeyHelper::CreateShaKey(succ);
      uint32_t predHash = PennKeyHelper::CreateShaKey(pred);

      std::string currKey = ToHexKey(currHash);
      std::string succKey = ToHexKey(succHash);
      std::string predKey = ToHexKey(predHash);

      GraderLogs::RingState(
          curr, ReverseLookup(curr), currHash,
          pred, ReverseLookup(pred), predHash,
          succ, ReverseLookup(succ), succHash);
    }

  GraderLogs::EndOfRingState();
}

// ---------- Helper ----------
bool PennChord::IsBetween(Ipv4Address target, Ipv4Address start, Ipv4Address end)
{
  uint32_t hStart = PennKeyHelper::CreateShaKey(start);
  uint32_t hEnd   = PennKeyHelper::CreateShaKey(end);
  uint32_t hTgt   = PennKeyHelper::CreateShaKey(target);

  if (hStart < hEnd)
    return (hTgt > hStart && hTgt < hEnd);
  else if (hStart > hEnd)
    return (hTgt > hStart || hTgt < hEnd);
  else
    return (hTgt != hStart);
}

//MS2A Utility: Convert uint32 key to hex string
std::string
HexString(uint32_t value)
{
  std::ostringstream oss;
  oss << std::hex << std::setw(8) << std::setfill('0') << value;
  return oss.str();
}

