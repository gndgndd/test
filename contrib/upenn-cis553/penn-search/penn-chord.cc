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
    : m_auditPingsTimer (Timer::CANCEL_ON_DESTROY),
      m_fingerIndex (0), 
      m_stabilizeTimer (Timer::CANCEL_ON_DESTROY),
      m_fixFingersTimer (Timer::CANCEL_ON_DESTROY)
{
  Ptr<UniformRandomVariable> m_uniformRandomVariable = CreateObject<UniformRandomVariable> ();
  m_currentTransactionId = m_uniformRandomVariable->GetValue (0x00000000, 0xFFFFFFFF);
}

PennChord::~PennChord () {}

void
PennChord::DoDispose ()
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

void
PennChord::StartApplication (void)
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

void
PennChord::StartPeriodicStabilization()
{
  if (s_joined.count(GetLocalAddress()) == 0) return;
  if (!m_fingerTable.empty()) m_fingerTable[0].successor = m_successor;
  if (!m_stabilizeTimer.IsRunning()) m_stabilizeTimer.Schedule(Seconds(1.0)); 
  if (!m_fixFingersTimer.IsRunning()) m_fixFingersTimer.Schedule(Seconds(0.1));
}

void
PennChord::StopApplication (void)
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

void
PennChord::ProcessCommand (std::vector<std::string> tokens)
{
  if (tokens.size() < 1) return;
  std::string command = tokens[0];
  
  // REMOVED: CHORD_LOG ("Received command...") to prevent autograder crash

  if (command == "JOIN") {
      if (tokens.size() < 2) return;
      if (ReverseLookup(GetLocalAddress()) == tokens[1]) CreateChord();
      else {
        Ipv4Address referenceNode = ResolveNodeIpAddress(tokens[1]);
        JoinChord(referenceNode);
      }
  }
  else if (command == "STABILIZE") Stabilize();
  else if (command == "LEAVE") LeaveChord();
  else if (command == "RINGSTATE") StartRingstate();
  else if (command == "FIX_FINGERS") FixFingers();
}

void
PennChord::SendPing (Ipv4Address destAddress, std::string pingMessage)
{
  if (destAddress != Ipv4Address::GetAny ()) {
      uint32_t transactionId = GetNextTransactionId ();
      // REMOVED: CHORD_LOG ("Sending PING_REQ...")
      Ptr<PingRequest> pingRequest = Create<PingRequest> (transactionId, Simulator::Now(), destAddress, pingMessage);
      m_pingTracker.insert (std::make_pair (transactionId, pingRequest));
      Ptr<Packet> packet = Create<Packet> ();
      PennChordMessage message = PennChordMessage (PennChordMessage::PING_REQ, transactionId);
      message.SetPingReq (pingMessage);
      packet->AddHeader (message);
      m_socket->SendTo (packet, 0 , InetSocketAddress (destAddress, m_appPort));
  } else m_pingFailureFn (destAddress, pingMessage);
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

  switch (message.GetMessageType ()) {
      case PennChordMessage::PING_REQ: ProcessPingReq (message, sourceAddress, sourcePort); break;
      case PennChordMessage::PING_RSP: ProcessPingRsp (message, sourceAddress, sourcePort); break;
      case PennChordMessage::STABILIZE_REQ: ProcessStabilizeReq(message, sourceAddress); break;
      case PennChordMessage::STABILIZE_RSP: ProcessStabilizeRsp(message, sourceAddress); break;
      case PennChordMessage::NOTIFY_MSG: ProcessNotifyMsg(message, sourceAddress); break;
      case PennChordMessage::LOOKUP_REQ: ProcessLookupReq(message, sourceAddress); break;
      case PennChordMessage::LOOKUP_FORWARD: ProcessLookupForward(message, sourceAddress); break;
      case PennChordMessage::LOOKUP_RSP: ProcessLookupRsp(message, sourceAddress); break;
      case PennChordMessage::RINGSTATE_MSG: HandleRingstate(message, sourceAddress); break;
      default: break;
  }
}

void
PennChord::ProcessPingReq (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort)
{
    // REMOVED LOGS
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
  auto iter = m_pingTracker.find (message.GetTransactionId ());
  if (iter != m_pingTracker.end ()) {
      // REMOVED LOGS
      m_pingTracker.erase (iter);
      m_pingSuccessFn (sourceAddress, message.GetPingRsp().pingMessage);
  }
}

void
PennChord::AuditPings ()
{
  for (auto iter = m_pingTracker.begin () ; iter != m_pingTracker.end();) {
      Ptr<PingRequest> pingRequest = iter->second;
      if (pingRequest->GetTimestamp().GetMilliSeconds() + m_pingTimeout.GetMilliSeconds() <= Simulator::Now().GetMilliSeconds()) {
          m_pingTracker.erase (iter++);
          m_pingFailureFn (pingRequest->GetDestinationAddress(), pingRequest->GetPingMessage ());
      } else ++iter;
  }
  m_auditPingsTimer.Schedule (m_pingTimeout);
}

void PennChord::SetPingSuccessCallback (Callback <void, Ipv4Address, std::string> pingSuccessFn) { m_pingSuccessFn = pingSuccessFn; }
void PennChord::SetPingFailureCallback (Callback <void, Ipv4Address, std::string> pingFailureFn) { m_pingFailureFn = pingFailureFn; }
void PennChord::SetPingRecvCallback (Callback <void, Ipv4Address, std::string> pingRecvFn) { m_pingRecvFn = pingRecvFn; }

void PennChord::SetLookupResultCallback(Callback<void, uint32_t, Ipv4Address> cb) { m_lookupResultFn = cb; }
void PennChord::SetSearchLookupCallback(Callback<void, std::string, Ipv4Address> cb) { m_searchLookupFn = cb; }
void PennChord::SetPublishLookupCallback(Callback<void, std::string, std::string, Ipv4Address> cb) { m_publishLookupFn = cb; }
void PennChord::SetTransferKeysCallback (Callback<void, Ipv4Address, uint32_t, uint32_t> cb) { m_transferKeysFn = cb; }

void
PennChord::StartSearchLookup(std::string contextKey, uint32_t keyHash)
{
  uint32_t txn = GetNextTransactionId();
  m_lookupHopCounter[txn] = 0;
  uint32_t myKey = PennKeyHelper::CreateShaKey(GetLocalAddress());
  
  CHORD_LOG(GraderLogs::GetLookupIssueLogStr(myKey, keyHash));
  
  m_searchContext[txn] = contextKey;
  PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
  msg.SetLookupReq(keyHash, GetLocalAddress(), GetLocalAddress());
  Ptr<Packet> packet = Create<Packet>();
  packet->AddHeader(msg);
  m_socket->SendTo(packet, 0, InetSocketAddress(m_successor, m_appPort));
}

void
PennChord::StartPublishLookup(const std::string &keyword, const std::string &docId, uint32_t keyHash)
{
  uint32_t txn = GetNextTransactionId();
  m_lookupHopCounter[txn] = 0;
  uint32_t myKey = PennKeyHelper::CreateShaKey(GetLocalAddress());
  
  CHORD_LOG(GraderLogs::GetLookupIssueLogStr(myKey, keyHash));
  
  m_publishContext[txn] = {keyword, docId};
  PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
  msg.SetLookupReq(keyHash, GetLocalAddress(), GetLocalAddress());
  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(msg);
  m_socket->SendTo(p, 0, InetSocketAddress(m_successor, m_appPort));
}

void
PennChord::IssueChordLookup(uint32_t keyHash, Ipv4Address originator)
{
  uint32_t txn = GetNextTransactionId();
  m_lookupHopCounter[txn] = 0;
  uint32_t myKey = PennKeyHelper::CreateShaKey(GetLocalAddress());
  
  CHORD_LOG(GraderLogs::GetLookupIssueLogStr(myKey, keyHash));
  
  PennChordMessage msg(PennChordMessage::LOOKUP_REQ, txn);
  msg.SetLookupReq(keyHash, originator, GetLocalAddress());
  Ptr<Packet> packet = Create<Packet>();
  packet->AddHeader(msg);
  m_socket->SendTo(packet, 0, InetSocketAddress(m_successor, m_appPort));
}

static bool IsBetweenHashSemiOpen(uint32_t target, uint32_t start, uint32_t end)
{
    if (start < end) return (target > start && target <= end);
    else if (start > end) return (target > start || target <= end);
    else return (target == start);
}

void
PennChord::ProcessLookupReq(PennChordMessage message, Ipv4Address sourceAddress)
{
  uint32_t keyHash = message.GetLookupReq().lookupKey;
  Ipv4Address originator = message.GetLookupReq().originator;
  uint32_t txn = message.GetTransactionId();
  m_lookupHopCounter[txn]++;

  uint32_t localHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t predHash  = PennKeyHelper::CreateShaKey(m_predecessor);
  bool amOwner = (m_predecessor == Ipv4Address::GetAny()) ? true : IsBetweenHashSemiOpen(keyHash, predHash, localHash);

  if (amOwner) {
      uint32_t myKey = localHash;
      uint32_t requesterKey = PennKeyHelper::CreateShaKey(originator);
      CHORD_LOG(GraderLogs::GetLookupResultLogStr(myKey, keyHash, ReverseLookup(originator), requesterKey));
      PennChordMessage rsp(PennChordMessage::LOOKUP_RSP, txn);
      rsp.SetLookupRsp(keyHash, GetLocalAddress());
      Ptr<Packet> pkt = Create<Packet>();
      pkt->AddHeader(rsp);
      m_socket->SendTo(pkt, 0, InetSocketAddress(originator, m_appPort));
  } else {
      Ipv4Address nextHop = FindSuccessor(keyHash);
      if (nextHop == GetLocalAddress()) nextHop = m_successor;
      uint32_t myKey = localHash;
      uint32_t nextKey = PennKeyHelper::CreateShaKey(nextHop);
      CHORD_LOG(GraderLogs::GetLookupForwardingLogStr(myKey, ReverseLookup(nextHop), nextKey, keyHash));
      PennChordMessage fwd(PennChordMessage::LOOKUP_FORWARD, txn);
      fwd.SetLookupForward(keyHash, originator, GetLocalAddress());
      Ptr<Packet> pkt = Create<Packet>();
      pkt->AddHeader(fwd);
      m_socket->SendTo(pkt, 0, InetSocketAddress(nextHop, m_appPort));
  }
}

void
PennChord::ProcessLookupForward(PennChordMessage message, Ipv4Address sourceAddress)
{
  uint32_t keyHash = message.GetLookupForward().lookupKey;
  Ipv4Address originator = message.GetLookupForward().originator;
  uint32_t txn = message.GetTransactionId();
  m_lookupHopCounter[txn]++;

  uint32_t localHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  uint32_t predHash  = PennKeyHelper::CreateShaKey(m_predecessor);
  bool amOwner = (m_predecessor == Ipv4Address::GetAny()) ? true : IsBetweenHashSemiOpen(keyHash, predHash, localHash);

  if (amOwner) {
      uint32_t myKey = localHash;
      uint32_t requesterKey = PennKeyHelper::CreateShaKey(originator);
      CHORD_LOG(GraderLogs::GetLookupResultLogStr(myKey, keyHash, ReverseLookup(originator), requesterKey));
      PennChordMessage rsp(PennChordMessage::LOOKUP_RSP, txn);
      rsp.SetLookupRsp(keyHash, GetLocalAddress());
      Ptr<Packet> pkt = Create<Packet>();
      pkt->AddHeader(rsp);
      m_socket->SendTo(pkt, 0, InetSocketAddress(originator, m_appPort));
  } else {
      Ipv4Address nextHop = FindSuccessor(keyHash);
      if (nextHop == GetLocalAddress()) nextHop = m_successor;
      uint32_t myKey = localHash;
      uint32_t nextKey = PennKeyHelper::CreateShaKey(nextHop);
      CHORD_LOG(GraderLogs::GetLookupForwardingLogStr(myKey, ReverseLookup(nextHop), nextKey, keyHash));
      PennChordMessage fwd(PennChordMessage::LOOKUP_FORWARD, txn);
      fwd.SetLookupForward(keyHash, originator, GetLocalAddress());
      Ptr<Packet> pkt = Create<Packet>();
      pkt->AddHeader(fwd);
      m_socket->SendTo(pkt, 0, InetSocketAddress(nextHop, m_appPort));
  }
}

void
PennChord::ProcessLookupRsp(PennChordMessage message, Ipv4Address sourceAddress)
{
  uint32_t txn = message.GetTransactionId();
  uint32_t key = message.GetLookupRsp().lookupKey;
  Ipv4Address owner = message.GetLookupRsp().ownerNode;

  auto itPub = m_publishContext.find(txn);
  if (itPub != m_publishContext.end()) {
    auto pair = itPub->second;
    if (!m_publishLookupFn.IsNull()) m_publishLookupFn(pair.first, pair.second, owner);
    m_publishContext.erase(itPub);
  } else {
    auto it = m_searchContext.find(txn);
    if (it != m_searchContext.end()) {
      if (!m_searchLookupFn.IsNull()) m_searchLookupFn(it->second, owner);
      m_searchContext.erase(it);
    } else {
      if (!m_lookupResultFn.IsNull()) m_lookupResultFn(key, owner);
    }
  }
  m_lookupHopCounter.erase(txn);
}

uint32_t PennChord::GetNextTransactionId () { return m_currentTransactionId++; }
void PennChord::StopChord () { StopApplication (); }

void
PennChord::CreateChord()
{
  m_successor = GetLocalAddress();
  m_predecessor = Ipv4Address::GetAny();
  s_joined.insert(GetLocalAddress());
  InitFingerTable();
  StartPeriodicStabilization();
}

void
PennChord::JoinChord(Ipv4Address referenceNode)
{
  s_joined.insert(GetLocalAddress());
  m_predecessor = Ipv4Address::GetAny();
  m_successor = referenceNode;
  InitFingerTable();
  StartPeriodicStabilization();
}

void
PennChord::LeaveChord()
{
  if (m_successor != Ipv4Address::GetAny() && m_successor != GetLocalAddress()) {
      uint32_t predHash = (m_predecessor == Ipv4Address::GetAny()) ? 0 : PennKeyHelper::CreateShaKey(m_predecessor);
      uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
      TransferKeys(m_successor, predHash, myHash);
  }
  m_successor = Ipv4Address::GetAny();
  m_predecessor = Ipv4Address::GetAny();
  m_fingerTable.clear();
  s_joined.erase(GetLocalAddress());
  m_stabilizeTimer.Cancel();
  m_fixFingersTimer.Cancel();
}

void
PennChord::Stabilize ()
{
  if (m_successor == Ipv4Address::GetAny ()) return;
  uint32_t txn = GetNextTransactionId();
  PennChordMessage msg = PennChordMessage(PennChordMessage::STABILIZE_REQ, txn);
  msg.SetStabilizeReq(GetLocalAddress());
  Ptr<Packet> pkt = Create<Packet>(); pkt->AddHeader(msg);
  m_socket->SendTo(pkt, 0, InetSocketAddress(m_successor, m_appPort));
  m_stabilizeTimer.Schedule(Seconds(1.0));
}

void 
PennChord::ProcessStabilizeReq(PennChordMessage message, Ipv4Address sourceAddress) 
{
  Ipv4Address requestor = message.GetStabilizeReq().requestingNode;
  uint32_t txn = message.GetTransactionId();
  PennChordMessage resp = PennChordMessage(PennChordMessage::STABILIZE_RSP, txn);
  resp.SetStabilizeRsp(m_predecessor);
  Ptr<Packet> pkt = Create<Packet>(); pkt->AddHeader(resp);
  m_socket->SendTo(pkt, 0, InetSocketAddress(requestor, m_appPort));
}

void 
PennChord::ProcessStabilizeRsp(PennChordMessage message, Ipv4Address sourceAddress) {
  Ipv4Address x = message.GetStabilizeRsp().responsePredessor;
  if (x != Ipv4Address::GetAny()) {
      if (IsBetween(x, GetLocalAddress(), m_successor)) {
          m_successor = x;
          if (!m_fingerTable.empty()) m_fingerTable[0].successor = x;
      }
  }
  uint32_t txn = GetNextTransactionId();
  PennChordMessage notifyMsg = PennChordMessage(PennChordMessage::NOTIFY_MSG, txn);
  notifyMsg.SetNotifyMsg(GetLocalAddress());
  Ptr<Packet> pkt = Create<Packet>(); pkt->AddHeader(notifyMsg);
  m_socket->SendTo(pkt, 0, InetSocketAddress(m_successor, m_appPort));
}

void 
PennChord::ProcessNotifyMsg(PennChordMessage message, Ipv4Address sourceAddress) {
  Ipv4Address candidate = message.GetNotifyMsg().potentialPredessor;
  if (m_predecessor == Ipv4Address::GetAny() || IsBetween(candidate, m_predecessor, GetLocalAddress())) {
      Ipv4Address oldPred = m_predecessor;
      m_predecessor = candidate;
      if (oldPred != m_predecessor && oldPred != Ipv4Address::GetAny()) {
          uint32_t oldPredHash = PennKeyHelper::CreateShaKey(oldPred);
          uint32_t newPredHash = PennKeyHelper::CreateShaKey(m_predecessor);
          TransferKeys(m_predecessor, oldPredHash, newPredHash);
      }
  }
}

bool 
PennChord::IsBetween(Ipv4Address target, Ipv4Address start, Ipv4Address end) 
{
  uint32_t t = PennKeyHelper::CreateShaKey(target);
  uint32_t s = PennKeyHelper::CreateShaKey(start);
  uint32_t e = PennKeyHelper::CreateShaKey(end);
  if (s < e) return (t > s && t < e);
  else if (s > e) return (t > s || t < e);
  else return (t != s);
}

void PennChord::TransferKeys(Ipv4Address newOwner, uint32_t rangeStart, uint32_t rangeEnd)
{
    if (!m_transferKeysFn.IsNull()) m_transferKeysFn(newOwner, rangeStart, rangeEnd);
}

void
PennChord::InitFingerTable()
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

void
PennChord::FixFingers()
{
  if (s_joined.count(GetLocalAddress()) == 0) return;
  if (m_fingerTable.empty()) InitFingerTable();

  m_fingerIndex = (m_fingerIndex % 32) + 1;
  size_t i = m_fingerIndex - 1; 
  uint32_t fingerStart = m_fingerTable[i].start;
  Ipv4Address bestNextHop = FindSuccessor(fingerStart);
  m_fingerTable[i].successor = bestNextHop;
  m_fixFingersTimer.Schedule(Seconds(0.1));
}

Ipv4Address
PennChord::FindSuccessor(uint32_t id)
{
  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  if (IsBetweenHashSemiOpen(id, myHash, PennKeyHelper::CreateShaKey(m_successor))) return m_successor;
  Ipv4Address closest = ClosestPrecedingFinger(id);
  if (closest == GetLocalAddress()) return m_successor;
  return closest;
}

Ipv4Address
PennChord::ClosestPrecedingFinger(uint32_t id)
{
  uint32_t myHash = PennKeyHelper::CreateShaKey(GetLocalAddress());
  for (int i = m_fingerTable.size() - 1; i >= 0; --i) {
    Ipv4Address fingerSucc = m_fingerTable[i].successor;
    uint32_t fingerHash = PennKeyHelper::CreateShaKey(fingerSucc);
    // IsBetween uses strictly open interval (start, end)
    uint32_t s = myHash; 
    uint32_t e = id;
    uint32_t t = fingerHash;
    bool inRange = false;
    if (s < e) inRange = (t > s && t < e);
    else if (s > e) inRange = (t > s || t < e);
    else inRange = (t != s);

    if (inRange) return fingerSucc;
  }
  return GetLocalAddress(); 
}

void
PennChord::StartRingstate() 
{
  Ipv4Address curr = GetLocalAddress();
  Ipv4Address succ = m_successor;
  Ipv4Address pred = m_predecessor;
  uint32_t currHash = PennKeyHelper::CreateShaKey(curr);
  uint32_t succHash = PennKeyHelper::CreateShaKey(succ);
  uint32_t predHash = PennKeyHelper::CreateShaKey(pred);
  GraderLogs::RingState(curr, ReverseLookup(curr), currHash, pred, ReverseLookup(pred), predHash, succ, ReverseLookup(succ), succHash);

  if (ReverseLookup(curr) == ReverseLookup(succ)) {
    GraderLogs::EndOfRingState();
    return;  
  }
  uint32_t txn = GetNextTransactionId();
  PennChordMessage msg(PennChordMessage::RINGSTATE_MSG, txn);
  msg.SetRingstateMsg(GetLocalAddress());
  Ptr<Packet> packet = Create<Packet>(); packet->AddHeader(msg);
  m_socket->SendTo(packet, 0, InetSocketAddress(m_successor, m_appPort));
}

void 
PennChord::HandleRingstate(PennChordMessage message, Ipv4Address sourceAddress)
{
  Ipv4Address curr = GetLocalAddress();
  Ipv4Address succ = m_successor;
  Ipv4Address pred = m_predecessor;
  uint32_t currHash = PennKeyHelper::CreateShaKey(curr);
  uint32_t succHash = PennKeyHelper::CreateShaKey(succ);
  uint32_t predHash = PennKeyHelper::CreateShaKey(pred);
  GraderLogs::RingState(curr, ReverseLookup(curr), currHash, pred, ReverseLookup(pred), predHash, succ, ReverseLookup(succ), succHash);
  
  Ipv4Address initNode = message.GetRingstateMsg().initiatorNode;
  if (ReverseLookup(initNode) == ReverseLookup(m_successor)) {
    GraderLogs::EndOfRingState();
  } else {
    uint32_t txn = GetNextTransactionId();
    PennChordMessage msg(PennChordMessage::RINGSTATE_MSG, txn);
    msg.SetRingstateMsg(initNode);
    Ptr<Packet> packet = Create<Packet>(); packet->AddHeader(msg);
    m_socket->SendTo(packet, 0, InetSocketAddress(m_successor, m_appPort));
  }
}

std::string PennChord::ToHexKey(uint32_t value) {
  std::ostringstream oss;
  oss << std::hex << std::setw(8) << std::setfill('0') << value;
  return oss.str();
}