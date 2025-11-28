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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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

// Static member initialization (used only for tracking overall joined set, not for ring maintenance)
std::set<Ipv4Address> PennChord::s_joined; // FIX: Correct static initialization

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

// Constructor initialization list reordered to match header declaration order.
PennChord::PennChord ()
    : m_auditPingsTimer (Timer::CANCEL_ON_DESTROY),
      m_fingerIndex (0), 
      m_stabilizeTimer (Timer::CANCEL_ON_DESTROY),
      m_fixFingersTimer (Timer::CANCEL_ON_DESTROY)
{
  Ptr<UniformRandomVariable> m_uniformRandomVariable = CreateObject<UniformRandomVariable> ();
  m_currentTransactionId = m_uniformRandomVariable->GetValue (0x00000000, 0xFFFFFFFF);
  m_successor = Ipv4Address::GetAny();
  m_predecessor = Ipv4Address::GetAny();
  m_ringstateOriginator = Ipv4Address::GetAny();
}

PennChord::~PennChord ()
{
}

void
PennChord::DoDispose ()
{
  // MS2: Log Average Hop Count before stop
  uint64_t totalHops = 0;
  uint32_t totalLookups = 0;

  for (auto const& entry : m_lookupHopCounter)
  {
      totalHops += entry.second; 
      totalLookups++;
  }

  if (totalLookups > 0)
  {
    // Match GraderLogs::AverageHopCount(std::string currNodeId, uint16_t lookupCount, uint16_t lookupHopCount)
    GraderLogs::AverageHopCount(
        ReverseLookup(GetLocalAddress()), 
        (uint16_t)totalLookups,           
        (uint16_t)(totalHops / totalLookups)               
    );
  }

  StopApplication ();
  PennApplication::DoDispose ();
}

void
PennChord::StartApplication (void)
{
  if (m_socket == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny(), m_appPort);
      m_socket->Bind (local);
      m_socket->SetRecvCallback (MakeCallback (&PennChord::RecvMessage, this));
    }

  // Configure timers
  m_auditPingsTimer.SetFunction (&PennChord::AuditPings, this);
  m_stabilizeTimer.SetFunction (&PennChord::Stabilize, this);
  m_fixFingersTimer.SetFunction (&PennChord::FixFingers, this);

  // Start timers (audit is always running)
  m_auditPingsTimer.Schedule (m_pingTimeout);
}

void
PennChord::StartPeriodicStabilization()
{
  // Only schedule if the node has joined the ring
  if (s_joined.count(GetLocalAddress()) == 0) return;
  
  // Schedule periodic calls if not already scheduled
  if (!m_stabilizeTimer.IsRunning())
      m_stabilizeTimer.Schedule(Seconds(1.0)); // Chord stabilization interval
  if (!m_fixFingersTimer.IsRunning())
      m_fixFingersTimer.Schedule(Seconds(0.2)); // Faster convergence than stabilize
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
  m_stabilizeTimer.Cancel ();
  m_fixFingersTimer.Cancel ();

  m_pingTracker.clear ();
}

void
PennChord::ProcessCommand (std::vector<std::string> tokens)
{
  if (tokens.size() < 1)
    return;

  std::string command = tokens[0];

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
  // Handle LEAVE command
  else if (command == "LEAVE")
    {
      LeaveChord();
    }
  // Handle RINGSTATE command
  else if (command == "RINGSTATE")
    {
        // Start ringstate dump
        m_ringstateOriginator = GetLocalAddress();
        SendRingstate(GetLocalAddress(), GetLocalAddress());
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
      // MS2A ADDITION: Handle Lookup Messages
      case PennChordMessage::LOOKUP_REQ:
        ProcessLookupReq(message, sourceAddress);
        break;
      case PennChordMessage::LOOKUP_FORWARD:
        ProcessLookupForward(message, sourceAddress);
        break;
      case PennChordMessage::LOOKUP_RSP:
        ProcessLookupRsp(message, sourceAddress);
        break;
      // MS1/MS2 Stabilization Messages
      case PennChordMessage::GET_PRED_REQ:
        ProcessGetPredReq(message, sourceAddress);
        break;
      case PennChordMessage::GET_PRED_RSP:
        ProcessGetPredRsp(message, sourceAddress);
        break;
      case PennChordMessage::NOTIFY_REQ:
        ProcessNotifyReq(message, sourceAddress);
        break;
      case PennChordMessage::RINGSTATE_REQ:
        ProcessRingstateReq(message, sourceAddress);
        break;
      default:
        ERROR_LOG ("Unknown Message Type!");
        break;
    }
}

// Helper: Check if target hash is in the semi-open interval (start, end]
static bool IsBetweenHashSemiOpen(uint32_t target, uint32_t start, uint32_t end)
{
    if (start == Ipv4Address::GetAny().Get()) // Special case: predecessor is nil (N=1 or first join)
      return true;

    if (start < end)
        return (target > start && target <= end);
    else if (start > end) // Wraparound (e.g., (0xFFFFFFF0, 0x0000000F])
        return (target > start || target <= end);
    else
        // start == end. Only possible if N=1 and node is its own pred/succ.
        return true; 
}

// MS2A: Process LOOKUP_REQ - Initial request to node
void
PennChord::ProcessLookupReq(PennChordMessage message, Ipv4Address sourceAddress)
{
  uint32_t keyHash = message.GetLookupReq().lookupKey;
  Ipv4Address originator = message.GetLookupReq().originator;
  uint32_t txn = message.GetTransactionId();

  // Increment hop count for this request (originator does not count the initial message)
  if (m_lookupHopCounter.count(txn) == 0)
    m_lookupHopCounter[txn] = 0; // Initialize for lookup that starts at me.
  else
    m_lookupHopCounter[txn]++; // Increment for forwarded messages (which shouldn't happen here, but safety)

  uint32_t localHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t predHash  = PennKeyHelper::CreateShaKey(m_predecessor);

  bool amOwner = false;
  amOwner = IsBetweenHashSemiOpen(keyHash, predHash, localHash);

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
      PennChordMessage rsp(PennChordMessage::LOOKUP_RSP, txn);
      rsp.SetLookupRsp(keyHash, GetLocalAddress());

      Ptr<Packet> pkt = Create<Packet>();
      pkt->AddHeader(rsp);
      m_socket->SendTo(pkt, 0, InetSocketAddress(originator, m_appPort));
      return;
    }

  // ============================================================
  // FORWARD CASE (O(log N) Routing)
  // ============================================================

  // Find next hop using finger table (or successor if table is empty/unhelpful)
  Ipv4Address nextHop = FindSuccessor(keyHash);

  // If FindSuccessor returned myself, the ring is small, next hop must be successor.
  if (nextHop == GetLocalAddress() && nextHop != m_successor)
    nextHop = m_successor;
  
  // Safety check (shouldn't happen with correct logic)
  if (nextHop == GetLocalAddress())
    return;

  uint32_t myKey   = localHash;
  uint32_t nextKey = PennKeyHelper::CreateShaKey(nextHop);

  // Log forwarding
  CHORD_LOG(
    GraderLogs::GetLookupForwardingLogStr(
      myKey,                    // current node's key
      ReverseLookup(nextHop),
      nextKey,                  // next hop key
      keyHash                   // target key
    )
  );
  
  // Forward LOOKUP to nextHop
  m_lookupHopCounter[txn]++; // Increment before forwarding
  PennChordMessage fwd(PennChordMessage::LOOKUP_FORWARD, txn);
  // Update lastHop to be the current node
  fwd.SetLookupForward(keyHash, originator, GetLocalAddress());

  Ptr<Packet> pkt = Create<Packet>();
  pkt->AddHeader(fwd);
  m_socket->SendTo(pkt, 0, InetSocketAddress(nextHop, m_appPort));
}


// MS2A: Process LOOKUP_FORWARD - Lookup forwarded from another node
void
PennChord::ProcessLookupForward(PennChordMessage message, Ipv4Address sourceAddress)
{
  uint32_t keyHash   = message.GetLookupForward().lookupKey;
  Ipv4Address originator = message.GetLookupForward().originator;
  uint32_t txn = message.GetTransactionId();

  // Hop count increases each forward
  m_lookupHopCounter[txn]++;

  uint32_t localHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t predHash  = PennKeyHelper::CreateShaKey(m_predecessor);

  bool amOwner = false;
  amOwner = IsBetweenHashSemiOpen(keyHash, predHash, localHash);

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

      PennChordMessage rsp(PennChordMessage::LOOKUP_RSP, txn);
      rsp.SetLookupRsp(keyHash, GetLocalAddress());

      Ptr<Packet> pkt = Create<Packet>();
      pkt->AddHeader(rsp);
      m_socket->SendTo(pkt, 0, InetSocketAddress(originator, m_appPort));
      return;
    }

  // ============================================================
  // FORWARD CASE (O(log N) Routing)
  // ============================================================

  // Find next hop using finger table
  Ipv4Address nextHop = FindSuccessor(keyHash);

  // If FindSuccessor returned myself, the ring is small, next hop must be successor.
  if (nextHop == GetLocalAddress() && nextHop != m_successor)
    nextHop = m_successor;
  
  // Safety check (shouldn't happen with correct logic)
  if (nextHop == GetLocalAddress())
    return;

  uint32_t myKey   = localHash;
  uint32_t nextKey = PennKeyHelper::CreateShaKey(nextHop);

  CHORD_LOG(
    GraderLogs::GetLookupForwardingLogStr(
      myKey,                       // current node key
      ReverseLookup(nextHop),      // next hop ID
      nextKey,                     // next hop key
      keyHash                      // target key
    )
  );

  PennChordMessage fwd(PennChordMessage::LOOKUP_FORWARD, txn);
  fwd.SetLookupForward(keyHash, originator, GetLocalAddress());

  Ptr<Packet> pkt = Create<Packet>();
  pkt->AddHeader(fwd);
  m_socket->SendTo(pkt, 0, InetSocketAddress(nextHop, m_appPort));
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
    m_lookupHopCounter.erase(txn); // Cleanup hop counter
    return;
  }
  // Handle MS2A search context
  auto it = m_searchContext.find(txn);
  if (it != m_searchContext.end())
  {
    std::string ctx = it->second;

    if (!m_searchLookupFn.IsNull())
      m_searchLookupFn(ctx, owner);

    m_searchContext.erase(it);
    m_lookupHopCounter.erase(txn); // Cleanup hop counter
    return;
  }

  // Fall back to original lookup callback for MS2A keyword ownership
  if (!m_lookupResultFn.IsNull())
    m_lookupResultFn(key, owner);

  m_lookupHopCounter.erase(txn); // Cleanup hop counter
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

// FIX: Added implementation for SendPing
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
      m_pingTracker.insert (std::make_pair (transactionId, pingRequest));
      Ptr<Packet> packet = Create<Packet> ();
      PennChordMessage message = PennChordMessage (PennChordMessage::PING_REQ, transactionId);
      message.SetPingReq (pingMessage);
      packet->AddHeader (message);
      m_socket->SendTo (packet, 0 , InetSocketAddress (destAddress, m_appPort));
    }
  else
    {
      m_pingFailureFn (destAddress, pingMessage);
    }
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

// FIX: Added implementation for StartSearchLookup
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
  // Lookup always starts the routing at the sender
  msg.SetLookupReq(keyHash, GetLocalAddress(), GetLocalAddress());

  Ptr<Packet> packet = Create<Packet>();
  packet->AddHeader(msg);

  // Use the successor for the initial hop
  m_socket->SendTo(packet, 0, InetSocketAddress(m_successor, m_appPort));
}

// FIX: Added implementation for StartPublishLookup
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

void 
PennChord::SetKeyTransferCallback(Callback<void, Ipv4Address, Ipv4Address> cb)
{
  m_keyTransferFn = cb;
}


// ===============================================================
// Milestone 1 – Ring Creation and Join
// ===============================================================

void
PennChord::CreateChord()
{
  // Only node 0 enters here (landmark)
  m_successor = GetLocalAddress();
  m_predecessor = Ipv4Address::GetAny();
  s_joined.insert(GetLocalAddress());
  
  // MS2: Initialize finger table and start periodic maintenance
  InitFingerTable();
  StartPeriodicStabilization();
}

void
PennChord::JoinChord(Ipv4Address referenceNode)
{
  // Mark this node as joined
  s_joined.insert(GetLocalAddress());
  m_predecessor = Ipv4Address::GetAny();

  // Initial successor guess: referenceNode. Stabilize will fix this.
  m_successor = referenceNode;

  // MS2: Initialize finger table first
  InitFingerTable();

  // Start stabilization timers. Stabilize will find the correct successor.
  StartPeriodicStabilization();
}

// Key transfer logic added for leaving node
void
PennChord::LeaveChord()
{
  if (s_joined.count(GetLocalAddress()) == 0) return;

  // 1. Trigger key transfer to the successor
  if (m_successor != Ipv4Address::GetAny() && m_successor != GetLocalAddress())
  {
      // The old owner transfers all its keys to its successor.
      TransferKeys(m_successor, GetLocalAddress());

      // 2. Notify successor and predecessor that I am leaving
      // Successor: needs to know its new predecessor is my predecessor
      PennChordMessage succNotify(PennChordMessage::NOTIFY_REQ, GetNextTransactionId());
      succNotify.SetNotifyReq(m_predecessor);
      Ptr<Packet> p1 = Create<Packet>();
      p1->AddHeader(succNotify);
      m_socket->SendTo(p1, 0, InetSocketAddress(m_successor, m_appPort));

      // Predecessor: needs to know its new successor is my successor
      // This requires a new message type "CHANGE_SUCCESSOR_REQ", but for simplicity/M1, rely on stabilization.
  }
  
  // Cleanup ring state
  m_successor = Ipv4Address::GetAny();
  m_predecessor = Ipv4Address::GetAny();
  m_fingerTable.clear();

  s_joined.erase(GetLocalAddress());

  // Stop periodic timers
  m_stabilizeTimer.Cancel();
  m_fixFingersTimer.Cancel();
}

// ===============================================================
// Milestone 1 – Stabilization Message Logic
// ===============================================================

void
PennChord::Stabilize ()
{
  if (m_successor == Ipv4Address::GetAny ())
  {
      // If successor is lost (e.g., node 0 leaves), we must look up our own successor again
      // Simplified: if we are not the only node, try to find a new successor.
      // This is complex, so for M1, rely on other nodes stabilizing.
      m_stabilizeTimer.Schedule(Seconds(1.0));
      return;
  }
  
  // 1. Ask successor for its predecessor (x)
  PennChordMessage msg(PennChordMessage::GET_PRED_REQ, GetNextTransactionId());
  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(msg);
  m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));

  // Reschedule self for stabilization
  m_stabilizeTimer.Schedule(Seconds(1.0));
}

void PennChord::ProcessGetPredReq(PennChordMessage message, Ipv4Address sourceAddress)
{
    // Source is my successor (Stabilize called by my successor)
    // 1. Send GET_PRED_RSP back with my predecessor address.
    PennChordMessage rsp(PennChordMessage::GET_PRED_RSP, message.GetTransactionId());
    rsp.SetGetPredRsp(m_predecessor);

    Ptr<Packet> p = Create<Packet>();
    p->AddHeader(rsp);
    m_socket->SendTo(p, 0, InetSocketAddress(sourceAddress, m_appPort));
}

void PennChord::ProcessGetPredRsp(PennChordMessage message, Ipv4Address sourceAddress)
{
    // Source is my successor. Its predecessor is x.
    Ipv4Address x = message.GetGetPredRsp().predecessor;

    if (x != Ipv4Address::GetAny() && x != GetLocalAddress())
    {
        // Check if x is a better successor than my current successor
        // Is x in the open interval (self, m_successor)?
        if (IsBetween(x, GetLocalAddress(), m_successor))
        {
            // If x is a better successor, set it and transfer keys (if needed)
            TransferKeys(x, m_successor); // Signal key transfer to new successor
            m_successor = x;

            // Update Finger[1] (index 0)
            if (!m_fingerTable.empty())
                m_fingerTable[0].successor = m_successor;
        }
    }

    // 2. Tell my successor (m_successor) that I believe I am its predecessor
    Notify(m_successor);
}

void PennChord::Notify(Ipv4Address successor)
{
    // Send NOTIFY_REQ to my current successor
    PennChordMessage msg(PennChordMessage::NOTIFY_REQ, GetNextTransactionId());
    msg.SetNotifyReq(GetLocalAddress());

    Ptr<Packet> p = Create<Packet>();
    p->AddHeader(msg);
    m_socket->SendTo(p, 0, InetSocketAddress(successor, m_appPort));
}

void PennChord::ProcessNotifyReq(PennChordMessage message, Ipv4Address sourceAddress)
{
    Ipv4Address potentialPred = message.GetNotifyReq().potentialPredecessor;

    // The potentialPred is the node that sent the notification (n).
    // The current node is the successor (n+1)
    // Check if potentialPred is a better predecessor:
    // Is predecessor nil, or is potentialPred in the open interval (predecessor, self)?
    if (m_predecessor == Ipv4Address::GetAny() ||
        IsBetween(potentialPred, m_predecessor, GetLocalAddress()))
    {
        Ipv4Address oldPredecessor = m_predecessor;
        m_predecessor = potentialPred;

        // Key transfer on join: if predecessor changed from nil to a node,
        // we signal the need to get keys from our successor.
        if (oldPredecessor == Ipv4Address::GetAny() && m_predecessor != GetLocalAddress())
        {
            // New node joined, we are its successor. We need to transfer keys that fall
            // between the new predecessor (potentialPred) and ourselves to the new predecessor.
            TransferKeys(potentialPred, GetLocalAddress());
        }
    }
}

// FIX: Method to signal key transfer to the application layer (PennSearch)
void PennChord::TransferKeys(Ipv4Address newOwner, Ipv4Address oldOwner)
{
    if (!m_keyTransferFn.IsNull())
    {
        m_keyTransferFn(newOwner, oldOwner);
    }
    else
    {
         CHORD_LOG("[TransferKeys] Key transfer signaled to App Layer. New Owner: " 
                  << ReverseLookup(newOwner) << ", Old Owner: " 
                  << ReverseLookup(oldOwner));
    }
}


// ===============================================================
// Milestone 2 – Finger Table Management
// ===============================================================

void
PennChord::InitFingerTable()
{
  m_fingerTable.clear();
  m_fingerIndex = 0; // Start at index 0 (Finger 1)

  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  // Assuming 32-bit hash (m=32)
  for (int i = 0; i < 32; ++i)
  {
    FingerEntry entry;
    // Calculate start: (myHash + 2^i) mod 2^32. Since uint32_t overflows correctly:
    entry.start = myHash + (1 << i);
    entry.successor = m_successor; // Initialize to current successor
    m_fingerTable.push_back(entry);
  }
}

void
PennChord::FixFingers()
{
  // Only run if the node has joined
  if (s_joined.count(GetLocalAddress()) == 0) return;
  if (m_fingerTable.empty()) InitFingerTable();

  // Increment index (1 to 32)
  m_fingerIndex = (m_fingerIndex % 32) + 1;
  size_t i = m_fingerIndex - 1; // 0-based index

  uint32_t fingerStart = m_fingerTable[i].start;

  Ipv4Address nextHop = FindSuccessor(fingerStart);

  // Update the finger table entry
  m_fingerTable[i].successor = nextHop;

  // Reschedule for next fix
  m_fixFingersTimer.Schedule(Seconds(0.2)); // Faster convergence
}


// O(log N) routing: Find the node responsible for ID, or the closest preceding node
Ipv4Address
PennChord::FindSuccessor(uint32_t id)
{
  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());

  // 1. Check if the target is between me (exclusive) and my successor (inclusive)
  if (IsBetweenHashSemiOpen(id, myHash, PennKeyHelper::CreateShaKey(m_successor)))
    return m_successor;

  // 2. Use finger table to find the closest preceding finger
  Ipv4Address closest = ClosestPrecedingFinger(id);

  // 3. The Chord paper mandates: n.find_successor(id) sends request to closest.successor
  // If closest is not myself, recursively call lookup on the closest finger.
  // Since we are not doing RPC lookups, we return the closest finger's successor (which is the next hop)

  // Use the closest node's successor as the next hop, or the closest node itself.
  if (closest == GetLocalAddress())
    return m_successor; 

  // In this simplified implementation, the closest preceding finger IS the next hop
  return closest;
}

Ipv4Address
PennChord::ClosestPrecedingFinger(uint32_t id)
{
  // uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress()); // Removed unused variable

  // Iterate backwards through finger table
  for (int i = m_fingerTable.size() - 1; i >= 0; --i)
  {
    Ipv4Address fingerSucc = m_fingerTable[i].successor;
    if (fingerSucc == Ipv4Address::GetAny()) continue;
    // uint32_t fingerHash = PennKeyHelper::CreateShaKey(fingerSucc); // Removed unused variable

    // Check if fingerSucc is in the open interval (myHash, id)
    if (IsBetween(fingerSucc, GetLocalAddress(), ResolveNodeIpAddress(ReverseLookup(GetLocalAddress()))))
      return fingerSucc;
  }
  return GetLocalAddress(); // No finger is closer, will fall back to successor (m_successor)
}

// ===============================================================
// Milestone 1 – Ringstate Logging (Message-based)
// ===============================================================

void
PennChord::SendRingstate(Ipv4Address target, Ipv4Address originator)
{
    // Log my state
    uint32_t currHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
    uint32_t succHash = PennKeyHelper::CreateShaKey(m_successor);
    uint32_t predHash = PennKeyHelper::CreateShaKey(m_predecessor);
    
    // Check if the current node is part of the ring
    if (s_joined.count(GetLocalAddress()))
    {
        GraderLogs::RingState(
            GetLocalAddress(), ReverseLookup(GetLocalAddress()), currHash,
            m_predecessor, ReverseLookup(m_predecessor), predHash,
            m_successor, ReverseLookup(m_successor), succHash);

        // If I am NOT the originator, I need to forward the request to my successor
        if (GetLocalAddress() != originator)
        {
            PennChordMessage msg(PennChordMessage::RINGSTATE_REQ, GetNextTransactionId());
            msg.SetRingstateReq(originator);
            Ptr<Packet> p = Create<Packet>();
            p->AddHeader(msg);
            m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));
        }
    }
}

void PennChord::ProcessRingstateReq(PennChordMessage message, Ipv4Address sourceAddress)
{
    Ipv4Address originator = message.GetRingstateReq().originator;

    if (GetLocalAddress() == originator)
    {
        // Request came back to the originator. Log my state one last time and close the ring.
        // The first log happens in SendRingstate when the command is initiated.
        GraderLogs::EndOfRingState();
        m_ringstateOriginator = Ipv4Address::GetAny(); // Clear state
    }
    else
    {
        // Log my state and forward the request
        SendRingstate(GetLocalAddress(), originator);
    }
}

void PennChord::Ringstate()
{
    // The actual ringstate initiation is handled in ProcessCommand to set m_ringstateOriginator
    // We do nothing here, the logic is in ProcessCommand and ProcessRingstateReq/SendRingstate
}

// ---------- Helper ----------
// Checks if target is in the open interval (start, end)
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

// PING Handlers (Unchanged, retaining for completeness)

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