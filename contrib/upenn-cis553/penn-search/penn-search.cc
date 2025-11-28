/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "penn-search.h"
#include "ns3/grader-logs.h"
#include "ns3/random-variable-stream.h"
#include "ns3/inet-socket-address.h"
#include "ns3/penn-key-helper.h"
#include <fstream>
#include <sstream>

using namespace ns3;

TypeId PennSearch::GetTypeId () {
  static TypeId tid = TypeId ("PennSearch")
    .SetParent<PennApplication> ()
    .AddConstructor<PennSearch> ()
    .AddAttribute ("AppPort", "Port", UintegerValue (10000), MakeUintegerAccessor (&PennSearch::m_appPort), MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("ChordPort", "Port", UintegerValue (10001), MakeUintegerAccessor (&PennSearch::m_chordPort), MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("PingTimeout", "Timeout", TimeValue (MilliSeconds (2000)), MakeTimeAccessor (&PennSearch::m_pingTimeout), MakeTimeChecker ());
  return tid;
}

PennSearch::PennSearch () : m_auditPingsTimer (Timer::CANCEL_ON_DESTROY) {
  m_chord = NULL;
  Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable> ();
  m_currentTransactionId = rng->GetValue (0, 0xFFFFFFFF);
}

PennSearch::~PennSearch () {}

void PennSearch::DoDispose () {
  StopApplication ();
  PennApplication::DoDispose ();
  GraderLogs::HelloGrader (ReverseLookup (GetLocalAddress ()), GetLocalAddress ());
}

void PennSearch::StartApplication (void) {
  ObjectFactory factory;
  factory.SetTypeId (PennChord::GetTypeId ());
  factory.Set ("AppPort", UintegerValue (m_chordPort));
  m_chord = factory.Create<PennChord> ();
  m_chord->SetNode (GetNode ());
  m_chord->SetNodeAddressMap (m_nodeAddressMap);
  m_chord->SetAddressNodeMap (m_addressNodeMap);
  m_chord->SetModuleName ("CHORD");
  m_chord->SetNodeId (GetNodeId ());
  m_chord->SetLocalAddress (m_local);

  m_chord->SetPingSuccessCallback (MakeCallback (&PennSearch::HandleChordPingSuccess, this));
  m_chord->SetPingFailureCallback (MakeCallback (&PennSearch::HandleChordPingFailure, this));
  m_chord->SetPingRecvCallback    (MakeCallback (&PennSearch::HandleChordPingRecv, this));
  m_chord->SetSearchLookupCallback (MakeCallback (&PennSearch::HandleSearchChordLookup, this));
  m_chord->SetPublishLookupCallback (MakeCallback (&PennSearch::HandlePublishChordLookup, this));
  m_chord->SetKeyTransferCallback (MakeCallback (&PennSearch::HandleKeyTransfer, this));

  m_chord->SetStartTime (Simulator::Now ());
  m_chord->Initialize ();

  if (m_socket == 0) {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), m_appPort);
      m_socket->Bind (local);
      m_socket->SetRecvCallback (MakeCallback (&PennSearch::RecvMessage, this));
  }
  m_auditPingsTimer.SetFunction (&PennSearch::AuditPings, this);
  m_auditPingsTimer.Schedule (m_pingTimeout);
}

void PennSearch::StopApplication (void) {
  m_chord->StopChord ();
  if (m_socket) { m_socket->Close (); m_socket = 0; }
  m_auditPingsTimer.Cancel ();
  m_pingTracker.clear ();
}

void PennSearch::ProcessCommand (std::vector<std::string> tokens) {
  std::string cmd = tokens[0];
  if (cmd == "CHORD") {
      tokens.erase (tokens.begin ());
      m_chord->ProcessCommand (tokens);
  }
  if (cmd == "PING") {
      if (tokens.size() >= 3) SendPing(tokens[1], tokens[2]); 
  }
  if (cmd == "SEARCH") {
      tokens.erase(tokens.begin());
      if (tokens.size() < 1) return;
      
      // Handle "Via Node" logic
      bool viaNode = false;
      std::string viaNodeId;
      if (isdigit(tokens[0][0])) {
          viaNode = true;
          viaNodeId = tokens[0];
          tokens.erase(tokens.begin());
      }

      if (tokens.empty()) return;
      
      // Check if Via Node is ME or if I need to forward
      if (viaNode && viaNodeId != ReverseLookup(GetLocalAddress())) {
          // I am a client, I must ask ViaNode to start search
          Ipv4Address target = ResolveNodeIpAddress(viaNodeId);
          PennSearchMessage msg(PennSearchMessage::SEARCH_REQ, GetNextTransactionId());
          // Using SEARCH_REQ with a special "Entry" signature, or abuse SearchReq
          // To be clean, let's assume StartSearch handles the message construction
          // We send a message to Target saying "Start this search"
          // Pack terms into remainingTerms space separated
          std::string allTerms;
          for(const auto& t : tokens) allTerms += (allTerms.empty() ? "" : " ") + t;
          
          // Using a mock transaction to send "terms" to the entry node
          // We reuse SEARCH_REQ logic but with empty docs/keyword to signal start?
          // Simplest: Send SEARCH_REQ where currentKeyword is "ENTRY" (hack) 
          // Better: Use tokens[0] as keyword, rest as remaining, empty docs, origin = ME
          std::string first = tokens[0];
          std::string rest;
          for(size_t i=1; i<tokens.size(); ++i) rest += (rest.empty() ? "" : " ") + tokens[i];
          
          std::stringstream ss; ss << GetLocalAddress();
          std::string myIp = ss.str();
          
          msg.SetSearchReq(myIp, rest, "", first);
          Ptr<Packet> p = Create<Packet>();
          p->AddHeader(msg);
          m_socket->SendTo(p, 0, InetSocketAddress(target, m_appPort));
          return;
      }

      // If via node is me, or no via node specified
      SEARCH_LOG (GraderLogs::GetSearchLogStr (tokens));
      StartSearch (tokens);
  }
  if (cmd == "PUBLISH") {
      tokens.erase (tokens.begin());
      if (tokens.size() == 2) {
          std::string k = tokens[0], d = tokens[1];
          uint32_t h = PennKeyHelper::CreateShaKey(k);
          SEARCH_LOG(GraderLogs::GetPublishLogStr(k, d));
          m_chord->StartPublishLookup(k, d, h);
      } else if (tokens.size() == 1) {
          std::ifstream file(tokens[0].c_str());
          std::string line;
          while(std::getline(file, line)) {
              std::istringstream iss(line);
              std::string docId, kw;
              iss >> docId;
              while(iss >> kw) {
                  uint32_t h = PennKeyHelper::CreateShaKey(kw);
                  SEARCH_LOG(GraderLogs::GetPublishLogStr(kw, docId));
                  m_chord->StartPublishLookup(kw, docId, h);
              }
          }
      }
  }
}

void PennSearch::StartSearch(const std::vector<std::string> &terms) {
  std::string first = terms[0];
  std::string rest;
  for (size_t i = 1; i < terms.size (); ++i) rest += (rest.empty() ? "" : " ") + terms[i];
  
  std::stringstream ss; ss << GetLocalAddress();
  std::string origin = ss.str();
  
  std::string ctx = first + "|" + "" + "|" + rest + "|" + origin;
  m_chord->StartSearchLookup(ctx, PennKeyHelper::CreateShaKey(first));
}

void PennSearch::ProcessSearchReq(PennSearchMessage msg, Ipv4Address src, uint16_t port) {
    auto req = msg.GetSearchReq();
    
    // Check if this is a "Client Entry" request (via node forwarding)
    // If "origin" is the source and "currentDocs" is empty and "currentKeyword" is a term...
    // Actually, the logic below works fine for entry too.
    
    SEARCH_LOG ("SEARCH_REQ kw=" << req.currentKeyword << " rem=" << req.remainingTerms);

    std::string localDocs;
    if (m_invertedList.count(req.currentKeyword)) localDocs = SetToString(m_invertedList[req.currentKeyword]);
    
    std::string merged = CombineSearchResults(req.currentDocs, localDocs);
    
    std::vector<std::string> logDocs;
    std::stringstream ss(merged); std::string t; while(ss>>t) logDocs.push_back(t);
    SEARCH_LOG(GraderLogs::GetInvertedListShipLogStr(req.currentKeyword, logDocs));
    
    ContinueSearch(req.currentKeyword, merged, req.remainingTerms, req.originIp);
}

void PennSearch::ContinueSearch(const std::string &kw, const std::string &docs, const std::string &rem, const std::string &origin) {
    if (rem == "") {
        PennSearchMessage rsp(PennSearchMessage::SEARCH_RSP, GetNextTransactionId());
        rsp.SetSearchRsp(origin, docs);
        Ptr<Packet> p = Create<Packet>();
        p->AddHeader(rsp);
        m_socket->SendTo(p, 0, InetSocketAddress(Ipv4Address(origin.c_str()), m_appPort));
        return;
    }
    std::stringstream ss(rem);
    std::string nextKw, nextRem;
    ss >> nextKw;
    std::getline(ss, nextRem);
    if (!nextRem.empty() && nextRem[0] == ' ') nextRem.erase(0,1);
    
    std::string ctx = nextKw + "|" + docs + "|" + nextRem + "|" + origin;
    m_chord->StartSearchLookup(ctx, PennKeyHelper::CreateShaKey(nextKw));
}

void PennSearch::HandleSearchChordLookup(std::string ctx, Ipv4Address owner) {
    std::vector<std::string> p;
    std::stringstream ss(ctx); std::string s;
    while(std::getline(ss, s, '|')) p.push_back(s);
    // context: nextKeyword | currentDocs | remainingTerms | originIp
    if (p.size() < 4) return; 

    PennSearchMessage req(PennSearchMessage::SEARCH_REQ, GetNextTransactionId());
    req.SetSearchReq(p[3], p[2], p[1], p[0]);
    Ptr<Packet> pkt = Create<Packet>();
    pkt->AddHeader(req);
    m_socket->SendTo(pkt, 0, InetSocketAddress(owner, m_appPort));
}

void PennSearch::ProcessPublishReq(PennSearchMessage m, Ipv4Address s, uint16_t p) {}
void PennSearch::HandlePublishChordLookup(std::string k, std::string d, Ipv4Address owner) {
    if (owner == m_local) {
        m_invertedList[k].insert(d);
        SEARCH_LOG(GraderLogs::GetStoreLogStr(k, d));
    } else {
        PennSearchMessage msg(PennSearchMessage::STORE_REQ, GetNextTransactionId());
        msg.SetStoreReq(k, d);
        Ptr<Packet> p = Create<Packet>();
        p->AddHeader(msg);
        m_socket->SendTo(p, 0, InetSocketAddress(owner, m_appPort));
    }
}
void PennSearch::ProcessStoreReq(PennSearchMessage m, Ipv4Address s, uint16_t p) {
    auto req = m.GetStoreReq();
    m_invertedList[req.keyword].insert(req.docId);
    SEARCH_LOG(GraderLogs::GetStoreLogStr(req.keyword, req.docId));
}

void PennSearch::HandleKeyTransfer(Ipv4Address newOwner) {
    // When my predecessor changes, or I leave, I might need to move keys.
    // For this project, usually "Dump all my keys to successor on leave" is enough for the leave case.
    
    for (auto it = m_invertedList.begin(); it != m_invertedList.end(); ) {
        // iterate all docs
        for (const auto& doc : it->second) {
             PennSearchMessage msg(PennSearchMessage::STORE_REQ, GetNextTransactionId());
             msg.SetStoreReq(it->first, doc);
             Ptr<Packet> p = Create<Packet>();
             p->AddHeader(msg);
             m_socket->SendTo(p, 0, InetSocketAddress(newOwner, m_appPort));
        }
        ++it;
    }
}

// Boilerplate
void PennSearch::RecvMessage(Ptr<Socket> s) {
    Address f; Ptr<Packet> p = s->RecvFrom(f);
    PennSearchMessage m; p->RemoveHeader(m);
    Ipv4Address src = InetSocketAddress::ConvertFrom(f).GetIpv4();
    uint16_t port = InetSocketAddress::ConvertFrom(f).GetPort();
    switch(m.GetMessageType()){
        case PennSearchMessage::PING_REQ: ProcessPingReq(m, src, port); break;
        case PennSearchMessage::PING_RSP: ProcessPingRsp(m, src, port); break;
        case PennSearchMessage::SEARCH_REQ: ProcessSearchReq(m, src, port); break;
        case PennSearchMessage::SEARCH_RSP: ProcessSearchRsp(m, src, port); break;
        case PennSearchMessage::STORE_REQ: ProcessStoreReq(m, src, port); break;
        default: break;
    }
}
void PennSearch::ProcessSearchRsp(PennSearchMessage m, Ipv4Address s, uint16_t p) {
    auto rsp = m.GetSearchRsp();
    std::vector<std::string> d; std::stringstream ss(rsp.finalDocs); std::string t;
    while(ss>>t) d.push_back(t);
    SEARCH_LOG(GraderLogs::GetSearchResultsLogStr(Ipv4Address(rsp.originIp.c_str()), d));
}

// Helpers
std::string PennSearch::SetToString(const std::set<std::string> &s) {
    std::string o; for(auto &x:s) o+=(o.empty()?"":" ")+x; return o;
}
std::string PennSearch::IntersectDocLists(const std::string &a, const std::string &b) {
    std::set<std::string> A, B, R;
    std::stringstream sa(a), sb(b); std::string t;
    while(sa>>t) { A.insert(t); }
    while(sb>>t) { B.insert(t); }
    for(auto &x:A) if(B.count(x)) R.insert(x);
    return SetToString(R);
}
std::string PennSearch::CombineSearchResults(const std::string &e, const std::string &n) {
    return (e == "") ? n : IntersectDocLists(e, n);
}
void PennSearch::SendPing(std::string id, std::string msg) { m_chord->SendPing(ResolveNodeIpAddress(id), msg); }
void PennSearch::SendPennSearchPing(Ipv4Address d, std::string m) { /*...*/ } // (Implement if needed, mostly redundant)
void PennSearch::ProcessPingReq(PennSearchMessage m, Ipv4Address s, uint16_t p) { /*...*/ }
void PennSearch::ProcessPingRsp(PennSearchMessage m, Ipv4Address s, uint16_t p) { /*...*/ }
void PennSearch::AuditPings() { /*...*/ m_auditPingsTimer.Schedule(m_pingTimeout); }
uint32_t PennSearch::GetNextTransactionId() { return m_currentTransactionId++; }
void PennSearch::HandleChordPingSuccess(Ipv4Address d, std::string m) { SEARCH_LOG("Ping Success " << m); }
void PennSearch::HandleChordPingFailure(Ipv4Address d, std::string m) { SEARCH_LOG("Ping Fail " << m); }
void PennSearch::HandleChordPingRecv(Ipv4Address d, std::string m) { SEARCH_LOG("Ping Recv " << m); }
void PennSearch::SetTrafficVerbose(bool on) { m_chord->SetTrafficVerbose(on); g_trafficVerbose=on; }
void PennSearch::SetErrorVerbose(bool on) { m_chord->SetErrorVerbose(on); g_errorVerbose=on; }
void PennSearch::SetDebugVerbose(bool on) { m_chord->SetDebugVerbose(on); g_debugVerbose=on; }
void PennSearch::SetStatusVerbose(bool on) { m_chord->SetStatusVerbose(on); g_statusVerbose=on; }
void PennSearch::SetChordVerbose(bool on) { m_chord->SetChordVerbose(on); g_chordVerbose=on; }
void PennSearch::SetSearchVerbose(bool on) { m_chord->SetSearchVerbose(on); g_searchVerbose=on; }