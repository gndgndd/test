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

std::map<Ipv4Address, Ipv4Address> PennChord::m_successorPredecessor;
std::set<Ipv4Address> PennChord::s_joined;

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("PennChord");

TypeId PennChord::GetTypeId () {
  static TypeId tid = TypeId ("PennChord").SetParent<PennApplication> ().AddConstructor<PennChord> ()
            .AddAttribute ("AppPort", "Listening port", UintegerValue (10001), MakeUintegerAccessor (&PennChord::m_appPort), MakeUintegerChecker<uint16_t> ())
            .AddAttribute ("PingTimeout", "Timeout", TimeValue (MilliSeconds (2000)), MakeTimeAccessor (&PennChord::m_pingTimeout), MakeTimeChecker ());
  return tid;
}

PennChord::PennChord () : m_auditPingsTimer (Timer::CANCEL_ON_DESTROY), m_fingerIndex (0), m_stabilizeTimer (Timer::CANCEL_ON_DESTROY), m_fixFingersTimer (Timer::CANCEL_ON_DESTROY) {
  Ptr<UniformRandomVariable> m_uniformRandomVariable = CreateObject<UniformRandomVariable> ();
  m_currentTransactionId = m_uniformRandomVariable->GetValue (0x00000000, 0xFFFFFFFF);
}

PennChord::~PennChord () {}

void PennChord::DoDispose () {
  uint64_t totalHops = 0;
  uint32_t totalLookups = 0;
  for (auto const& entry : m_lookupHopCounter) { totalHops += entry.second; totalLookups++; }
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
  // Initialize Hash
  m_nodeHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  
  m_auditPingsTimer.SetFunction (&PennChord::AuditPings, this);
  m_stabilizeTimer.SetFunction (&PennChord::Stabilize, this);
  m_fixFingersTimer.SetFunction (&PennChord::FixFingers, this);
  m_auditPingsTimer.Schedule (m_pingTimeout);
}

void PennChord::StartPeriodicStabilization() {
  if (s_joined.count(GetLocalAddress()) == 0) return;
  if (!m_fingerTable.empty()) m_fingerTable[0].successor = m_successor;
  if (!m_stabilizeTimer.IsRunning()) m_stabilizeTimer.Schedule(Seconds(1.0));
  if (!m_fixFingersTimer.IsRunning()) m_fixFingersTimer.Schedule(Seconds(0.1));
}

void PennChord::StopApplication (void) {
  if (m_socket) { m_socket->Close (); m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ()); m_socket = 0; }
  m_auditPingsTimer.Cancel (); m_stabilizeTimer.Cancel (); m_fixFingersTimer.Cancel ();
  m_pingTracker.clear ();
}

void PennChord::ProcessCommand (std::vector<std::string> tokens) {
  if (tokens.size() < 1) return;
  std::string command = tokens[0];
  if (command == "JOIN") {
      if (tokens.size() < 2) return;
      if (ReverseLookup(GetLocalAddress()) == tokens[1]) CreateChord();
      else JoinChord(ResolveNodeIpAddress(tokens[1]));
  } else if (command == "STABILIZE") { Stabilize(); }
    else if (command == "LEAVE") { LeaveChord(); }
    else if (command == "RINGSTATE") { Ringstate(); }
    else if (command == "FIX_FINGERS") { FixFingers(); }
}

void PennChord::SendPing (Ipv4Address destAddress, std::string pingMessage) {
  if (destAddress != Ipv4Address::GetAny ()) {
      uint32_t transactionId = GetNextTransactionId ();
      CHORD_LOG ("Sending PING_REQ to Node: " << ReverseLookup(destAddress) << " IP: " << destAddress << " Message: " << pingMessage << " transactionId: " << transactionId);
      Ptr<PingRequest> pingRequest = Create<PingRequest> (transactionId, Simulator::Now(), destAddress, pingMessage);
      m_pingTracker.insert (std::make_pair (transactionId, pingRequest));
      Ptr<Packet> packet = Create<Packet> ();
      PennChordMessage message = PennChordMessage (PennChordMessage::PING_REQ, transactionId);
      message.SetPingReq (pingMessage);
      packet->AddHeader (message);
      m_socket->SendTo (packet, 0 , InetSocketAddress (destAddress, m_appPort));
  } else { m_pingFailureFn (destAddress, pingMessage); }
}

void PennChord::RecvMessage (Ptr<Socket> socket) {
  Address sourceAddr;
  Ptr<Packet> packet = socket->RecvFrom (sourceAddr);
  InetSocketAddress inetSocketAddr = InetSocketAddress::ConvertFrom (sourceAddr);
  Ipv4Address sourceAddress = inetSocketAddr.GetIpv4 ();
  uint16_t sourcePort = inetSocketAddr.GetPort ();
  PennChordMessage message;
  packet->RemoveHeader (message);

  switch (message.GetMessageType ()) {
      case PennChordMessage::PING_REQ: ProcessPingReq (message, sourceAddress, sourcePort); break;
      case PennChordMessage::PING_RSP: ProcessPingRsp (message, sourceAddress, sourcePort); break;
      case PennChordMessage::LOOKUP_REQ: ProcessLookupReq(message, sourceAddress); break;
      case PennChordMessage::LOOKUP_FORWARD: ProcessLookupForward(message, sourceAddress); break;
      case PennChordMessage::LOOKUP_RSP: ProcessLookupRsp(message, sourceAddress); break;
      case PennChordMessage::RINGSTATE_MSG: HandleRingstate(message, sourceAddress); break;
      case PennChordMessage::STABILIZE_REQ: ProcessStabilizeReq(message); break;
      case PennChordMessage::STABILIZE_RSP: ProcessStabilizeRsp(message); break;
      case PennChordMessage::NOTIFY_PKT: ProcessNotifyPkt(message); break;
      default: ERROR_LOG ("Unknown Message Type!"); break;
    }
}

void PennChord::ProcessPingReq (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort) {
    std::string fromNode = ReverseLookup (sourceAddress);
    CHORD_LOG ("Received PING_REQ, From Node: " << fromNode << ", Message: " << message.GetPingReq().pingMessage);
    PennChordMessage resp = PennChordMessage (PennChordMessage::PING_RSP, message.GetTransactionId());
    resp.SetPingRsp (message.GetPingReq().pingMessage);
    Ptr<Packet> packet = Create<Packet> ();
    packet->AddHeader (resp);
    m_socket->SendTo (packet, 0 , InetSocketAddress (sourceAddress, sourcePort));
    m_pingRecvFn (sourceAddress, message.GetPingReq().pingMessage);
}

void PennChord::ProcessPingRsp (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort) {
  std::map<uint32_t, Ptr<PingRequest> >::iterator iter;
  iter = m_pingTracker.find (message.GetTransactionId ());
  if (iter != m_pingTracker.end ()) {
      std::string fromNode = ReverseLookup (sourceAddress);
      CHORD_LOG ("Received PING_RSP, From Node: " << fromNode << ", Message: " << message.GetPingRsp().pingMessage);
      m_pingTracker.erase (iter);
      m_pingSuccessFn (sourceAddress, message.GetPingRsp().pingMessage);
  } else { DEBUG_LOG ("Received invalid PING_RSP!"); }
}

void PennChord::AuditPings () {
  std::map<uint32_t, Ptr<PingRequest> >::iterator iter;
  for (iter = m_pingTracker.begin () ; iter != m_pingTracker.end();) {
      Ptr<PingRequest> pingRequest = iter->second;
      if (pingRequest->GetTimestamp().GetMilliSeconds() + m_pingTimeout.GetMilliSeconds() <= Simulator::Now().GetMilliSeconds()) {
          m_pingTracker.erase (iter++);
          m_pingFailureFn (pingRequest->GetDestinationAddress(), pingRequest->GetPingMessage ());
      } else { ++iter; }
  }
  m_auditPingsTimer.Schedule (m_pingTimeout);
}

void PennChord::SetLookupResultCallback(Callback<void, uint32_t, Ipv4Address> lookupResultFn) { m_lookupResultFn = lookupResultFn; }

void PennChord::StartSearchLookup(std::string contextKey, uint32_t keyHash) {
  uint32_t txn = GetNextTransactionId();
  m_lookupHopCounter[txn] = 0;
  uint32_t myKey = PennKeyHelper::CreateShaKey(GetLocalAddress());
  CHORD_LOG(GraderLogs::GetLookupIssueLogStr(myKey, keyHash));
  m_searchContext[txn] = contextKey;
  PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
  msg.SetLookupReq(keyHash, GetLocalAddress(), GetLocalAddress());
  Ptr<Packet> packet = Create<Packet>(); packet->AddHeader(msg);
  m_socket->SendTo(packet, 0, InetSocketAddress(m_successor, m_appPort));
}

void PennChord::StartPublishLookup(const std::string &keyword, const std::string &docId, uint32_t keyHash) {
  uint32_t txn = GetNextTransactionId();
  m_lookupHopCounter[txn] = 0;
  uint32_t myKey = PennKeyHelper::CreateShaKey(GetLocalAddress());
  CHORD_LOG(GraderLogs::GetLookupIssueLogStr(myKey, keyHash));
  m_publishContext[txn] = {keyword, docId};
  PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
  msg.SetLookupReq(keyHash, GetLocalAddress(), GetLocalAddress());
  Ptr<Packet> p = Create<Packet>(); p->AddHeader(msg);
  m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));
}

void PennChord::IssueChordLookup(uint32_t keyHash, Ipv4Address originator) {
  uint32_t txn = GetNextTransactionId();
  m_lookupHopCounter[txn] = 0;
  uint32_t myKey = PennKeyHelper::CreateShaKey(GetLocalAddress());
  CHORD_LOG(GraderLogs::GetLookupIssueLogStr(myKey, keyHash));
  PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
  msg.SetLookupReq(keyHash, originator, GetLocalAddress());
  Ptr<Packet> packet = Create<Packet>(); packet->AddHeader(msg);
  m_socket->SendTo(packet, 0, InetSocketAddress(m_successor, m_appPort));
}

static bool IsBetweenHashSemiOpen(uint32_t target, uint32_t start, uint32_t end) {
    if (start < end) return (target > start && target <= end);
    else if (start > end) return (target > start || target <= end);
    else return (target == start);
}

void PennChord::ProcessLookupReq(PennChordMessage message, Ipv4Address sourceAddress) {
  uint32_t keyHash = message.GetLookupReq().lookupKey;
  Ipv4Address originator = message.GetLookupReq().originator;
  uint32_t txn = message.GetTransactionId();
  m_lookupHopCounter[txn]++;
  uint32_t localHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t predHash  = PennKeyHelper::CreateShaKey(m_predecessor);
  bool amOwner = false;
  if (m_predecessor == Ipv4Address::GetAny()) amOwner = true;
  else amOwner = IsBetweenHashSemiOpen(keyHash, predHash, localHash);

  if (amOwner) {
      CHORD_LOG(GraderLogs::GetLookupResultLogStr(localHash, keyHash, ReverseLookup(originator), PennKeyHelper::CreateShaKey(originator)));
      PennChordMessage rsp(PennChordMessage::LOOKUP_RSP, txn);
      rsp.SetLookupRsp(keyHash, GetLocalAddress());
      Ptr<Packet> pkt = Create<Packet>(); pkt->AddHeader(rsp);
      m_socket->SendTo(pkt, 0, InetSocketAddress(originator, m_appPort));
      return;
  }
  Ipv4Address nextHop = FindSuccessor(keyHash);
  if (nextHop == GetLocalAddress()) nextHop = m_successor;
  CHORD_LOG(GraderLogs::GetLookupForwardingLogStr(localHash, ReverseLookup(nextHop), PennKeyHelper::CreateShaKey(nextHop), keyHash));
  PennChordMessage fwd(PennChordMessage::LOOKUP_FORWARD, txn);
  fwd.SetLookupForward(keyHash, originator, GetLocalAddress());
  Ptr<Packet> pkt = Create<Packet>(); pkt->AddHeader(fwd);
  m_socket->SendTo(pkt, 0, InetSocketAddress(nextHop, m_appPort));
}

void PennChord::ProcessLookupForward(PennChordMessage message, Ipv4Address sourceAddress) {
  uint32_t keyHash = message.GetLookupForward().lookupKey;
  Ipv4Address originator = message.GetLookupForward().originator;
  uint32_t txn = message.GetTransactionId();
  m_lookupHopCounter[txn]++;
  uint32_t localHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t predHash  = PennKeyHelper::CreateShaKey(m_predecessor);
  bool amOwner = false;
  if (m_predecessor == Ipv4Address::GetAny()) amOwner = true;
  else amOwner = IsBetweenHashSemiOpen(keyHash, predHash, localHash);

  if (amOwner) {
      CHORD_LOG(GraderLogs::GetLookupResultLogStr(localHash, keyHash, ReverseLookup(originator), PennKeyHelper::CreateShaKey(originator)));
      PennChordMessage rsp(PennChordMessage::LOOKUP_RSP, txn);
      rsp.SetLookupRsp(keyHash, GetLocalAddress());
      Ptr<Packet> pkt = Create<Packet>(); pkt->AddHeader(rsp);
      m_socket->SendTo(pkt, 0, InetSocketAddress(originator, m_appPort));
      return;
  }
  Ipv4Address nextHop = FindSuccessor(keyHash);
  if (nextHop == GetLocalAddress()) nextHop = m_successor;
  CHORD_LOG(GraderLogs::GetLookupForwardingLogStr(localHash, ReverseLookup(nextHop), PennKeyHelper::CreateShaKey(nextHop), keyHash));
  PennChordMessage fwd(PennChordMessage::LOOKUP_FORWARD, txn);
  fwd.SetLookupForward(keyHash, originator, GetLocalAddress());
  Ptr<Packet> pkt = Create<Packet>(); pkt->AddHeader(fwd);
  m_socket->SendTo(pkt, 0, InetSocketAddress(nextHop, m_appPort));
}

void PennChord::ProcessLookupRsp(PennChordMessage message, Ipv4Address sourceAddress) {
  uint32_t txn = message.GetTransactionId();
  uint32_t key = message.GetLookupRsp().lookupKey;
  Ipv4Address owner = message.GetLookupRsp().ownerNode;
  auto itPub = m_publishContext.find(txn);
  if (itPub != m_publishContext.end()) {
    if (!m_publishLookupFn.IsNull()) m_publishLookupFn(itPub->second.first, itPub->second.second, owner);
    m_publishContext.erase(itPub); m_lookupHopCounter.erase(txn); return;
  }
  auto it = m_searchContext.find(txn);
  if (it != m_searchContext.end()) {
    if (!m_searchLookupFn.IsNull()) m_searchLookupFn(it->second, owner);
    m_searchContext.erase(it); m_lookupHopCounter.erase(txn); return;
  }
  if (!m_lookupResultFn.IsNull()) m_lookupResultFn(key, owner);
  m_lookupHopCounter.erase(txn);
}

uint32_t PennChord::GetNextTransactionId () { return m_currentTransactionId++; }
void PennChord::StopChord () { StopApplication (); }
void PennChord::SetPingSuccessCallback (Callback <void, Ipv4Address, std::string> pingSuccessFn) { m_pingSuccessFn = pingSuccessFn; }
void PennChord::SetPingFailureCallback (Callback <void, Ipv4Address, std::string> pingFailureFn) { m_pingFailureFn = pingFailureFn; }
void PennChord::SetPingRecvCallback (Callback <void, Ipv4Address, std::string> pingRecvFn) { m_pingRecvFn = pingRecvFn; }
void PennChord::SetSearchLookupCallback(Callback<void, std::string, Ipv4Address> cb) { m_searchLookupFn = cb; }
void PennChord::SetPublishLookupCallback(Callback<void, std::string, std::string, Ipv4Address> cb) { m_publishLookupFn = cb; }

void PennChord::CreateChord() {
  m_successor = GetLocalAddress();
  m_predecessor = Ipv4Address::GetAny();
  m_successorPredecessor[GetLocalAddress()] = GetLocalAddress();
  s_joined.insert(GetLocalAddress());
  InitFingerTable();
  StartPeriodicStabilization();
}

Ipv4Address PennChord::FindSuccessor(uint32_t id) {
  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  if (IsBetweenHashSemiOpen(id, myHash, PennKeyHelper::CreateShaKey(m_successor))) return m_successor;
  Ipv4Address closest = ClosestPrecedingFinger(id);
  if (closest == GetLocalAddress()) return m_successor;
  return closest;
}

Ipv4Address PennChord::ClosestPrecedingFinger(uint32_t id) {
  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  for (int i = m_fingerTable.size() - 1; i >= 0; --i) {
    Ipv4Address fingerSucc = m_fingerTable[i].successor;
    uint32_t fingerHash = PennKeyHelper::CreateShaKey(fingerSucc);
    if (IsInBetween(fingerHash, myHash, id)) return fingerSucc;
  }
  return GetLocalAddress();
}

void PennChord::JoinChord(Ipv4Address referenceNode) {
  s_joined.insert(GetLocalAddress());
  m_predecessor = Ipv4Address::GetAny();
  InitFingerTable();
  m_successor = referenceNode;
  StartPeriodicStabilization();
}

void PennChord::LeaveChord() {
  if (m_successor != Ipv4Address::GetAny() && m_successor != GetLocalAddress())
      TransferKeys(m_successor, GetLocalAddress(), m_predecessor);
  m_successor = Ipv4Address::GetAny();
  m_predecessor = Ipv4Address::GetAny();
  m_fingerTable.clear();
  s_joined.erase(GetLocalAddress());
  m_stabilizeTimer.Cancel();
  m_fixFingersTimer.Cancel();
  m_successorPredecessor.erase(GetLocalAddress());
}

// ---------------- STABILIZATION ----------------
void PennChord::Stabilize() {
  Ipv4Address dest = m_successor;
  uint32_t transactionId = GetNextTransactionId();
  PennChordMessage msg = PennChordMessage(PennChordMessage::STABILIZE_REQ, transactionId);
  msg.SetStabilizeReq(GetLocalAddress());
  Ptr<Packet> pkt = Create<Packet>(); pkt->AddHeader(msg);
  m_socket->SendTo(pkt, 0, InetSocketAddress(dest, m_appPort));
  m_stabilizeTimer.Schedule(MilliSeconds(600)); 
}

void PennChord::ProcessStabilizeReq(PennChordMessage message) {
  PennChordMessage::StabilizeReq req = message.GetStabilizeReq();
  Ipv4Address requestor = req.senderIp;
  uint32_t transactionId = message.GetTransactionId();
  PennChordMessage resp = PennChordMessage(PennChordMessage::STABILIZE_RSP, transactionId);
  resp.SetStabilizeRsp(m_predecessor);
  Ptr<Packet> pkt = Create<Packet>(); pkt->AddHeader(resp);
  m_socket->SendTo(pkt, 0, InetSocketAddress(requestor, m_appPort));
}

void PennChord::ProcessStabilizeRsp(PennChordMessage message) {
  PennChordMessage::StabilizeRsp rsp = message.GetStabilizeRsp();
  Ipv4Address x = rsp.predecessorIp; 
  if (x != Ipv4Address::GetAny()) {
      uint32_t myID = m_nodeHash;
      uint32_t succID = PennKeyHelper::CreateShaKey(m_successor);
      uint32_t xID = PennKeyHelper::CreateShaKey(x);
      if (IsInBetween(xID, myID, succID)) m_successor = x; 
  }
  uint32_t transactionId = GetNextTransactionId();
  PennChordMessage notifyMsg = PennChordMessage(PennChordMessage::NOTIFY_PKT, transactionId);
  notifyMsg.SetNotifyPkt(GetLocalAddress());
  Ptr<Packet> pkt = Create<Packet>(); pkt->AddHeader(notifyMsg);
  m_socket->SendTo(pkt, 0, InetSocketAddress(m_successor, m_appPort));
}

void PennChord::ProcessNotifyPkt(PennChordMessage message) {
  PennChordMessage::NotifyPkt notif = message.GetNotifyPkt();
  Ipv4Address candidate = notif.candidateIp;
  uint32_t candidateID = PennKeyHelper::CreateShaKey(candidate);
  uint32_t currentPredID = PennKeyHelper::CreateShaKey(m_predecessor);
  uint32_t myID = m_nodeHash;
  if (m_predecessor == Ipv4Address::GetAny() || IsInBetween(candidateID, currentPredID, myID)) {
      m_predecessor = candidate;
  }
}

// -----------------------------------------------

bool PennChord::IsInBetween(uint32_t idToCheck, uint32_t start, uint32_t end) const {
  if (start < end) return (idToCheck > start && idToCheck < end);
  else if (start > end) return (idToCheck > start || idToCheck < end);
  else return true;
}

void PennChord::Notify(Ipv4Address potentialPred) {
    // Legacy local method left for interface compliance if needed, but ProcessNotifyPkt handles this in distributed mode
    uint32_t candidateID = PennKeyHelper::CreateShaKey(potentialPred);
    uint32_t currentPredID = PennKeyHelper::CreateShaKey(m_predecessor);
    uint32_t myID = m_nodeHash;
    if (m_predecessor == Ipv4Address::GetAny() || IsInBetween(candidateID, currentPredID, myID)) {
      m_predecessor = potentialPred;
  }
}

void PennChord::TransferKeys(Ipv4Address newOwner, Ipv4Address oldOwner, Ipv4Address boundaryNode) {
    CHORD_LOG("[TransferKeys] Signaling App Layer to manage keys.");
}

void PennChord::InitFingerTable() {
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

void PennChord::FixFingers() {
  if (s_joined.count(GetLocalAddress()) == 0) return;
  if (m_fingerTable.empty()) InitFingerTable();
  m_fingerIndex = (m_fingerIndex % 32) + 1;
  size_t i = m_fingerIndex - 1; 
  uint32_t fingerStart = m_fingerTable[i].start;
  Ipv4Address bestNextHop = FindSuccessor(fingerStart);
  m_fingerTable[i].successor = bestNextHop;
  m_fixFingersTimer.Schedule(Seconds(0.1));
}

std::string PennChord::ToHexKey(uint32_t value) {
  std::ostringstream oss;
  oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << value;
  return oss.str();
}

void PennChord::StartRingstate() {
  if (ReverseLookup(GetLocalAddress()) == ReverseLookup(m_successor)) {
    uint32_t hash = PennKeyHelper::CreateShaKey(GetLocalAddress());
    GraderLogs::RingState(GetLocalAddress(), ReverseLookup(GetLocalAddress()), hash, Ipv4Address::GetAny(), "", 0, Ipv4Address::GetAny(), "", 0);
    GraderLogs::EndOfRingState();
    return;  
  }
  Ipv4Address curr = GetLocalAddress(); Ipv4Address succ = m_successor; Ipv4Address pred = m_predecessor;
  GraderLogs::RingState(curr, ReverseLookup(curr), PennKeyHelper::CreateShaKey(curr), pred, ReverseLookup(pred), PennKeyHelper::CreateShaKey(pred), succ, ReverseLookup(succ), PennKeyHelper::CreateShaKey(succ));
  uint32_t txn = GetNextTransactionId();
  PennChordMessage msg(PennChordMessage::RINGSTATE_MSG, txn);
  msg.setRingstateMsg(GetLocalAddress());
  Ptr<Packet> packet = Create<Packet>(); packet->AddHeader(msg);
  m_socket->SendTo(packet, 0, InetSocketAddress(m_successor, m_appPort));
}

void PennChord::HandleRingstate(PennChordMessage message, Ipv4Address sourceAddress) {
  Ipv4Address curr = GetLocalAddress(); Ipv4Address succ = m_successor; Ipv4Address pred = m_predecessor;
  GraderLogs::RingState(curr, ReverseLookup(curr), PennKeyHelper::CreateShaKey(curr), pred, ReverseLookup(pred), PennKeyHelper::CreateShaKey(pred), succ, ReverseLookup(succ), PennKeyHelper::CreateShaKey(succ));
  Ipv4Address initNode = message.getRingstateMsg().initiatorNode;
  if (ReverseLookup(initNode) == ReverseLookup(m_successor)) { GraderLogs::EndOfRingState(); return; }
  else {
    uint32_t txn = GetNextTransactionId();
    PennChordMessage msg(PennChordMessage::RINGSTATE_MSG, txn);
    msg.setRingstateMsg(initNode);
    Ptr<Packet> packet = Create<Packet>(); packet->AddHeader(msg);
    m_socket->SendTo(packet, 0, InetSocketAddress(m_successor, m_appPort));
  }
}

void PennChord::Ringstate() { StartRingstate(); }

bool PennChord::IsBetween(Ipv4Address target, Ipv4Address start, Ipv4Address end) {
  uint32_t hStart = PennKeyHelper::CreateShaKey(start);
  uint32_t hEnd   = PennKeyHelper::CreateShaKey(end);
  uint32_t hTgt   = PennKeyHelper::CreateShaKey(target);
  if (hStart < hEnd) return (hTgt > hStart && hTgt < hEnd);
  else if (hStart > hEnd) return (hTgt > hStart || hTgt < hEnd);
  else return (hTgt != hStart);
}

std::string HexString(uint32_t value) {
  std::ostringstream oss;
  oss << std::hex << std::setw(8) << std::setfill('0') << value;
  return oss.str();
}