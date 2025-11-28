/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "penn-chord.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4.h"
#include "ns3/random-variable-stream.h"
#include "ns3/penn-key-helper.h"
#include "ns3/grader-logs.h"
#include <iomanip>
#include <sstream>
#include <algorithm>

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("PennChord");

// ======================= STATIC HELPERS =======================
// Moved to top so member functions can see it
static bool IsBetweenHash(uint32_t t, uint32_t s, uint32_t e) {
    if (s < e) return t > s && t < e;
    else if (s > e) return t > s || t < e;
    else return false; // s == e, ring full or empty
}
// ==============================================================

TypeId PennChord::GetTypeId () {
  static TypeId tid = TypeId ("PennChord")
    .SetParent<PennApplication> ()
    .AddConstructor<PennChord> ()
    .AddAttribute ("AppPort", "Port", UintegerValue (10001),
                   MakeUintegerAccessor (&PennChord::m_appPort), MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("PingTimeout", "Timeout", TimeValue (MilliSeconds (2000)),
                   MakeTimeAccessor (&PennChord::m_pingTimeout), MakeTimeChecker ());
  return tid;
}

PennChord::PennChord ()
    : m_auditPingsTimer (Timer::CANCEL_ON_DESTROY),
      m_stabilizeTimer (Timer::CANCEL_ON_DESTROY),
      m_fixFingersTimer (Timer::CANCEL_ON_DESTROY)
{
  Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable> ();
  m_currentTransactionId = rng->GetValue (0, 0xFFFFFFFF);
  m_fingerIndex = 0;
  m_successor = Ipv4Address::GetAny();
  m_predecessor = Ipv4Address::GetAny();
}

PennChord::~PennChord () {}

void PennChord::DoDispose () {
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

void PennChord::StartApplication (void) {
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

void PennChord::StopApplication (void) {
  if (m_socket) { m_socket->Close (); m_socket = 0; }
  m_auditPingsTimer.Cancel ();
  m_stabilizeTimer.Cancel ();
  m_fixFingersTimer.Cancel ();
}

// FIX: Added missing implementation causing linker error
void PennChord::StopChord () {
  StopApplication();
}

void PennChord::ProcessCommand (std::vector<std::string> tokens) {
  if (tokens.empty()) return;
  std::string cmd = tokens[0];

  if (cmd == "JOIN") {
      if (tokens.size() < 2) return;
      if (ReverseLookup(GetLocalAddress()) == tokens[1]) CreateChord();
      else JoinChord(ResolveNodeIpAddress(tokens[1]));
  } else if (cmd == "STABILIZE") {
      Stabilize();
  } else if (cmd == "LEAVE") {
      LeaveChord();
  } else if (cmd == "RINGSTATE") {
      InitiateRingState();
  }
}

// ======================= MESSAGING =======================

void PennChord::SendPing (Ipv4Address dest, std::string msg) {
  if (dest == Ipv4Address::GetAny()) { m_pingFailureFn(dest, msg); return; }
  uint32_t txn = GetNextTransactionId ();
  Ptr<PingRequest> req = Create<PingRequest> (txn, Simulator::Now(), dest, msg);
  m_pingTracker[txn] = req;
  PennChordMessage pcm (PennChordMessage::PING_REQ, txn);
  pcm.SetPingReq (msg);
  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (pcm);
  m_socket->SendTo (packet, 0 , InetSocketAddress (dest, m_appPort));
}

void PennChord::RecvMessage (Ptr<Socket> socket) {
  Address from;
  Ptr<Packet> packet = socket->RecvFrom (from);
  Ipv4Address src = InetSocketAddress::ConvertFrom (from).GetIpv4 ();
  uint16_t port = InetSocketAddress::ConvertFrom (from).GetPort ();
  PennChordMessage msg;
  packet->RemoveHeader (msg);

  switch (msg.GetMessageType ()) {
    case PennChordMessage::PING_REQ: ProcessPingReq(msg, src, port); break;
    case PennChordMessage::PING_RSP: ProcessPingRsp(msg, src, port); break;
    case PennChordMessage::LOOKUP_REQ: ProcessLookupReq(msg, src); break;
    case PennChordMessage::LOOKUP_FORWARD: ProcessLookupForward(msg, src); break;
    case PennChordMessage::LOOKUP_RSP: ProcessLookupRsp(msg, src); break;
    case PennChordMessage::GET_PREDECESSOR: ProcessGetPredecessor(msg, src, port); break;
    case PennChordMessage::PREDECESSOR_RSP: ProcessPredecessorRsp(msg, src); break;
    case PennChordMessage::NOTIFY: ProcessNotify(msg, src); break;
    case PennChordMessage::RINGSTATE_REQ: ProcessRingStateReq(msg, src); break;
    default: break;
  }
}

// ======================= CHORD LOGIC =======================

void PennChord::CreateChord() {
  m_successor = GetLocalAddress();
  m_predecessor = Ipv4Address::GetAny();
  InitFingerTable();
  m_stabilizeTimer.Schedule(Seconds(1.0));
  m_fixFingersTimer.Schedule(Seconds(0.5));
}

void PennChord::JoinChord(Ipv4Address ref) {
  m_predecessor = Ipv4Address::GetAny();
  m_successor = ref; // Simple join, stabilize will fix
  InitFingerTable();
  m_stabilizeTimer.Schedule(Seconds(1.0));
  m_fixFingersTimer.Schedule(Seconds(0.5));
  Stabilize(); // Trigger immediate stabilization
}

void PennChord::LeaveChord() {
  // Transfer keys to successor before leaving
  if (m_successor != Ipv4Address::GetAny() && !m_keyTransferFn.IsNull()) {
      m_keyTransferFn(m_successor);
  }
  StopApplication();
}

// ======================= STABILIZATION =======================

void PennChord::Stabilize () {
  if (m_successor == Ipv4Address::GetAny()) return;
  // Send GET_PREDECESSOR to successor
  uint32_t txn = GetNextTransactionId();
  PennChordMessage msg(PennChordMessage::GET_PREDECESSOR, txn);
  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(msg);
  m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));
  
  // Reschedule
  m_stabilizeTimer.Schedule(Seconds(1.0));
}

void PennChord::ProcessGetPredecessor(PennChordMessage msg, Ipv4Address src, uint16_t port) {
  PennChordMessage rsp(PennChordMessage::PREDECESSOR_RSP, msg.GetTransactionId());
  rsp.SetPredecessorRsp(m_predecessor);
  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(rsp);
  m_socket->SendTo(p, 0, InetSocketAddress(src, port));
}

void PennChord::ProcessPredecessorRsp(PennChordMessage msg, Ipv4Address src) {
  Ipv4Address x = msg.GetPredecessorRsp().predecessorNode;
  
  // If x is between me and my successor, x is my new successor
  if (x != Ipv4Address::GetAny() && IsBetween(x, GetLocalAddress(), m_successor)) {
      m_successor = x;
      if (!m_fingerTable.empty()) m_fingerTable[0].successor = x;
  }
  Notify(m_successor);
}

void PennChord::Notify(Ipv4Address node) {
  uint32_t txn = GetNextTransactionId();
  PennChordMessage msg(PennChordMessage::NOTIFY, txn);
  msg.SetNotify(GetLocalAddress()); // I think I am your predecessor
  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(msg);
  m_socket->SendTo(p, 0, InetSocketAddress(node, m_appPort));
}

void PennChord::ProcessNotify(PennChordMessage msg, Ipv4Address src) {
  Ipv4Address potentialPred = msg.GetNotify().potentialPredecessor;
  
  // If I have no pred, or this new guy is between my current pred and me
  if (m_predecessor == Ipv4Address::GetAny() || 
      IsBetween(potentialPred, m_predecessor, GetLocalAddress())) {
      m_predecessor = potentialPred;
      // Transfer keys that now belong to the new predecessor
      if (!m_keyTransferFn.IsNull()) {
          m_keyTransferFn(m_predecessor);
      }
  }
}

// ======================= RING STATE =======================

void PennChord::InitiateRingState() {
  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t succHash = PennKeyHelper::CreateShaKey(m_successor);
  uint32_t predHash = (m_predecessor == Ipv4Address::GetAny()) ? 0 : PennKeyHelper::CreateShaKey(m_predecessor);

  // Log myself
  GraderLogs::RingState(
    GetLocalAddress(), ReverseLookup(GetLocalAddress()), myHash,
    m_predecessor, ReverseLookup(m_predecessor), predHash,
    m_successor, ReverseLookup(m_successor), succHash
  );

  // Send token to successor
  if (m_successor != Ipv4Address::GetAny() && m_successor != GetLocalAddress()) {
      PennChordMessage msg(PennChordMessage::RINGSTATE_REQ, GetNextTransactionId());
      msg.SetRingStateReq(GetLocalAddress()); // I am the origin
      Ptr<Packet> p = Create<Packet>();
      p->AddHeader(msg);
      m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));
  } else {
      GraderLogs::EndOfRingState();
  }
}

void PennChord::ProcessRingStateReq(PennChordMessage msg, Ipv4Address src) {
  Ipv4Address origin = msg.GetRingStateReq().originNode;

  if (origin == GetLocalAddress()) {
      // Loop complete
      GraderLogs::EndOfRingState();
      return;
  }

  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t succHash = PennKeyHelper::CreateShaKey(m_successor);
  uint32_t predHash = (m_predecessor == Ipv4Address::GetAny()) ? 0 : PennKeyHelper::CreateShaKey(m_predecessor);

  GraderLogs::RingState(
    GetLocalAddress(), ReverseLookup(GetLocalAddress()), myHash,
    m_predecessor, ReverseLookup(m_predecessor), predHash,
    m_successor, ReverseLookup(m_successor), succHash
  );

  // Forward to successor
  if (m_successor != Ipv4Address::GetAny()) {
      PennChordMessage fwd(PennChordMessage::RINGSTATE_REQ, GetNextTransactionId());
      fwd.SetRingStateReq(origin);
      Ptr<Packet> p = Create<Packet>();
      p->AddHeader(fwd);
      m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));
  }
}

// ======================= LOOKUPS & ROUTING =======================

void PennChord::IssueChordLookup(uint32_t key, Ipv4Address originator) {
  // Generic helper usually called by tests, no specific context needed
  // Note: If tests use this, they expect m_lookupResultFn to be called.
  uint32_t txn = GetNextTransactionId();
  m_lookupHopCounter[txn] = 0;
  
  CHORD_LOG(GraderLogs::GetLookupIssueLogStr(PennKeyHelper::CreateShaKey(GetLocalAddress()), key));

  PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
  msg.SetLookupReq(key, originator, GetLocalAddress());
  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(msg);
  m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));
}

void PennChord::StartSearchLookup(std::string contextKey, uint32_t keyHash) {
  uint32_t txn = GetNextTransactionId();
  m_lookupHopCounter[txn] = 0;
  LookupContext ctx; ctx.type = CTX_SEARCH; ctx.searchCtx = contextKey;
  m_lookupContexts[txn] = ctx;

  CHORD_LOG(GraderLogs::GetLookupIssueLogStr(PennKeyHelper::CreateShaKey(GetLocalAddress()), keyHash));
  
  PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
  msg.SetLookupReq(keyHash, GetLocalAddress(), GetLocalAddress());
  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(msg);
  m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));
}

void PennChord::StartPublishLookup(const std::string &kw, const std::string &doc, uint32_t hash) {
  uint32_t txn = GetNextTransactionId();
  m_lookupHopCounter[txn] = 0;
  LookupContext ctx; ctx.type = CTX_PUBLISH; ctx.pubKeyword = kw; ctx.pubDocId = doc;
  m_lookupContexts[txn] = ctx;

  CHORD_LOG(GraderLogs::GetLookupIssueLogStr(PennKeyHelper::CreateShaKey(GetLocalAddress()), hash));
  
  PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
  msg.SetLookupReq(hash, GetLocalAddress(), GetLocalAddress());
  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(msg);
  m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));
}

void PennChord::ProcessLookupReq(PennChordMessage msg, Ipv4Address src) {
  uint32_t key = msg.GetLookupReq().lookupKey;
  Ipv4Address origin = msg.GetLookupReq().originator;
  
  m_lookupHopCounter[msg.GetTransactionId()]++;

  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t predHash = (m_predecessor == Ipv4Address::GetAny()) ? myHash : PennKeyHelper::CreateShaKey(m_predecessor);
  
  // Am I the owner? (pred, me]
  bool owner = (m_predecessor == Ipv4Address::GetAny()) || IsBetweenHashSemiOpen(key, predHash, myHash);

  if (owner) {
    CHORD_LOG(GraderLogs::GetLookupResultLogStr(myHash, key, ReverseLookup(origin), PennKeyHelper::CreateShaKey(origin)));
    PennChordMessage rsp(PennChordMessage::LOOKUP_RSP, msg.GetTransactionId());
    rsp.SetLookupRsp(key, GetLocalAddress());
    Ptr<Packet> p = Create<Packet>();
    p->AddHeader(rsp);
    m_socket->SendTo(p, 0, InetSocketAddress(origin, m_appPort));
  } else {
    Ipv4Address next = FindSuccessor(key);
    CHORD_LOG(GraderLogs::GetLookupForwardingLogStr(myHash, ReverseLookup(next), PennKeyHelper::CreateShaKey(next), key));
    
    PennChordMessage fwd(PennChordMessage::LOOKUP_FORWARD, msg.GetTransactionId());
    fwd.SetLookupForward(key, origin, GetLocalAddress());
    Ptr<Packet> p = Create<Packet>();
    p->AddHeader(fwd);
    m_socket->SendTo(p, 0, InetSocketAddress(next, m_appPort));
  }
}

void PennChord::ProcessLookupForward(PennChordMessage msg, Ipv4Address src) {
  // Logic identical to ProcessLookupReq, just a different message type input
  uint32_t key = msg.GetLookupForward().lookupKey;
  Ipv4Address origin = msg.GetLookupForward().originator;
  m_lookupHopCounter[msg.GetTransactionId()]++;

  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t predHash = (m_predecessor == Ipv4Address::GetAny()) ? myHash : PennKeyHelper::CreateShaKey(m_predecessor);
  
  bool owner = (m_predecessor == Ipv4Address::GetAny()) || IsBetweenHashSemiOpen(key, predHash, myHash);

  if (owner) {
    CHORD_LOG(GraderLogs::GetLookupResultLogStr(myHash, key, ReverseLookup(origin), PennKeyHelper::CreateShaKey(origin)));
    PennChordMessage rsp(PennChordMessage::LOOKUP_RSP, msg.GetTransactionId());
    rsp.SetLookupRsp(key, GetLocalAddress());
    Ptr<Packet> p = Create<Packet>();
    p->AddHeader(rsp);
    m_socket->SendTo(p, 0, InetSocketAddress(origin, m_appPort));
  } else {
    Ipv4Address next = FindSuccessor(key);
    CHORD_LOG(GraderLogs::GetLookupForwardingLogStr(myHash, ReverseLookup(next), PennKeyHelper::CreateShaKey(next), key));
    
    PennChordMessage fwd(PennChordMessage::LOOKUP_FORWARD, msg.GetTransactionId());
    fwd.SetLookupForward(key, origin, GetLocalAddress());
    Ptr<Packet> p = Create<Packet>();
    p->AddHeader(fwd);
    m_socket->SendTo(p, 0, InetSocketAddress(next, m_appPort));
  }
}

void PennChord::ProcessLookupRsp(PennChordMessage msg, Ipv4Address src) {
  uint32_t txn = msg.GetTransactionId();
  Ipv4Address owner = msg.GetLookupRsp().ownerNode;
  
  if (m_lookupContexts.count(txn)) {
      LookupContext ctx = m_lookupContexts[txn];
      m_lookupContexts.erase(txn);
      
      if (ctx.type == CTX_SEARCH && !m_searchLookupFn.IsNull()) {
          m_searchLookupFn(ctx.searchCtx, owner);
      } else if (ctx.type == CTX_PUBLISH && !m_publishLookupFn.IsNull()) {
          m_publishLookupFn(ctx.pubKeyword, ctx.pubDocId, owner);
      } else if (ctx.type == CTX_FINGER_FIX) {
          // Update finger table
          if (ctx.fingerIndex < m_fingerTable.size()) {
              m_fingerTable[ctx.fingerIndex].successor = owner;
          }
      }
  } else if (!m_lookupResultFn.IsNull()) {
      m_lookupResultFn(msg.GetLookupRsp().lookupKey, owner);
  }
  m_lookupHopCounter.erase(txn);
}

// ======================= FINGER TABLE =======================

void PennChord::InitFingerTable() {
  m_fingerTable.clear();
  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  for (int i = 0; i < 32; ++i) {
      FingerEntry entry;
      entry.start = myHash + (1 << i);
      entry.successor = m_successor; 
      m_fingerTable.push_back(entry);
  }
}

void PennChord::FixFingers() {
  if (m_fingerTable.empty()) return;
  m_fingerIndex = (m_fingerIndex + 1) % 32;
  
  uint32_t start = m_fingerTable[m_fingerIndex].start;
  uint32_t txn = GetNextTransactionId();
  
  LookupContext ctx; ctx.type = CTX_FINGER_FIX; ctx.fingerIndex = m_fingerIndex;
  m_lookupContexts[txn] = ctx;
  
  // Issue lookup for the finger's start ID
  PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
  msg.SetLookupReq(start, GetLocalAddress(), GetLocalAddress());
  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(msg);
  // Send to best local guess (usually successor)
  m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));

  m_fixFingersTimer.Schedule(Seconds(0.5));
}

Ipv4Address PennChord::FindSuccessor(uint32_t id) {
    uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
    uint32_t succHash = PennKeyHelper::CreateShaKey(m_successor);

    if (IsBetweenHashSemiOpen(id, myHash, succHash)) return m_successor;
    
    Ipv4Address cp = ClosestPrecedingFinger(id);
    if (cp == GetLocalAddress()) return m_successor;
    return cp;
}

Ipv4Address PennChord::ClosestPrecedingFinger(uint32_t id) {
  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  for (int i = 31; i >= 0; --i) {
      uint32_t fingerHash = PennKeyHelper::CreateShaKey(m_fingerTable[i].successor);
      if (IsBetweenHash(fingerHash, myHash, id)) return m_fingerTable[i].successor;
  }
  return GetLocalAddress();
}

// ======================= HELPERS & PINGS =======================

void PennChord::ProcessPingReq (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort) {
    PennChordMessage resp (PennChordMessage::PING_RSP, message.GetTransactionId());
    resp.SetPingRsp (message.GetPingReq().pingMessage);
    Ptr<Packet> packet = Create<Packet> ();
    packet->AddHeader (resp);
    m_socket->SendTo (packet, 0 , InetSocketAddress (sourceAddress, sourcePort));
    if (!m_pingRecvFn.IsNull()) m_pingRecvFn (sourceAddress, message.GetPingReq().pingMessage);
}

void PennChord::ProcessPingRsp (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort) {
  auto iter = m_pingTracker.find (message.GetTransactionId ());
  if (iter != m_pingTracker.end ()) {
      m_pingTracker.erase (iter);
      if (!m_pingSuccessFn.IsNull()) m_pingSuccessFn (sourceAddress, message.GetPingRsp().pingMessage);
  }
}

void PennChord::AuditPings () {
  auto iter = m_pingTracker.begin ();
  while (iter != m_pingTracker.end()) {
      if (iter->second->GetTimestamp().GetMilliSeconds() + m_pingTimeout.GetMilliSeconds() <= Simulator::Now().GetMilliSeconds()) {
          if (!m_pingFailureFn.IsNull()) m_pingFailureFn(iter->second->GetDestinationAddress(), iter->second->GetPingMessage());
          m_pingTracker.erase(iter++);
      } else { ++iter; }
  }
  m_auditPingsTimer.Schedule (m_pingTimeout);
}

uint32_t PennChord::GetNextTransactionId () { return m_currentTransactionId++; }
void PennChord::SetPingSuccessCallback (Callback <void, Ipv4Address, std::string> cb) { m_pingSuccessFn = cb; }
void PennChord::SetPingFailureCallback (Callback <void, Ipv4Address, std::string> cb) { m_pingFailureFn = cb; }
void PennChord::SetPingRecvCallback (Callback <void, Ipv4Address, std::string> cb) { m_pingRecvFn = cb; }
void PennChord::SetLookupResultCallback(Callback<void, uint32_t, Ipv4Address> cb) { m_lookupResultFn = cb; }
void PennChord::SetSearchLookupCallback(Callback<void, std::string, Ipv4Address> cb) { m_searchLookupFn = cb; }
void PennChord::SetPublishLookupCallback(Callback<void, std::string, std::string, Ipv4Address> cb) { m_publishLookupFn = cb; }
void PennChord::SetKeyTransferCallback(Callback<void, Ipv4Address> cb) { m_keyTransferFn = cb; }

bool PennChord::IsBetween(Ipv4Address t, Ipv4Address s, Ipv4Address e) {
    uint32_t ht = PennKeyHelper::CreateShaKey(t);
    uint32_t hs = PennKeyHelper::CreateShaKey(s);
    uint32_t he = PennKeyHelper::CreateShaKey(e);
    return IsBetweenHash(ht, hs, he);
}

bool PennChord::IsBetweenHashSemiOpen(uint32_t t, uint32_t s, uint32_t e) {
    if (s < e) return t > s && t <= e;
    else if (s > e) return t > s || t <= e;
    else return true; // Full ring
}