/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "penn-search.h"
#include "ns3/grader-logs.h"
#include "ns3/random-variable-stream.h"
#include "ns3/inet-socket-address.h"
#include "ns3/penn-key-helper.h" // Needed for key hashing
#include <fstream>

using namespace ns3;

TypeId
PennSearch::GetTypeId ()
{
  static TypeId tid = TypeId ("PennSearch")
    .SetParent<PennApplication> ()
    .AddConstructor<PennSearch> ()
    .AddAttribute ("AppPort", "Listening port for Application", UintegerValue (10000),
                   MakeUintegerAccessor (&PennSearch::m_appPort), MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("ChordPort", "Listening port for Application", UintegerValue (10001),
                   MakeUintegerAccessor (&PennSearch::m_chordPort), MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("PingTimeout", "Timeout value for PING_REQ in milliseconds", TimeValue (MilliSeconds (2000)),
                   MakeTimeAccessor (&PennSearch::m_pingTimeout), MakeTimeChecker ())
    ;
  return tid;
}

PennSearch::PennSearch () : m_auditPingsTimer (Timer::CANCEL_ON_DESTROY)
{
  m_chord = NULL;
  Ptr<UniformRandomVariable> m_uniformRandomVariable = CreateObject<UniformRandomVariable> ();
  m_currentTransactionId = m_uniformRandomVariable->GetValue (0x00000000, 0xFFFFFFFF);
}

PennSearch::~PennSearch () {}

void
PennSearch::DoDispose ()
{
  StopApplication ();
  PennApplication::DoDispose ();
  GraderLogs::HelloGrader (ReverseLookup (GetLocalAddress ()), GetLocalAddress ());
}

void
PennSearch::StartApplication (void)
{
  ObjectFactory factory;
  factory.SetTypeId (PennChord::GetTypeId ());
  factory.Set ("AppPort", UintegerValue (m_chordPort));
  m_chord = factory.Create<PennChord> ();
  m_chord->SetNode (GetNode ());
  m_chord->SetNodeAddressMap (m_nodeAddressMap);
  m_chord->SetAddressNodeMap (m_addressNodeMap);
  m_chord->SetModuleName ("CHORD");
  std::string nodeId = GetNodeId ();
  m_chord->SetNodeId (nodeId);
  m_chord->SetLocalAddress (m_local);

  m_chord->SetPingSuccessCallback (MakeCallback (&PennSearch::HandleChordPingSuccess, this));
  m_chord->SetPingFailureCallback (MakeCallback (&PennSearch::HandleChordPingFailure, this));
  m_chord->SetPingRecvCallback    (MakeCallback (&PennSearch::HandleChordPingRecv, this));
  m_chord->SetSearchLookupCallback (MakeCallback (&PennSearch::HandleSearchChordLookup, this));
  m_chord->SetPublishLookupCallback (MakeCallback (&PennSearch::HandlePublishChordLookup, this));
  
  m_chord->SetTransferKeysCallback (MakeCallback (&PennSearch::HandleTransferKeys, this));

  m_chord->SetStartTime (Simulator::Now ());
  m_chord->Initialize ();

  if (m_socket == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), m_appPort);
      m_socket->Bind (local);
      m_socket->SetRecvCallback (MakeCallback (&PennSearch::RecvMessage, this));
    }

  m_auditPingsTimer.SetFunction (&PennSearch::AuditPings, this);
  m_auditPingsTimer.Schedule (m_pingTimeout);
}

void
PennSearch::StopApplication (void)
{
  m_chord->StopChord ();
  if (m_socket)
    {
      m_socket->Close ();
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
      m_socket = 0;
    }
  m_auditPingsTimer.Cancel ();
  m_pingTracker.clear ();
}

void
PennSearch::ProcessCommand (std::vector<std::string> tokens)
{
  std::vector<std::string>::iterator iterator = tokens.begin ();
  std::string command = *iterator;

  if (command == "CHORD")
    {
      tokens.erase (iterator);
      m_chord->ProcessCommand (tokens);
    }
  else if (command == "PING")
    {
       if (tokens.size() >= 3) {
           std::string nodeId = tokens[1];
           std::string msg = tokens[2];
           if (nodeId == "*") {
               for(auto const& item : m_nodeAddressMap) {
                   std::stringstream ss; ss << item.first;
                   SendPing(ss.str(), msg);
               }
           } else {
               SendPing(nodeId, msg);
           }
       }
    }
  else if (command == "SEARCH")
    {
      tokens.erase (tokens.begin ());
      if (tokens.size () < 2) return;

      std::string viaNodeId = tokens[0];
      tokens.erase (tokens.begin ());

      std::vector<std::string> terms = tokens;
      SEARCH_LOG (GraderLogs::GetSearchLogStr (terms));

      Ipv4Address viaNodeAddr = ResolveNodeIpAddress (viaNodeId);
      Ipv4Address myAddr      = GetLocalAddress ();

      if (viaNodeAddr == myAddr)
        {
          StartSearch (terms);
          return;
        }

      std::ostringstream originStream; originStream << myAddr;
      std::string originIp = originStream.str ();

      std::ostringstream termsStream;
      for (size_t i = 0; i < terms.size (); ++i) {
          if (i > 0) termsStream << " ";
          termsStream << terms[i];
      }
      std::string allTerms = termsStream.str ();
      std::string dummyKeyword = (terms.empty () ? "" : terms[0]);

      uint32_t transactionId = GetNextTransactionId ();
      PennSearchMessage initReq (PennSearchMessage::SEARCH_REQ, transactionId);
      initReq.SetSearchReq (originIp, allTerms, std::string ("__INIT__"), dummyKeyword);

      Ptr<Packet> p = Create<Packet> ();
      p->AddHeader (initReq);
      m_socket->SendTo (p, 0, InetSocketAddress (viaNodeAddr, m_appPort));
    }
  else if (command == "PUBLISH")
    {
      tokens.erase (tokens.begin ());
      if (tokens.size () == 1)
        {
          std::string filepath = tokens[0];
          std::ifstream file (filepath.c_str ());
          if (!file.is_open ()) return;

          std::string line;
          while (std::getline (file, line))
            {
              std::istringstream iss (line);
              std::string docId, keyword;
              iss >> docId;
              while (iss >> keyword)
                {
                  uint32_t hash = PennKeyHelper::CreateShaKey (keyword);
                  SEARCH_LOG (GraderLogs::GetPublishLogStr (keyword, docId));
                  m_chord->StartPublishLookup (keyword, docId, hash);
                }
            }
        }
      else if (tokens.size () == 2)
        {
          std::string keyword = tokens[0];
          std::string docId   = tokens[1];
          uint32_t hash = PennKeyHelper::CreateShaKey (keyword);
          SEARCH_LOG (GraderLogs::GetPublishLogStr (keyword, docId));
          m_chord->StartPublishLookup (keyword, docId, hash);
        }
    }
}

void
PennSearch::StartSearch (const std::vector<std::string> &terms)
{
  std::ostringstream ss; ss << GetLocalAddress ();
  StartSearchFromOrigin (terms, ss.str ());
}

void
PennSearch::StartSearchFromOrigin (const std::vector<std::string> &terms, const std::string &originIp)
{
  if (terms.empty ()) return;

  std::string firstKeyword = terms[0];
  std::string remainingTerms;
  for (size_t i = 1; i < terms.size (); ++i)
    {
      if (!remainingTerms.empty ()) remainingTerms += " ";
      remainingTerms += terms[i];
    }

  std::string ctx = firstKeyword + "|" + "" + "|" + remainingTerms + "|" + originIp;
  uint32_t hash = PennKeyHelper::CreateShaKey (firstKeyword);
  m_chord->StartSearchLookup (ctx, hash);
}

void
PennSearch::SendPing (std::string nodeId, std::string pingMessage)
{
  SEARCH_LOG ("Sending Ping via Chord Layer to node: " << nodeId << " Message: " << pingMessage);
  Ipv4Address destAddress = ResolveNodeIpAddress (nodeId);
  m_chord->SendPing (destAddress, pingMessage);
}

void
PennSearch::SendPennSearchPing (Ipv4Address destAddress, std::string pingMessage)
{
  if (destAddress != Ipv4Address::GetAny ())
    {
      uint32_t transactionId = GetNextTransactionId ();
      SEARCH_LOG ("Sending PING_REQ to Node: " << ReverseLookup (destAddress)
                 << " IP: " << destAddress << " Message: " << pingMessage);
      Ptr<PingRequest> pingRequest = Create<PingRequest> (transactionId, Simulator::Now (), destAddress, pingMessage);
      m_pingTracker.insert (std::make_pair (transactionId, pingRequest));
      Ptr<Packet> packet = Create<Packet> ();
      PennSearchMessage message = PennSearchMessage (PennSearchMessage::PING_REQ, transactionId);
      message.SetPingReq (pingMessage);
      packet->AddHeader (message);
      m_socket->SendTo (packet, 0, InetSocketAddress (destAddress, m_appPort));
    }
}

void
PennSearch::RecvMessage (Ptr<Socket> socket)
{
  Address sourceAddr;
  Ptr<Packet> packet = socket->RecvFrom (sourceAddr);
  InetSocketAddress inetSocketAddr = InetSocketAddress::ConvertFrom (sourceAddr);
  Ipv4Address sourceAddress = inetSocketAddr.GetIpv4 ();
  uint16_t sourcePort = inetSocketAddr.GetPort ();
  PennSearchMessage message;
  packet->RemoveHeader (message);

  switch (message.GetMessageType ())
    {
    case PennSearchMessage::PING_REQ: ProcessPingReq (message, sourceAddress, sourcePort); break;
    case PennSearchMessage::PING_RSP: ProcessPingRsp (message, sourceAddress, sourcePort); break;
    case PennSearchMessage::SEARCH_REQ: ProcessSearchReq (message, sourceAddress, sourcePort); break;
    case PennSearchMessage::SEARCH_RSP: ProcessSearchRsp (message, sourceAddress, sourcePort); break;
    case PennSearchMessage::PUBLISH_REQ: ProcessPublishReq (message, sourceAddress, sourcePort); break;
    case PennSearchMessage::STORE_REQ: ProcessStoreReq (message, sourceAddress, sourcePort); break;
    default: break;
    }
}

void
PennSearch::ProcessSearchReq (PennSearchMessage message, Ipv4Address source, uint16_t port)
{
  auto req = message.GetSearchReq ();
  SEARCH_LOG ("SEARCH_REQ keyword=" << req.currentKeyword << " remaining=" << req.remainingTerms);

  if (req.currentDocs == "__INIT__")
    {
      std::vector<std::string> terms;
      std::stringstream ss (req.remainingTerms);
      std::string t;
      while (ss >> t) terms.push_back (t);

      if (terms.empty ()) return;
      StartSearchFromOrigin(terms, req.originIp);
      return;
    }

  std::string localDocs;
  auto it = m_invertedList.find (req.currentKeyword);
  if (it != m_invertedList.end ()) localDocs = SetToString (it->second);

  std::string merged = CombineSearchResults (req.currentDocs, localDocs);

  std::vector<std::string> mergedDocs;
  std::stringstream ss (merged);
  std::string tok;
  while (ss >> tok) mergedDocs.push_back (tok);

  SEARCH_LOG (GraderLogs::GetInvertedListShipLogStr (req.currentKeyword, mergedDocs));

  ContinueSearch (req.currentKeyword, merged, req.remainingTerms, req.originIp);
}

void
PennSearch::ProcessSearchRsp (PennSearchMessage message, Ipv4Address source, uint16_t port)
{
  auto rsp = message.GetSearchRsp ();
  std::vector<std::string> docs;
  std::stringstream ss (rsp.finalDocs);
  std::string tok;
  while (ss >> tok) docs.push_back (tok);

  Ipv4Address originAddr (rsp.originIp.c_str ());
  SEARCH_LOG (GraderLogs::GetSearchResultsLogStr (originAddr, docs));
}

void
PennSearch::ContinueSearch (const std::string &keyword, const std::string &currentDocs,
                            const std::string &remainingTerms, const std::string &originIp)
{
  if (currentDocs == "" && remainingTerms != "")
  {
      PennSearchMessage rsp (PennSearchMessage::SEARCH_RSP, GetNextTransactionId ());
      rsp.SetSearchRsp (originIp, ""); 
      Ptr<Packet> p = Create<Packet> ();
      p->AddHeader (rsp);
      Ipv4Address dest (originIp.c_str ());
      m_socket->SendTo (p, 0, InetSocketAddress (dest, m_appPort));
      return;
  }

  if (remainingTerms == "")
    {
      PennSearchMessage rsp (PennSearchMessage::SEARCH_RSP, GetNextTransactionId ());
      rsp.SetSearchRsp (originIp, currentDocs);
      Ptr<Packet> p = Create<Packet> ();
      p->AddHeader (rsp);
      Ipv4Address dest (originIp.c_str ());
      m_socket->SendTo (p, 0, InetSocketAddress (dest, m_appPort));
      return;
    }

  std::string nextKeyword;
  std::string nextRemaining;
  std::stringstream ss (remainingTerms);
  ss >> nextKeyword;
  std::getline (ss, nextRemaining);
  if (!nextRemaining.empty () && nextRemaining[0] == ' ') nextRemaining.erase (0, 1);

  std::string ctx = nextKeyword + "|" + currentDocs + "|" + nextRemaining + "|" + originIp;
  uint32_t hash = PennKeyHelper::CreateShaKey (nextKeyword);
  m_chord->StartSearchLookup (ctx, hash);
}

void
PennSearch::HandleSearchChordLookup (std::string ctx, Ipv4Address owner)
{
  std::vector<std::string> parts;
  std::stringstream ss (ctx);
  std::string x;
  while (std::getline (ss, x, '|')) parts.push_back (x);

  if (parts.size () != 4) return;

  std::string nextKeyword    = parts[0];
  std::string currentDocs    = parts[1];
  std::string remainingTerms = parts[2];
  std::string originIp       = parts[3];

  PennSearchMessage req (PennSearchMessage::SEARCH_REQ, GetNextTransactionId ());
  req.SetSearchReq (originIp, remainingTerms, currentDocs, nextKeyword);

  Ptr<Packet> p = Create<Packet> ();
  p->AddHeader (req);
  m_socket->SendTo (p, 0, InetSocketAddress (owner, m_appPort));
}

void
PennSearch::ProcessPublishReq (PennSearchMessage message, Ipv4Address source, uint16_t port)
{
  auto pr = message.GetPublishReq ();
  uint32_t hash = PennKeyHelper::CreateShaKey (pr.keyword);
  m_chord->StartPublishLookup (pr.keyword, pr.docId, hash);
  SEARCH_LOG (GraderLogs::GetPublishLogStr (pr.keyword, pr.docId));
}

void
PennSearch::HandlePublishChordLookup (std::string keyword, std::string docId, Ipv4Address owner)
{
  if (owner == m_local)
    {
      m_invertedList[keyword].insert (docId);
      SEARCH_LOG (GraderLogs::GetStoreLogStr (keyword, docId));
      return;
    }

  PennSearchMessage m (PennSearchMessage::STORE_REQ, GetNextTransactionId ());
  m.SetStoreReq (keyword, docId);
  Ptr<Packet> p = Create<Packet> ();
  p->AddHeader (m);
  m_socket->SendTo (p, 0, InetSocketAddress (owner, m_appPort));
}

void
PennSearch::ProcessStoreReq (PennSearchMessage message, Ipv4Address source, uint16_t port)
{
  auto s = message.GetStoreReq ();
  m_invertedList[s.keyword].insert (s.docId);
  SEARCH_LOG (GraderLogs::GetStoreLogStr (s.keyword, s.docId));
}

static bool IsBetweenSemiOpen(uint32_t target, uint32_t start, uint32_t end)
{
    if (start < end) return (target > start && target <= end);
    else if (start > end) return (target > start || target <= end);
    else return (target == start);
}

void
PennSearch::HandleTransferKeys (Ipv4Address newOwner, uint32_t rangeStart, uint32_t rangeEnd)
{
    for (auto it = m_invertedList.begin(); it != m_invertedList.end(); /* no increment */)
    {
        std::string keyword = it->first;
        uint32_t keyHash = PennKeyHelper::CreateShaKey(keyword);

        if (IsBetweenSemiOpen(keyHash, rangeStart, rangeEnd))
        {
            for (const auto& docId : it->second)
            {
                PennSearchMessage m (PennSearchMessage::STORE_REQ, GetNextTransactionId ());
                m.SetStoreReq (keyword, docId);
                Ptr<Packet> p = Create<Packet> ();
                p->AddHeader (m);
                m_socket->SendTo (p, 0, InetSocketAddress (newOwner, m_appPort));
            }
            m_invertedList.erase(it++);
        }
        else
        {
            ++it;
        }
    }
}

std::string PennSearch::SetToString (const std::set<std::string> &s)
{
  std::string out;
  for (auto &x : s) { if (!out.empty ()) out += " "; out += x; }
  return out;
}

std::string PennSearch::IntersectDocLists (const std::string &a, const std::string &b)
{
  std::set<std::string> A, B, R;
  if (!a.empty ()) { std::stringstream ss (a); std::string tok; while (ss >> tok) A.insert (tok); }
  if (!b.empty ()) { std::stringstream ss (b); std::string tok; while (ss >> tok) B.insert (tok); }
  for (auto &x : A) { if (B.count (x)) R.insert (x); }
  return SetToString (R);
}

std::string PennSearch::CombineSearchResults (const std::string &existing, const std::string &next)
{
  if (existing == "") return next;
  return IntersectDocLists (existing, next);
}

void PennSearch::InitializeSearchLayer () { m_invertedList.clear (); }
void PennSearch::ChordLookupForwardingStub (const std::string &k, const std::string &d, const std::string &r, const std::string &o, Ipv4Address n) {}
void PennSearch::DistributedInvertedListMaintenanceStub () {}

void PennSearch::ProcessPingReq (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort)
{
  std::string fromNode = ReverseLookup (sourceAddress);
  SEARCH_LOG ("Received PING_REQ, From Node: " << fromNode << ", Message: " << message.GetPingReq ().pingMessage);
  PennSearchMessage resp = PennSearchMessage (PennSearchMessage::PING_RSP, message.GetTransactionId ());
  resp.SetPingRsp (message.GetPingReq ().pingMessage);
  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (resp);
  m_socket->SendTo (packet, 0, InetSocketAddress (sourceAddress, sourcePort));
}

void PennSearch::ProcessPingRsp (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort)
{
  auto iter = m_pingTracker.find (message.GetTransactionId ());
  if (iter != m_pingTracker.end ()) m_pingTracker.erase (iter);
}

void PennSearch::AuditPings ()
{
  for (auto iter = m_pingTracker.begin (); iter != m_pingTracker.end ();)
    {
      if (iter->second->GetTimestamp ().GetMilliSeconds () + m_pingTimeout.GetMilliSeconds () <= Simulator::Now ().GetMilliSeconds ())
        m_pingTracker.erase (iter++);
      else ++iter;
    }
  m_auditPingsTimer.Schedule (m_pingTimeout);
}

uint32_t PennSearch::GetNextTransactionId () { return m_currentTransactionId++; }
void PennSearch::HandleChordPingFailure (Ipv4Address destAddress, std::string message) {}
void PennSearch::HandleChordPingSuccess (Ipv4Address destAddress, std::string message) { SendPennSearchPing (destAddress, message); }
void PennSearch::HandleChordPingRecv (Ipv4Address destAddress, std::string message) {}
void PennSearch::SetTrafficVerbose (bool on) { m_chord->SetTrafficVerbose (on); g_trafficVerbose = on; }
void PennSearch::SetErrorVerbose (bool on) { m_chord->SetErrorVerbose (on); g_errorVerbose = on; }
void PennSearch::SetDebugVerbose (bool on) { m_chord->SetDebugVerbose (on); g_debugVerbose = on; }
void PennSearch::SetStatusVerbose (bool on) { m_chord->SetStatusVerbose (on); g_statusVerbose = on; }
void PennSearch::SetChordVerbose (bool on) { m_chord->SetChordVerbose (on); g_chordVerbose = on; }
void PennSearch::SetSearchVerbose (bool on) { m_chord->SetSearchVerbose (on); g_searchVerbose = on; }