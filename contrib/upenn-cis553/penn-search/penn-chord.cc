/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "penn-chord.h"
#include "ns3/log.h"
#include "ns3/inet-socket-address.h"
#include "ns3/random-variable-stream.h"
#include "ns3/penn-key-helper.h"
#include "ns3/grader-logs.h"
#include "ns3/uinteger.h" // Fixed: Added for UintegerValue
#include <vector>
#include <string>

using namespace ns3;

TypeId PennChord::GetTypeId () {
  static TypeId tid = TypeId ("PennChord")
            .SetParent<PennApplication> ()
            .AddConstructor<PennChord> ()
            .AddAttribute ("AppPort", "Port", UintegerValue (10001), MakeUintegerAccessor (&PennChord::m_appPort), MakeUintegerChecker<uint16_t> ())
            .AddAttribute ("PingTimeout", "Timeout", TimeValue (MilliSeconds (2000)), MakeTimeAccessor (&PennChord::m_pingTimeout), MakeTimeChecker ());
  return tid;
}

PennChord::PennChord () : m_auditPingsTimer (Timer::CANCEL_ON_DESTROY), m_stabilizeTimer(Timer::CANCEL_ON_DESTROY), m_fingerTimer(Timer::CANCEL_ON_DESTROY) {
  m_currentTransactionId = CreateObject<UniformRandomVariable> ()->GetValue (0, UINT32_MAX);
  m_nextFingerFix = 0;
  m_totalHops = 0;
  m_totalLookups = 0;
}

PennChord::~PennChord () {
    GraderLogs::AverageHopCount(GetNodeId(), m_totalLookups, m_totalHops);
}

void PennChord::DoDispose () { StopApplication (); PennApplication::DoDispose (); }

void PennChord::StartApplication (void) {
  if (!m_socket) { 
      m_socket = Socket::CreateSocket (GetNode (), TypeId::LookupByName ("ns3::UdpSocketFactory"));
      m_socket->Bind (InetSocketAddress (Ipv4Address::GetAny(), m_appPort));
      m_socket->SetRecvCallback (MakeCallback (&PennChord::RecvMessage, this));
  }
  
  // Setup Ring State
  m_myId = PennKeyHelper::CreateShaKey(GetLocalAddress());
  m_predecessor = {Ipv4Address::GetAny(), 0};
  
  m_fingers.resize(32);
  for(auto &f : m_fingers) { f.ip = GetLocalAddress(); f.id = m_myId; }

  // Timers
  m_auditPingsTimer.SetFunction (&PennChord::AuditPings, this);
  m_auditPingsTimer.Schedule (m_pingTimeout);
  
  m_stabilizeTimer.SetFunction(&PennChord::HandleStabilize, this);
  m_stabilizeTimer.Schedule(Seconds(1.0));
  
  m_fingerTimer.SetFunction(&PennChord::HandleFixFingers, this);
  m_fingerTimer.Schedule(Seconds(0.5));
}

void PennChord::StopApplication (void) {
  if (m_socket) { m_socket->Close (); m_socket = 0; }
  m_auditPingsTimer.Cancel(); m_stabilizeTimer.Cancel(); m_fingerTimer.Cancel();
}

void PennChord::ProcessCommand (std::vector<std::string> tokens) {
  if (tokens.empty()) return;
  std::string cmd = tokens[0];

  if (cmd == "JOIN" && tokens.size() > 1) {
      // Fixed: Cast std::stoi result to uint32_t to match variable type
      uint32_t landmark = (uint32_t)std::stoi(tokens[1]);
      uint32_t myNodeId = (uint32_t)std::stoi(GetNodeId());
      
      if (landmark != myNodeId) {
          Ipv4Address lmIp = ResolveNodeIpAddress(tokens[1]);
          InitiateLookup(m_myId, false); 
          PennChordMessage msg(PennChordMessage::FIND_SUCCESSOR_REQ, GetNextTransactionId());
          msg.SetFindSuccReq(m_myId, false);
          Ptr<Packet> p = Create<Packet>(); p->AddHeader(msg);
          m_socket->SendTo(p, 0, InetSocketAddress(lmIp, m_appPort));
      }
  } else if (cmd == "RINGSTATE") {
      GraderLogs::RingState(GetLocalAddress(), GetNodeId(), m_myId,
                            m_predecessor.ip, ReverseLookup(m_predecessor.ip), m_predecessor.id,
                            m_fingers[0].ip, ReverseLookup(m_fingers[0].ip), m_fingers[0].id);
      
      PennChordMessage msg(PennChordMessage::RING_STATE, 0);
      Ptr<Packet> p = Create<Packet>(); p->AddHeader(msg);
      m_socket->SendTo(p, 0, InetSocketAddress(m_fingers[0].ip, m_appPort));
  }
}

// --- Logic ---

bool PennChord::IsBetween(uint32_t k, uint32_t start, uint32_t end, bool incEnd) {
    if (start < end) return (k > start && (incEnd ? k <= end : k < end));
    return (k > start || (incEnd ? k <= end : k < end));
}

Ipv4Address PennChord::GetClosestPrecedingNode(uint32_t key) {
    for (int i = 31; i >= 0; i--) {
        if (m_fingers[i].ip != Ipv4Address::GetAny() && IsBetween(m_fingers[i].id, m_myId, key, false)) {
            return m_fingers[i].ip;
        }
    }
    return m_fingers[0].ip;
}

void PennChord::RecvMessage (Ptr<Socket> socket) {
  Address from;
  Ptr<Packet> p = socket->RecvFrom (from);
  Ipv4Address src = InetSocketAddress::ConvertFrom (from).GetIpv4();
  PennChordMessage msg;
  p->RemoveHeader (msg);

  switch (msg.GetMessageType()) {
      case PennChordMessage::PING_REQ: ProcessPingReq(msg, src, m_appPort); break;
      case PennChordMessage::PING_RSP: ProcessPingRsp(msg, src, m_appPort); break;
      case PennChordMessage::FIND_SUCCESSOR_REQ: OnFindSuccessorReq(msg, src); break;
      case PennChordMessage::FIND_SUCCESSOR_RSP: OnFindSuccessorRsp(msg, src); break;
      case PennChordMessage::NOTIFY_REQ: OnNotify(msg, src); break;
      case PennChordMessage::GET_PREDECESSOR_REQ: OnGetPredecessorReq(msg, src); break;
      case PennChordMessage::GET_PREDECESSOR_RSP: OnGetPredecessorRsp(msg, src); break;
      case PennChordMessage::RING_STATE: OnRingState(msg, src); break;
  }
}

void PennChord::OnFindSuccessorReq(PennChordMessage msg, Ipv4Address src) {
    auto req = msg.GetFindSuccReq();
    if (req.isApp) CHORD_LOG(GraderLogs::GetLookupIssueLogStr(m_myId, req.key));

    if (IsBetween(req.key, m_myId, m_fingers[0].id, true)) {
        PennChordMessage rsp(PennChordMessage::FIND_SUCCESSOR_RSP, msg.GetTransactionId());
        rsp.SetFindSuccRsp(m_fingers[0].ip, req.isApp);
        Ptr<Packet> p = Create<Packet>(); p->AddHeader(rsp);
        m_socket->SendTo(p, 0, InetSocketAddress(src, m_appPort));
    } else {
        Ipv4Address next = GetClosestPrecedingNode(req.key);
        if (req.isApp) {
            m_totalHops++;
            CHORD_LOG(GraderLogs::GetLookupForwardingLogStr(m_myId, ReverseLookup(next), PennKeyHelper::CreateShaKey(next), req.key));
        }
        Ptr<Packet> p = Create<Packet>(); p->AddHeader(msg); // Forward original
        m_socket->SendTo(p, 0, InetSocketAddress(next, m_appPort));
    }
}

void PennChord::OnFindSuccessorRsp(PennChordMessage msg, Ipv4Address src) {
    auto rsp = msg.GetFindSuccRsp();
    if (rsp.isApp) {
        if (!m_lookupResultCb.IsNull()) m_lookupResultCb(0, rsp.addr);
    } else {
        m_fingers[0].ip = rsp.addr;
        m_fingers[0].id = PennKeyHelper::CreateShaKey(rsp.addr);
    }
}

void PennChord::OnNotify(PennChordMessage msg, Ipv4Address src) {
    Ipv4Address candidate = msg.GetNotifyReq().addr;
    uint32_t cId = PennKeyHelper::CreateShaKey(candidate);
    if (m_predecessor.ip == Ipv4Address::GetAny() || IsBetween(cId, m_predecessor.id, m_myId, false)) {
        m_predecessor = {candidate, cId};
    }
}

void PennChord::HandleStabilize() {
    PennChordMessage msg(PennChordMessage::GET_PREDECESSOR_REQ, GetNextTransactionId());
    Ptr<Packet> p = Create<Packet>(); p->AddHeader(msg);
    m_socket->SendTo(p, 0, InetSocketAddress(m_fingers[0].ip, m_appPort));
    m_stabilizeTimer.Schedule(Seconds(1));
}

void PennChord::OnGetPredecessorReq(PennChordMessage msg, Ipv4Address src) {
    PennChordMessage rsp(PennChordMessage::GET_PREDECESSOR_RSP, msg.GetTransactionId());
    rsp.SetGetPredRsp(m_predecessor.ip);
    Ptr<Packet> p = Create<Packet>(); p->AddHeader(rsp);
    m_socket->SendTo(p, 0, InetSocketAddress(src, m_appPort));
}

void PennChord::OnGetPredecessorRsp(PennChordMessage msg, Ipv4Address src) {
    Ipv4Address x = msg.GetGetPredRsp().addr;
    if (x != Ipv4Address::GetAny()) {
        uint32_t xId = PennKeyHelper::CreateShaKey(x);
        if (IsBetween(xId, m_myId, m_fingers[0].id, false)) {
            m_fingers[0] = {x, xId};
        }
    }
    PennChordMessage nMsg(PennChordMessage::NOTIFY_REQ, GetNextTransactionId());
    nMsg.SetNotifyReq(GetLocalAddress());
    Ptr<Packet> p = Create<Packet>(); p->AddHeader(nMsg);
    m_socket->SendTo(p, 0, InetSocketAddress(m_fingers[0].ip, m_appPort));
}

void PennChord::OnRingState(PennChordMessage msg, Ipv4Address src) {
    GraderLogs::RingState(GetLocalAddress(), GetNodeId(), m_myId,
        m_predecessor.ip, ReverseLookup(m_predecessor.ip), m_predecessor.id,
        m_fingers[0].ip, ReverseLookup(m_fingers[0].ip), m_fingers[0].id);
    
    if (msg.GetTransactionId() == 0) {
         Ptr<Packet> p = Create<Packet>(); p->AddHeader(msg);
         m_socket->SendTo(p, 0, InetSocketAddress(m_fingers[0].ip, m_appPort));
         GraderLogs::EndOfRingState();
    }
}

void PennChord::HandleFixFingers() {
    m_nextFingerFix = (m_nextFingerFix + 1) % 32;
    uint32_t target = m_myId + (1 << m_nextFingerFix);
    InitiateLookup(target, false);
    m_fingerTimer.Schedule(Seconds(0.5));
}

void PennChord::InitiateLookup(uint32_t key, bool isApp) {
    if (isApp) {
        m_totalLookups++;
        CHORD_LOG(GraderLogs::GetLookupIssueLogStr(m_myId, key));
    }
    
    if (IsBetween(key, m_myId, m_fingers[0].id, true)) {
        if (isApp && !m_lookupResultCb.IsNull()) m_lookupResultCb(key, m_fingers[0].ip);
    } else {
        Ipv4Address next = GetClosestPrecedingNode(key);
        if (isApp) {
            m_totalHops++;
            CHORD_LOG(GraderLogs::GetLookupForwardingLogStr(m_myId, ReverseLookup(next), PennKeyHelper::CreateShaKey(next), key));
        }
        PennChordMessage msg(PennChordMessage::FIND_SUCCESSOR_REQ, GetNextTransactionId());
        msg.SetFindSuccReq(key, isApp);
        Ptr<Packet> p = Create<Packet>(); p->AddHeader(msg);
        m_socket->SendTo(p, 0, InetSocketAddress(next, m_appPort));
    }
}

void PennChord::SetLookupResultCallback(Callback<void, uint32_t, Ipv4Address> cb) { m_lookupResultCb = cb; }

// Ping methods
void PennChord::SendPing(Ipv4Address d, std::string m) {
    if (d == Ipv4Address::GetAny()) { if (!m_pingFailureFn.IsNull()) m_pingFailureFn(d, m); return; }
    uint32_t tx = GetNextTransactionId();
    Ptr<PingRequest> r = Create<PingRequest>(tx, Simulator::Now(), d, m);
    m_pingTracker[tx] = r;
    PennChordMessage msg(PennChordMessage::PING_REQ, tx); msg.SetPingReq(m);
    Ptr<Packet> p = Create<Packet>(); p->AddHeader(msg);
    m_socket->SendTo(p, 0, InetSocketAddress(d, m_appPort));
}
void PennChord::ProcessPingReq (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort) {
    PennChordMessage resp (PennChordMessage::PING_RSP, message.GetTransactionId());
    // Fixed: calling GetPingReq() returns struct, then .msg
    resp.SetPingRsp (message.GetPingReq().msg);
    Ptr<Packet> packet = Create<Packet> (); packet->AddHeader (resp);
    m_socket->SendTo (packet, 0 , InetSocketAddress (sourceAddress, sourcePort));
    if(!m_pingRecvFn.IsNull()) m_pingRecvFn (sourceAddress, message.GetPingReq().msg);
}
void PennChord::ProcessPingRsp (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort) {
  auto iter = m_pingTracker.find (message.GetTransactionId ());
  if (iter != m_pingTracker.end ()) {
      m_pingTracker.erase (iter);
      // Fixed: calling GetPingRsp() returns struct, then .msg
      if(!m_pingSuccessFn.IsNull()) m_pingSuccessFn (sourceAddress, message.GetPingRsp().msg);
  }
}
void PennChord::AuditPings () { m_auditPingsTimer.Schedule (m_pingTimeout); }
uint32_t PennChord::GetNextTransactionId () { return m_currentTransactionId++; }
void PennChord::StopChord () { StopApplication (); }
void PennChord::SetPingSuccessCallback (Callback <void, Ipv4Address, std::string> f) { m_pingSuccessFn = f; }
void PennChord::SetPingFailureCallback (Callback <void, Ipv4Address, std::string> f) { m_pingFailureFn = f; }
void PennChord::SetPingRecvCallback (Callback <void, Ipv4Address, std::string> f) { m_pingRecvFn = f; }