/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "penn-chord.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4.h"
#include "ns3/random-variable-stream.h"
#include "ns3/penn-key-helper.h"
#include "ns3/grader-logs.h"
#include <openssl/sha.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <vector>

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("PennChord");

// --- Helper Functions ---
// Defined statically here to avoid scope/declaration order issues
static std::string HexString(uint32_t value) {
    // Uses the helper from penn-key-helper.h
    return PennKeyHelper::KeyToHexString(value);
}

// --- PennChord Implementation ---

TypeId PennChord::GetTypeId ()
{
  static TypeId tid = TypeId ("PennChord")
            .SetParent<PennApplication> ()
            .AddConstructor<PennChord> ()
            .AddAttribute ("AppPort", "Listening port for Application", UintegerValue (10001),
                           MakeUintegerAccessor (&PennChord::m_appPort), MakeUintegerChecker<uint16_t> ())
            .AddAttribute ("PingTimeout", "Timeout value for PING_REQ", TimeValue (MilliSeconds (2000)),
                           MakeTimeAccessor (&PennChord::m_pingTimeout), MakeTimeChecker ());
  return tid;
}

PennChord::PennChord ()
    : m_auditPingsTimer (Timer::CANCEL_ON_DESTROY),
      m_stabilizeTimer (Timer::CANCEL_ON_DESTROY), 
      m_fingerIndex (0),
      m_fixFingersTimer (Timer::CANCEL_ON_DESTROY)
{
  Ptr<UniformRandomVariable> m_uniformRandomVariable = CreateObject<UniformRandomVariable> ();
  m_currentTransactionId = m_uniformRandomVariable->GetValue (0x00000000, 0xFFFFFFFF);
  m_isJoined = false;
  m_successor = Ipv4Address::GetAny();
  m_predecessor = Ipv4Address::GetAny();
}

PennChord::~PennChord () { }

void PennChord::DoDispose ()
{
  uint64_t totalHops = 0;
  uint32_t totalLookups = 0;
  for (auto const& entry : m_lookupHopCounter) {
      totalHops += entry.second;
      totalLookups++;
  }
  if (totalLookups > 0) {
    GraderLogs::AverageHopCount(ReverseLookup(GetLocalAddress()), (uint16_t)totalLookups, (uint16_t)totalHops);
  }
  StopApplication ();
  PennApplication::DoDispose ();
}

void PennChord::StartApplication (void)
{
  if (m_socket == 0) {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny(), m_appPort);
      m_socket->Bind (local);
      m_socket->SetRecvCallback (MakeCallback (&PennChord::RecvMessage, this));
  }
  m_auditPingsTimer.SetFunction (&PennChord::AuditPings, this);
  m_stabilizeTimer.SetFunction (&PennChord::Stabilize, this);
  m_fixFingersTimer.SetFunction (&PennChord::FixFingers, this);
  m_auditPingsTimer.Schedule (m_pingTimeout);
}

void PennChord::StopApplication (void)
{
  if (m_socket) {
      m_socket->Close ();
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
      m_socket = 0;
  }
  m_auditPingsTimer.Cancel ();
  m_stabilizeTimer.Cancel ();
  m_fixFingersTimer.Cancel ();
  m_pingTracker.clear ();
}

// FIXED: Added StopChord implementation to satisfy linker
void PennChord::StopChord()
{
    StopApplication();
}

void PennChord::ProcessCommand (std::vector<std::string> tokens)
{
  if (tokens.size() < 1) return;
  std::string command = tokens[0];

  CHORD_LOG ("[ProcessCommand] " << command << " on " <<  ReverseLookup(GetLocalAddress()));

  if (command == "JOIN") {
      if (tokens.size() < 2) return;
      if (ReverseLookup(GetLocalAddress()) == tokens[1]) CreateChord();
      else {
        Ipv4Address referenceNode = ResolveNodeIpAddress(tokens[1]);
        JoinChord(referenceNode);
      }
    }
  else if (command == "LEAVE") LeaveChord();
  else if (command == "RINGSTATE") Ringstate();
}

void PennChord::RecvMessage (Ptr<Socket> socket)
{
  Address sourceAddr;
  Ptr<Packet> packet = socket->RecvFrom (sourceAddr);
  InetSocketAddress inetSocketAddr = InetSocketAddress::ConvertFrom (sourceAddr);
  Ipv4Address sourceAddress = inetSocketAddr.GetIpv4 ();
  PennChordMessage message;
  packet->RemoveHeader (message);

  switch (message.GetMessageType ()) {
      case PennChordMessage::PING_REQ: ProcessPingReq (message, sourceAddress, inetSocketAddr.GetPort()); break;
      case PennChordMessage::PING_RSP: ProcessPingRsp (message, sourceAddress, inetSocketAddr.GetPort()); break;
      case PennChordMessage::LOOKUP_REQ: ProcessLookupReq(message, sourceAddress); break;
      case PennChordMessage::LOOKUP_FORWARD: ProcessLookupForward(message, sourceAddress); break;
      case PennChordMessage::LOOKUP_RSP: ProcessLookupRsp(message, sourceAddress); break;
      // MS2 Distributed
      case PennChordMessage::GET_PREDECESSOR_REQ: ProcessGetPredecessorReq(message, sourceAddress); break;
      case PennChordMessage::GET_PREDECESSOR_RSP: ProcessGetPredecessorRsp(message, sourceAddress); break;
      case PennChordMessage::NOTIFY_REQ: ProcessNotifyReq(message, sourceAddress); break;
      case PennChordMessage::RINGSTATE_REQ: ProcessRingStateReq(message, sourceAddress); break;
      default: break;
  }
}

// --- Ring Creation & Join ---

void PennChord::CreateChord()
{
  m_successor = GetLocalAddress();
  m_predecessor = Ipv4Address::GetAny();
  m_isJoined = true;
  InitFingerTable();
  StartPeriodicStabilization();
}

void PennChord::JoinChord(Ipv4Address referenceNode)
{
  // Per teammate/spec: Use FindSuccessor (via Lookup) to find where we belong.
  // We issue a lookup for our own ID to the reference node.
  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  m_joinTransactionId = GetNextTransactionId(); // Track this specific lookup

  CHORD_LOG("Joining via " << referenceNode << ". Issuing LOOKUP for self: " << HexString(myHash));
  
  // Create Lookup Request
  PennChordMessage msg(PennChordMessage::LOOKUP_REQ, m_joinTransactionId);
  msg.SetLookupReq(myHash, GetLocalAddress(), GetLocalAddress());

  // Send directly to reference node
  Ptr<Packet> packet = Create<Packet>();
  packet->AddHeader(msg);
  m_socket->SendTo(packet, 0, InetSocketAddress(referenceNode, m_appPort));
}

// --- Distributed Stabilization ---

void PennChord::StartPeriodicStabilization()
{
  if (!m_isJoined) return;
  if (!m_stabilizeTimer.IsRunning()) m_stabilizeTimer.Schedule(Seconds(1.0));
  if (!m_fixFingersTimer.IsRunning()) m_fixFingersTimer.Schedule(Seconds(0.5));
}

void PennChord::Stabilize ()
{
  if (!m_isJoined || m_successor == Ipv4Address::GetAny()) return;
  
  // Step 1: Ask successor for its predecessor
  PennChordMessage msg(PennChordMessage::GET_PREDECESSOR_REQ, GetNextTransactionId());
  msg.SetGetPredecessorReq();
  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(msg);
  m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));
  
  // Schedule next
  m_stabilizeTimer.Schedule(Seconds(1.0));
}

void PennChord::ProcessGetPredecessorReq(PennChordMessage message, Ipv4Address sourceAddress)
{
  // Reply with my current predecessor
  PennChordMessage rsp(PennChordMessage::GET_PREDECESSOR_RSP, message.GetTransactionId());
  rsp.SetGetPredecessorRsp(m_predecessor);
  
  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(rsp);
  m_socket->SendTo(p, 0, InetSocketAddress(sourceAddress, m_appPort));
}

void PennChord::ProcessGetPredecessorRsp(PennChordMessage message, Ipv4Address sourceAddress)
{
  Ipv4Address x = message.GetGetPredecessorRsp().predecessor;

  // If successor has a predecessor, check if it's closer to me than the successor itself
  if (x != Ipv4Address::GetAny()) {
      if (IsBetween(x, GetLocalAddress(), m_successor)) {
          m_successor = x;
          // Update Finger[0]
          if (!m_fingerTable.empty()) m_fingerTable[0].successor = m_successor;
      }
  }

  // Step 2: Notify successor that I am its predecessor
  Notify(m_successor);
}

void PennChord::Notify(Ipv4Address target)
{
  if (target == Ipv4Address::GetAny()) return;

  PennChordMessage msg(PennChordMessage::NOTIFY_REQ, GetNextTransactionId());
  msg.SetNotifyReq(GetLocalAddress()); // I am the candidate
  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(msg);
  m_socket->SendTo(p, 0, InetSocketAddress(target, m_appPort));
}

void PennChord::ProcessNotifyReq(PennChordMessage message, Ipv4Address sourceAddress)
{
  Ipv4Address candidate = message.GetNotifyReq().potentialPredecessor;

  // If pred is nil or candidate is between current pred and me
  if (m_predecessor == Ipv4Address::GetAny() || IsBetween(candidate, m_predecessor, GetLocalAddress())) {
      Ipv4Address oldPred = m_predecessor;
      m_predecessor = candidate;
      
      // MS2: Logic to transfer keys would go here if we were doing data movement
      if (oldPred != m_predecessor) {
          TransferKeys(m_predecessor, oldPred, Ipv4Address::GetAny());
      }
  }
}

// FIXED: Added stub for TransferKeys to prevent linker error
void PennChord::TransferKeys(Ipv4Address newOwner, Ipv4Address oldOwner, Ipv4Address predOfNewOwner)
{
    // Stub implementation for MS2B data transfer
    CHORD_LOG("Transferring keys from " << oldOwner << " to " << newOwner);
}

// --- Ring State (Token Traversal) ---

void PennChord::Ringstate()
{
  if (!m_isJoined) {
    // Just print self (degenerate ring)
    uint32_t h = PennKeyHelper::CreateShaKey(GetLocalAddress());
    GraderLogs::RingState(GetLocalAddress(), ReverseLookup(GetLocalAddress()), h,
                          Ipv4Address::GetAny(), "", 0, Ipv4Address::GetAny(), "", 0);
    GraderLogs::EndOfRingState();
    return;
  }

  // Log self
  uint32_t myH = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t sH = (m_successor != Ipv4Address::GetAny()) ? PennKeyHelper::CreateShaKey(m_successor) : 0;
  uint32_t pH = (m_predecessor != Ipv4Address::GetAny()) ? PennKeyHelper::CreateShaKey(m_predecessor) : 0;
  
  GraderLogs::RingState(GetLocalAddress(), ReverseLookup(GetLocalAddress()), myH,
                        m_predecessor, ReverseLookup(m_predecessor), pH,
                        m_successor, ReverseLookup(m_successor), sH);

  // Forward RingState token
  PennChordMessage msg(PennChordMessage::RINGSTATE_REQ, GetNextTransactionId());
  msg.SetRingStateReq(GetLocalAddress()); // I am initiator
  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(msg);
  m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));
}

void PennChord::ProcessRingStateReq(PennChordMessage message, Ipv4Address sourceAddress)
{
  Ipv4Address initiator = message.GetRingStateReq().initiator;

  if (initiator == GetLocalAddress()) {
      // Token returned to sender. Ring complete.
      GraderLogs::EndOfRingState();
      return;
  }

  // Otherwise, print self and forward
  uint32_t myH = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t sH = (m_successor != Ipv4Address::GetAny()) ? PennKeyHelper::CreateShaKey(m_successor) : 0;
  uint32_t pH = (m_predecessor != Ipv4Address::GetAny()) ? PennKeyHelper::CreateShaKey(m_predecessor) : 0;

  GraderLogs::RingState(GetLocalAddress(), ReverseLookup(GetLocalAddress()), myH,
                        m_predecessor, ReverseLookup(m_predecessor), pH,
                        m_successor, ReverseLookup(m_successor), sH);

  // Forward
  PennChordMessage msg(PennChordMessage::RINGSTATE_REQ, GetNextTransactionId());
  msg.SetRingStateReq(initiator); // Preserve initiator
  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(msg);
  m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));
}

// --- Lookup Logic (Updated for Join) ---

void PennChord::ProcessLookupRsp(PennChordMessage message, Ipv4Address sourceAddress)
{
  uint32_t txn = message.GetTransactionId();
  Ipv4Address owner = message.GetLookupRsp().ownerNode;

  // 1. Check if this is a JOIN response
  if (txn == m_joinTransactionId) {
      m_successor = owner;
      m_isJoined = true;
      m_predecessor = Ipv4Address::GetAny();
      InitFingerTable(); // Init table (succ is now known)
      StartPeriodicStabilization();
      CHORD_LOG("Joined successfully. Successor is " << ReverseLookup(m_successor));
      return;
  }

  // 2. Publish context
  auto itPub = m_publishContext.find(txn);
  if (itPub != m_publishContext.end()) {
    if (!m_publishLookupFn.IsNull()) m_publishLookupFn(itPub->second.first, itPub->second.second, owner);
    m_publishContext.erase(itPub);
    m_lookupHopCounter.erase(txn);
    return;
  }
  
  // 3. Search context
  auto it = m_searchContext.find(txn);
  if (it != m_searchContext.end()) {
    if (!m_searchLookupFn.IsNull()) m_searchLookupFn(it->second, owner);
    m_searchContext.erase(it);
    m_lookupHopCounter.erase(txn);
    return;
  }

  // 4. Generic callback
  if (!m_lookupResultFn.IsNull()) m_lookupResultFn(message.GetLookupRsp().lookupKey, owner);
  m_lookupHopCounter.erase(txn);
}

// --- Standard Chord Utilities ---

// Check if target is in (start, end)
bool PennChord::IsBetween(Ipv4Address target, Ipv4Address start, Ipv4Address end)
{
  uint32_t hStart = PennKeyHelper::CreateShaKey(start);
  uint32_t hEnd   = PennKeyHelper::CreateShaKey(end);
  uint32_t hTgt   = PennKeyHelper::CreateShaKey(target);

  if (hStart < hEnd) return (hTgt > hStart && hTgt < hEnd);
  else if (hStart > hEnd) return (hTgt > hStart || hTgt < hEnd);
  else return (hTgt != hStart);
}

// Helper: Semi-Open Interval (start, end]
static bool IsBetweenHashSemiOpen(uint32_t target, uint32_t start, uint32_t end)
{
    if (start < end) return (target > start && target <= end);
    else if (start > end) return (target > start || target <= end);
    else return (target == start); // Full circle
}

void PennChord::ProcessLookupReq(PennChordMessage message, Ipv4Address sourceAddress)
{
  uint32_t keyHash = message.GetLookupReq().lookupKey;
  Ipv4Address originator = message.GetLookupReq().originator;
  uint32_t txn = message.GetTransactionId();
  m_lookupHopCounter[txn]++;

  uint32_t localHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t predHash  = PennKeyHelper::CreateShaKey(m_predecessor);

  bool amOwner = (m_predecessor == Ipv4Address::GetAny()) ? true : IsBetweenHashSemiOpen(keyHash, predHash, localHash);

  if (amOwner) {
      CHORD_LOG(GraderLogs::GetLookupResultLogStr(localHash, keyHash, ReverseLookup(originator), PennKeyHelper::CreateShaKey(originator)));
      PennChordMessage rsp(PennChordMessage::LOOKUP_RSP, txn);
      rsp.SetLookupRsp(keyHash, GetLocalAddress());
      Ptr<Packet> pkt = Create<Packet>();
      pkt->AddHeader(rsp);
      m_socket->SendTo(pkt, 0, InetSocketAddress(originator, m_appPort));
  } else {
      Ipv4Address nextHop = FindSuccessor(keyHash);
      if (nextHop == GetLocalAddress()) nextHop = m_successor; 

      CHORD_LOG(GraderLogs::GetLookupForwardingLogStr(localHash, ReverseLookup(nextHop), PennKeyHelper::CreateShaKey(nextHop), keyHash));
      PennChordMessage fwd(PennChordMessage::LOOKUP_FORWARD, txn);
      fwd.SetLookupForward(keyHash, originator, GetLocalAddress());
      Ptr<Packet> pkt = Create<Packet>();
      pkt->AddHeader(fwd);
      m_socket->SendTo(pkt, 0, InetSocketAddress(nextHop, m_appPort));
  }
}

void PennChord::ProcessLookupForward(PennChordMessage message, Ipv4Address sourceAddress)
{
    // Logic identical to ProcessLookupReq, just logging handling is implicitly same
    ProcessLookupReq(message, sourceAddress); 
}

// Find closest finger preceding id
Ipv4Address PennChord::FindSuccessor(uint32_t id)
{
  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  if (IsBetweenHashSemiOpen(id, myHash, PennKeyHelper::CreateShaKey(m_successor))) return m_successor;
  
  Ipv4Address closest = ClosestPrecedingFinger(id);
  if (closest == GetLocalAddress()) return m_successor;
  return closest;
}

Ipv4Address PennChord::ClosestPrecedingFinger(uint32_t id)
{
  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  for (int i = m_fingerTable.size() - 1; i >= 0; --i) {
    Ipv4Address fingerSucc = m_fingerTable[i].successor;
    uint32_t fingerHash = PennKeyHelper::CreateShaKey(fingerSucc);
    
    if (myHash < id) { 
        if (fingerHash > myHash && fingerHash < id) return fingerSucc; 
    } else { 
        if (fingerHash > myHash || fingerHash < id) return fingerSucc; 
    }
  }
  return GetLocalAddress();
}

void PennChord::InitFingerTable()
{
  m_fingerTable.clear();
  m_fingerIndex = 0;
  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  for (int i = 0; i < 32; ++i) {
    FingerEntry entry;
    entry.start = myHash + (1 << i);
    entry.successor = m_successor;
    m_fingerTable.push_back(entry);
  }
}

void PennChord::FixFingers()
{
  if (!m_isJoined) return;
  if (m_fingerTable.empty()) InitFingerTable();

  m_fingerIndex = (m_fingerIndex % 32) + 1;
  size_t i = m_fingerIndex - 1;
  m_fingerTable[i].successor = FindSuccessor(m_fingerTable[i].start);
  m_fixFingersTimer.Schedule(Seconds(0.1));
}

void PennChord::IssueChordLookup(uint32_t keyHash, Ipv4Address originator)
{
    uint32_t txn = GetNextTransactionId();
    m_lookupHopCounter[txn] = 0;
    uint32_t myKey = PennKeyHelper::CreateShaKey(GetLocalAddress());
    CHORD_LOG(GraderLogs::GetLookupIssueLogStr(myKey, keyHash));
    
    // Start local processing (check ownership or forward)
    PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
    msg.SetLookupReq(keyHash, originator, GetLocalAddress());
    ProcessLookupReq(msg, Ipv4Address::GetAny());
}

// Helper stubs
void PennChord::SetPingSuccessCallback (Callback <void, Ipv4Address, std::string> cb) { m_pingSuccessFn = cb; }
void PennChord::SetPingFailureCallback (Callback <void, Ipv4Address, std::string> cb) { m_pingFailureFn = cb; }
void PennChord::SetPingRecvCallback (Callback <void, Ipv4Address, std::string> cb) { m_pingRecvFn = cb; }
void PennChord::SetLookupResultCallback(Callback<void, uint32_t, Ipv4Address> cb) { m_lookupResultFn = cb; }
void PennChord::SetSearchLookupCallback(Callback<void, std::string, Ipv4Address> cb) { m_searchLookupFn = cb; }
void PennChord::SetPublishLookupCallback(Callback<void, std::string, std::string, Ipv4Address> cb) { m_publishLookupFn = cb; }
uint32_t PennChord::GetNextTransactionId () { return m_currentTransactionId++; }
void PennChord::LeaveChord() { StopApplication(); } // Simplified leave

// Callbacks wrappers for start lookup
void PennChord::StartSearchLookup(std::string contextKey, uint32_t keyHash) {
    uint32_t txn = GetNextTransactionId();
    m_lookupHopCounter[txn] = 0;
    m_searchContext[txn] = contextKey;
    uint32_t myKey = PennKeyHelper::CreateShaKey(GetLocalAddress());
    CHORD_LOG(GraderLogs::GetLookupIssueLogStr(myKey, keyHash));
    
    PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
    msg.SetLookupReq(keyHash, GetLocalAddress(), GetLocalAddress());
    ProcessLookupReq(msg, Ipv4Address::GetAny());
}

void PennChord::StartPublishLookup(const std::string &keyword, const std::string &docId, uint32_t keyHash) {
    uint32_t txn = GetNextTransactionId();
    m_lookupHopCounter[txn] = 0;
    m_publishContext[txn] = {keyword, docId};
    uint32_t myKey = PennKeyHelper::CreateShaKey(GetLocalAddress());
    CHORD_LOG(GraderLogs::GetLookupIssueLogStr(myKey, keyHash));

    PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
    msg.SetLookupReq(keyHash, GetLocalAddress(), GetLocalAddress());
    ProcessLookupReq(msg, Ipv4Address::GetAny());
}

// PING Handlers (Standard)
void PennChord::SendPing (Ipv4Address destAddress, std::string pingMessage) {
    if (destAddress == Ipv4Address::GetAny()) { m_pingFailureFn(destAddress, pingMessage); return; }
    uint32_t txn = GetNextTransactionId();
    CHORD_LOG("Sending PING_REQ to " << ReverseLookup(destAddress));
    Ptr<PingRequest> pr = Create<PingRequest>(txn, Simulator::Now(), destAddress, pingMessage);
    m_pingTracker.insert(std::make_pair(txn, pr));
    PennChordMessage msg(PennChordMessage::PING_REQ, txn); msg.SetPingReq(pingMessage);
    Ptr<Packet> p = Create<Packet>(); p->AddHeader(msg);
    m_socket->SendTo(p, 0, InetSocketAddress(destAddress, m_appPort));
}
void PennChord::ProcessPingReq (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort) {
    CHORD_LOG("Recv PING_REQ from " << ReverseLookup(sourceAddress));
    PennChordMessage rsp(PennChordMessage::PING_RSP, message.GetTransactionId()); rsp.SetPingRsp(message.GetPingReq().pingMessage);
    Ptr<Packet> p = Create<Packet>(); p->AddHeader(rsp); m_socket->SendTo(p, 0, InetSocketAddress(sourceAddress, sourcePort));
    m_pingRecvFn(sourceAddress, message.GetPingReq().pingMessage);
}
void PennChord::ProcessPingRsp (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort) {
    auto it = m_pingTracker.find(message.GetTransactionId());
    if (it != m_pingTracker.end()) {
        CHORD_LOG("Recv PING_RSP from " << ReverseLookup(sourceAddress));
        m_pingTracker.erase(it); m_pingSuccessFn(sourceAddress, message.GetPingRsp().pingMessage);
    }
}
void PennChord::AuditPings() {
    for (auto it = m_pingTracker.begin(); it != m_pingTracker.end();) {
        if (it->second->GetTimestamp().GetMilliSeconds() + m_pingTimeout.GetMilliSeconds() <= Simulator::Now().GetMilliSeconds()) {
            m_pingFailureFn(it->second->GetDestinationAddress(), it->second->GetPingMessage());
            m_pingTracker.erase(it++);
        } else ++it;
    }
    m_auditPingsTimer.Schedule(m_pingTimeout);
}