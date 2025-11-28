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

#include "penn-search.h"
#include "ns3/grader-logs.h"

#include "ns3/random-variable-stream.h"
#include "ns3/inet-socket-address.h"

using namespace ns3;

TypeId
PennSearch::GetTypeId ()
{
  static TypeId tid = TypeId ("PennSearch")
    .SetParent<PennApplication> ()
    .AddConstructor<PennSearch> ()
    .AddAttribute ("AppPort",
                   "Listening port for Application",
                   UintegerValue (10000),
                   MakeUintegerAccessor (&PennSearch::m_appPort),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("ChordPort",
                   "Listening port for Application",
                   UintegerValue (10001),
                   MakeUintegerAccessor (&PennSearch::m_chordPort),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("PingTimeout",
                   "Timeout value for PING_REQ in milliseconds",
                   TimeValue (MilliSeconds (2000)),
                   MakeTimeAccessor (&PennSearch::m_pingTimeout),
                   MakeTimeChecker ())
    ;
  return tid;
}

PennSearch::PennSearch ()
  : m_auditPingsTimer (Timer::CANCEL_ON_DESTROY)
{
  m_chord = NULL;

  Ptr<UniformRandomVariable> m_uniformRandomVariable = CreateObject<UniformRandomVariable> ();
  m_currentTransactionId = m_uniformRandomVariable->GetValue (0x00000000, 0xFFFFFFFF);
}

PennSearch::~PennSearch ()
{
}

void
PennSearch::DoDispose ()
{
  StopApplication ();
  PennApplication::DoDispose ();

  // FOR TESTING
  GraderLogs::HelloGrader(ReverseLookup(GetLocalAddress()), GetLocalAddress());
}

void
PennSearch::StartApplication (void)
{
  // Create and Configure PennChord
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

  // Configure Callbacks with Chord
  m_chord->SetPingSuccessCallback (MakeCallback (&PennSearch::HandleChordPingSuccess, this));
  m_chord->SetPingFailureCallback (MakeCallback (&PennSearch::HandleChordPingFailure, this));
  m_chord->SetPingRecvCallback (MakeCallback (&PennSearch::HandleChordPingRecv, this));

  // For search chord lookups
  m_chord->SetSearchLookupCallback (MakeCallback(&PennSearch::HandleSearchChordLookup, this));
  // For publish chord lookups
  m_chord->SetPublishLookupCallback (MakeCallback(&PennSearch::HandlePublishChordLookup, this));

  // Start Chord
  m_chord->SetStartTime (Simulator::Now());
  m_chord->Initialize();

  if (m_socket == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny(), m_appPort);
      m_socket->Bind (local);
      m_socket->SetRecvCallback (MakeCallback (&PennSearch::RecvMessage, this));
    }

  // Configure timers
  m_auditPingsTimer.SetFunction (&PennSearch::AuditPings, this);
  // Start timers
  m_auditPingsTimer.Schedule (m_pingTimeout);
}

void
PennSearch::StopApplication (void)
{
  //Stop chord
  m_chord->StopChord ();
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
PennSearch::ProcessCommand (std::vector<std::string> tokens)
{
  std::vector<std::string>::iterator iterator = tokens.begin();
  std::string command = *iterator;

  if (command == "CHORD")
    {
      // Send to Chord Sub-Layer
      tokens.erase (iterator);
      m_chord->ProcessCommand (tokens);
    }

  if (command == "PING")
    {
      if (tokens.size() < 3)
        {
          ERROR_LOG ("Insufficient PING params...");
          return;
        }
      iterator++;
      if (*iterator != "*")
        {
          std::string nodeId = *iterator;
          iterator++;
          std::string pingMessage = *iterator;
          SendPing (nodeId, pingMessage);
        }
      else
        {
          iterator++;
          std::string pingMessage = *iterator;
          std::map<uint32_t, Ipv4Address>::iterator iter;
          for (iter = m_nodeAddressMap.begin () ; iter != m_nodeAddressMap.end (); iter++)
            {
              std::ostringstream sin;
              uint32_t nodeNumber = iter->first;
              sin << nodeNumber;
              std::string nodeId = sin.str();
              SendPing (nodeId, pingMessage);
            }
        }
    }

  // USER TRIGGERED SEARCH COMMAND
  if (command == "SEARCH")
    {
      // Remove the "SEARCH" token
      tokens.erase(tokens.begin());

      if (tokens.size() < 1)
        {
          ERROR_LOG("SEARCH requires at least one term");
          return;
        }

      // At this point tokens are either:
      //   [term1, term2, ...]
      // or
      //   [viaNodeId, term1, term2, ...]
      // where viaNodeId is a numeric node id (e.g., "4" in
      // "PENNSEARCH SEARCH 4 Johnny-Depp").
      if (tokens.size() > 1)
        {
          bool allDigits = true;
          for (size_t i = 0; i < tokens[0].size(); ++i)
            {
              char c = tokens[0][i];
              if (c < '0' || c > '9')
                {
                  allDigits = false;
                  break;
                }
            }
          // If the first remaining token is all digits and there are
          // more tokens after it, treat it as the via-node id and drop it.
          if (allDigits)
            {
              tokens.erase(tokens.begin());
            }
        }

      if (tokens.empty())
        {
        ERROR_LOG("SEARCH requires at least one term after via-node id");
        return;
        }

      std::vector<std::string> terms = tokens;

      SEARCH_LOG(
        GraderLogs::GetSearchLogStr (terms));

      StartSearch (terms);
      return;
    }

  // USER TRIGGERED PUBLISH COMMAND
  if (command == "PUBLISH")
    {
      tokens.erase(tokens.begin());

      // Case 1: metadata file
      if (tokens.size() == 1)
        {
          std::string filepath = tokens[0];
          std::ifstream file(filepath);

          if (!file.is_open())
            {
              ERROR_LOG("Could not open metadata file: " << filepath);
              return;
            }

          std::string line;
          while (std::getline(file, line))
            {
              std::istringstream iss(line);
              std::string docId;
              iss >> docId;

              std::string keyword;
              while (iss >> keyword)
                {
                  uint32_t hash = PennKeyHelper::CreateShaKey(keyword);

                  SEARCH_LOG(
                    GraderLogs::GetPublishLogStr(keyword, docId)
                  );

                  m_chord->StartPublishLookup(keyword, docId, hash);
                }
            }

          return;
        }

      // Case 2: PUBLISH <keyword> <docId>
      if (tokens.size() == 2)
        {
          std::string keyword = tokens[0];
          std::string docId = tokens[1];

          uint32_t hash = PennKeyHelper::CreateShaKey(keyword);

          SEARCH_LOG(
            GraderLogs::GetPublishLogStr(keyword, docId)
          );

          m_chord->StartPublishLookup(keyword, docId, hash);
          return;
        }

      // Invalid
      ERROR_LOG("PUBLISH <keyword> <docId> or PUBLISH <metadataFile>");
      return;
    }
}

// MS2A START SEARCH (USER INITIATED)
/* This function begins the multi keyword workflow.
   Extracts the first keyword
   Builds remainingTerms string
   Creates context string:
   nextKeyword | currentDocs | remainingTerms | originIp
   Calls Chord lookup to find owner of first keyword
*/
void
PennSearch::StartSearch(const std::vector<std::string> &terms)
{
  if (terms.empty())
    {
      ERROR_LOG("StartSearch requires at least 1 search term");
      return;
    }

  // First keyword in the multi keyword query
  std::string firstKeyword = terms[0];

  // Build remainingTerms string
  std::string remainingTerms;
  for (size_t i = 1; i < terms.size(); i++)
    {
      if (!remainingTerms.empty()) remainingTerms += " ";
      remainingTerms += terms[i];
    }

  // Origin of the full search is this node
  std::string originIp;
  {
    std::ostringstream ss;
    ss << GetLocalAddress();
    originIp = ss.str();
  }

  // Context format:
  // nextKeyword | currentDocs | remainingTerms | originIp
  // currentDocs initially empty because nothing intersected yet
  std::string ctx = firstKeyword + "|" + "" + "|" + remainingTerms + "|" + originIp;

  // Hash the first keyword
  uint32_t hash = PennKeyHelper::CreateShaKey(firstKeyword);

  // Ask Chord to resolve the owner of the first keyword
  m_chord->StartSearchLookup(ctx, hash);
}

void
PennSearch::SendPing (std::string nodeId, std::string pingMessage)
{
  // Send Ping Via-Chord layer
  SEARCH_LOG ("Sending Ping via Chord Layer to node: " << nodeId << " Message: " << pingMessage);
  Ipv4Address destAddress = ResolveNodeIpAddress(nodeId);
  m_chord->SendPing (destAddress, pingMessage);
}

void
PennSearch::SendPennSearchPing (Ipv4Address destAddress, std::string pingMessage)
{
  if (destAddress != Ipv4Address::GetAny ())
    {
      uint32_t transactionId = GetNextTransactionId ();
      SEARCH_LOG ("Sending PING_REQ to Node: " << ReverseLookup(destAddress) << " IP: " << destAddress << " Message: " << pingMessage << " transactionId: " << transactionId);
      Ptr<PingRequest> pingRequest = Create<PingRequest> (transactionId, Simulator::Now(), destAddress, pingMessage);
      // Add to ping-tracker
      m_pingTracker.insert (std::make_pair (transactionId, pingRequest));
      Ptr<Packet> packet = Create<Packet> ();
      PennSearchMessage message = PennSearchMessage (PennSearchMessage::PING_REQ, transactionId);
      message.SetPingReq (pingMessage);
      packet->AddHeader (message);
      m_socket->SendTo (packet, 0 , InetSocketAddress (destAddress, m_appPort));
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
    case PennSearchMessage::PING_REQ:
      ProcessPingReq (message, sourceAddress, sourcePort);
      break;
    case PennSearchMessage::PING_RSP:
      ProcessPingRsp (message, sourceAddress, sourcePort);
      break;
      // MS2 portion
    case PennSearchMessage::SEARCH_REQ:
      ProcessSearchReq (message, sourceAddress, sourcePort);
      break;
    case PennSearchMessage::SEARCH_RSP:
      ProcessSearchRsp (message, sourceAddress, sourcePort);
      break;
    case PennSearchMessage::PUBLISH_REQ:
      ProcessPublishReq (message, sourceAddress, sourcePort);
      break;
    case PennSearchMessage::STORE_REQ:
      ProcessStoreReq (message, sourceAddress, sourcePort);
      break;
    default:
      ERROR_LOG ("Unknown Message Type!");
      break;
    }
}

// MS2 SEARCH REQUEST AND RESPONSE HANDLING

void
PennSearch::ProcessSearchReq(PennSearchMessage message,
                             Ipv4Address source,
                             uint16_t port)
{
  auto req = message.GetSearchReq();

  SEARCH_LOG(
    "SEARCH_REQ keyword=" << req.currentKeyword
    << " remaining=" << req.remainingTerms
    << " currentDocs=" << req.currentDocs
    << " origin=" << req.originIp
  );

  // Lookup local docs for keyword
  std::string localDocs;
  auto it = m_invertedList.find(req.currentKeyword);
  if (it != m_invertedList.end())
    localDocs = SetToString(it->second);

  // Merge local docs with incoming doc set
  std::string merged = CombineSearchResults(req.currentDocs, localDocs);

  // Continue multi keyword workflow
  ContinueSearch(req.currentKeyword, merged, req.remainingTerms, req.originIp);
}

void
PennSearch::ProcessSearchRsp (PennSearchMessage message,
                              Ipv4Address source,
                              uint16_t port)
{
  auto rsp = message.GetSearchRsp ();

  // Convert finalDocs string into vector<string>
  std::vector<std::string> docs;
  {
    std::stringstream ss (rsp.finalDocs);
    std::string tok;
    while (ss >> tok)
      {
        docs.push_back (tok);
      }
  }

  Ipv4Address originAddr (rsp.originIp.c_str ());

  SEARCH_LOG (
    GraderLogs::GetSearchResultsLogStr (
      originAddr,
      docs));

  // This concludes the search. No more forwarding.
}

void
PennSearch::ContinueSearch(const std::string &keyword,
                           const std::string &currentDocs,
                           const std::string &remainingTerms,
                           const std::string &originIp)
{
  SEARCH_LOG("ContinueSearch docs=" << currentDocs
             << " remaining=" << remainingTerms);

  // Base case: no remaining keywords
  if (remainingTerms == "")
    {
      // Build and send SEARCH_RSP back to the origin
      PennSearchMessage rsp (PennSearchMessage::SEARCH_RSP, GetNextTransactionId ());
      rsp.SetSearchRsp (originIp, currentDocs);

      Ptr<Packet> p = Create<Packet> ();
      p->AddHeader (rsp);

      Ipv4Address dest (originIp.c_str ());
      m_socket->SendTo (p, 0, InetSocketAddress (dest, m_appPort));
      return;
    }

  // Extract next keyword
  std::string nextKeyword;
  std::string nextRemaining;

  {
    std::stringstream ss (remainingTerms);
    ss >> nextKeyword;
    std::getline (ss, nextRemaining);

    if (!nextRemaining.empty () && nextRemaining[0] == ' ')
      nextRemaining.erase (0, 1);
  }

  // Prepare Chord lookup context
  std::string ctx =
    nextKeyword + "|" +
    currentDocs + "|" +
    nextRemaining + "|" +
    originIp;

  uint32_t hash = PennKeyHelper::CreateShaKey(nextKeyword);
  m_chord->StartSearchLookup (ctx, hash);
}

// MS2 SEARCH CHORD LOOKUP CALLBACK
void
PennSearch::HandleSearchChordLookup(std::string ctx, Ipv4Address owner)
{
  std::vector<std::string> parts;

  {
    std::stringstream ss(ctx);
    std::string x;
    while (std::getline(ss, x, '|'))
      parts.push_back(x);
  }

  if (parts.size() != 4)
    {
      ERROR_LOG("Invalid search context");
      return;
    }

  std::string nextKeyword    = parts[0];
  std::string currentDocs    = parts[1];
  std::string remainingTerms = parts[2];
  std::string originIp       = parts[3];

  // Convert currentDocs to vector<string> for logging
  std::vector<std::string> docs;
  {
    std::stringstream ss (currentDocs);
    std::string tok;
    while (ss >> tok)
      {
        docs.push_back (tok);
      }
  }

  SEARCH_LOG (
    GraderLogs::GetInvertedListShipLogStr (
      nextKeyword,
      docs));

  PennSearchMessage req (PennSearchMessage::SEARCH_REQ, GetNextTransactionId ());
  req.SetSearchReq (originIp, remainingTerms, currentDocs, nextKeyword);

  Ptr<Packet> p = Create<Packet> ();
  p->AddHeader (req);

  m_socket->SendTo (p, 0, InetSocketAddress (owner, m_appPort));
}

// MS2 INVERTED LIST PUBLISH LOGIC

void
PennSearch::ProcessPublishReq (PennSearchMessage message,
                               Ipv4Address source,
                               uint16_t port)
{
  auto pr = message.GetPublishReq ();

  uint32_t hash = PennKeyHelper::CreateShaKey(pr.keyword);
  m_chord->StartPublishLookup (pr.keyword, pr.docId, hash);

  SEARCH_LOG (
    GraderLogs::GetPublishLogStr (
      pr.keyword,
      pr.docId));
}

// MS2 PUBLISH CHORD LOOKUP CALLBACK
void
PennSearch::HandlePublishChordLookup(std::string keyword,
                                     std::string docId,
                                     Ipv4Address owner)
{
  if (owner == m_local)
    {
      m_invertedList[keyword].insert (docId);

      SEARCH_LOG (
        GraderLogs::GetStoreLogStr (
          keyword,
          docId));

      return;
    }

  PennSearchMessage m (PennSearchMessage::STORE_REQ, GetNextTransactionId ());
  m.SetStoreReq (keyword, docId);

  Ptr<Packet> p = Create<Packet> ();
  p->AddHeader (m);

  m_socket->SendTo (p, 0, InetSocketAddress (owner, m_appPort));
}

// MS2 STORE_REQ HANDLER
void
PennSearch::ProcessStoreReq (PennSearchMessage message,
                             Ipv4Address source,
                             uint16_t port)
{
  auto s = message.GetStoreReq ();
  m_invertedList[s.keyword].insert (s.docId);

  SEARCH_LOG (
    GraderLogs::GetStoreLogStr (
      s.keyword,
      s.docId));
}

// UTILITY FOR LOGGING SETS (Used in publish logging above.)

std::string
PennSearch::SetToString (const std::set<std::string> &s)
{
  std::string out;
  for (auto &x : s)
    {
      if (!out.empty ()) out += " ";
      out += x;
    }
  return out;
}

// MULTI KEYWORD INTERSECTION + FINAL MS2 HELPERS/STUBS

/* --------------------------------------------------------------------------
   Helper: Intersect two space separated doc lists
   Example:
       docsA = "a b c"
       docsB = "b c d"
       => output "b c"
   -------------------------------------------------------------------------- */
std::string
PennSearch::IntersectDocLists (const std::string &a,
                               const std::string &b)
{
  std::set<std::string> A, B, R;

  if (!a.empty ())
    {
      std::stringstream ss (a);
      std::string tok;
      while (std::getline (ss, tok, ' '))
        if (!tok.empty ()) A.insert (tok);
    }

  if (!b.empty ())
    {
      std::stringstream ss (b);
      std::string tok;
      while (std::getline (ss, tok, ' '))
        if (!tok.empty ()) B.insert (tok);
    }

  for (auto &x : A)
    if (B.count (x)) R.insert (x);

  return SetToString (R);
}

// ContinueSearch() calls this helper when combining result sets
std::string
PennSearch::CombineSearchResults (const std::string &existing,
                                  const std::string &next)
{
  if (existing == "")
    return next;

  return IntersectDocLists (existing, next);
}

// STUBS
void
PennSearch::ChordLookupForwardingStub (const std::string &keyword,
                                       const std::string &docs,
                                       const std::string &remaining,
                                       const std::string &originIp,
                                       Ipv4Address nextHop)
{
  SEARCH_LOG ("[STUB] Chord forwarding stub invoked");
}

void
PennSearch::DistributedInvertedListMaintenanceStub ()
{
  SEARCH_LOG ("[STUB] Inverted list maintenance");
}

void
PennSearch::ProcessPingReq (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort)
{
  // Use reverse lookup for ease of debug
  std::string fromNode = ReverseLookup (sourceAddress);
  SEARCH_LOG ("Received PING_REQ, From Node: " << fromNode << ", Message: " << message.GetPingReq ().pingMessage);
  // Send Ping Response
  PennSearchMessage resp = PennSearchMessage (PennSearchMessage::PING_RSP, message.GetTransactionId ());
  resp.SetPingRsp (message.GetPingReq ().pingMessage);
  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (resp);
  m_socket->SendTo (packet, 0, InetSocketAddress (sourceAddress, sourcePort));
}

void
PennSearch::ProcessPingRsp (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort)
{
  // Remove from pingTracker
  std::map<uint32_t, Ptr<PingRequest> >::iterator iter;
  iter = m_pingTracker.find (message.GetTransactionId ());
  if (iter != m_pingTracker.end ())
    {
      std::string fromNode = ReverseLookup (sourceAddress);
      SEARCH_LOG ("Received PING_RSP, From Node: " << fromNode << ", Message: " << message.GetPingRsp ().pingMessage);
      m_pingTracker.erase (iter);
    }
  else
    {
      DEBUG_LOG ("Received invalid PING_RSP!");
    }
}

void
PennSearch::AuditPings ()
{
  std::map<uint32_t, Ptr<PingRequest> >::iterator iter;
  for (iter = m_pingTracker.begin (); iter != m_pingTracker.end ();)
    {
      Ptr<PingRequest> pingRequest = iter->second;
      if (pingRequest->GetTimestamp ().GetMilliSeconds () + m_pingTimeout.GetMilliSeconds () <= Simulator::Now ().GetMilliSeconds ())
        {
          DEBUG_LOG ("Ping expired. Message: " << pingRequest->GetPingMessage () << " Timestamp: " << pingRequest->GetTimestamp ().GetMilliSeconds () << " CurrentTime: " << Simulator::Now ().GetMilliSeconds ());
          // Remove stale entries
          m_pingTracker.erase (iter++);
        }
      else
        {
          ++iter;
        }
    }
  // Reschedule timer
  m_auditPingsTimer.Schedule (m_pingTimeout);
}

uint32_t
PennSearch::GetNextTransactionId ()
{
  return m_currentTransactionId++;
}

// Handle Chord Callbacks

void
PennSearch::HandleChordPingFailure (Ipv4Address destAddress, std::string message)
{
  SEARCH_LOG ("Chord Ping Expired! Destination nodeId: " << ReverseLookup (destAddress) << " IP: " << destAddress << " Message: " << message);
}

void
PennSearch::HandleChordPingSuccess (Ipv4Address destAddress, std::string message)
{
  SEARCH_LOG ("Chord Ping Success! Destination nodeId: " << ReverseLookup (destAddress) << " IP: " << destAddress << " Message: " << message);
  // Send ping via search layer
  SendPennSearchPing (destAddress, message);
}

void
PennSearch::HandleChordPingRecv (Ipv4Address destAddress, std::string message)
{
  SEARCH_LOG ("Chord Layer Received Ping! Source nodeId: " << ReverseLookup (destAddress) << " IP: " << destAddress << " Message: " << message);
}

// Override PennLog

void
PennSearch::SetTrafficVerbose (bool on)
{
  m_chord->SetTrafficVerbose (on);
  g_trafficVerbose = on;
}

void
PennSearch::SetErrorVerbose (bool on)
{
  m_chord->SetErrorVerbose (on);
  g_errorVerbose = on;
}

void
PennSearch::SetDebugVerbose (bool on)
{
  m_chord->SetDebugVerbose (on);
  g_debugVerbose = on;
}

void
PennSearch::SetStatusVerbose (bool on)
{
  m_chord->SetStatusVerbose (on);
  g_statusVerbose = on;
}

void
PennSearch::SetChordVerbose (bool on)
{
  m_chord->SetChordVerbose (on);
  g_chordVerbose = on;
}

void
PennSearch::SetSearchVerbose (bool on)
{
  m_chord->SetSearchVerbose (on);
  g_searchVerbose = on;
}
