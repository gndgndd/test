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
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"

using namespace ns3;

TypeId
PennChord::GetTypeId ()
{
  static TypeId tid
      = TypeId ("PennChord")
            .SetParent<PennApplication> ()
            .AddConstructor<PennChord> ()
            .AddAttribute ("AppPort", "Listening port for Application",
                           UintegerValue (10001),
                           MakeUintegerAccessor (&PennChord::m_appPort),
                           MakeUintegerChecker<uint16_t> ())
            .AddAttribute ("PingTimeout", "Timeout value for PING_REQ in milliseconds",
                           TimeValue (MilliSeconds (2000)),
                           MakeTimeAccessor (&PennChord::m_pingTimeout),
                           MakeTimeChecker ())
            .AddAttribute ("StabilizeInterval", "Chord stabilize interval in milliseconds",
                           TimeValue (MilliSeconds (1000)),
                           MakeTimeAccessor (&PennChord::m_stabilizeInterval),
                           MakeTimeChecker ())
  ;
  return tid;
}

PennChord::PennChord ()
  : m_auditPingsTimer (Timer::CANCEL_ON_DESTROY),
    m_stabilizeTimer  (Timer::CANCEL_ON_DESTROY)
{
  Ptr<UniformRandomVariable> u = CreateObject<UniformRandomVariable> ();
  m_currentTransactionId = (uint32_t) u->GetInteger (0x00000000, 0xFFFFFFFF);
  m_self = Ipv4Address::GetAny ();
  m_pred = Ipv4Address::GetAny ();
  m_succ = Ipv4Address::GetAny ();
}

PennChord::~PennChord () {}

void
PennChord::DoDispose ()
{
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

  // derive my IP address from the provided map (simulation stack)
  // Prefer: node id lookup (primary) then fallback to m_local if in real stack mode
  std::map<uint32_t, Ipv4Address>::iterator it = m_nodeAddressMap.find (GetNode()->GetId());
  if (it != m_nodeAddressMap.end())
    m_self = it->second;
  else
    m_self = GetLocalAddress(); // in case of real stack

  // Initially not in any ring
  m_pred = Ipv4Address::GetAny ();
  m_succ = Ipv4Address::GetAny ();

  // Configure timers
  m_auditPingsTimer.SetFunction (&PennChord::AuditPings, this);
  m_auditPingsTimer.Schedule (m_pingTimeout);

  m_stabilizeTimer.SetFunction (&PennChord::DoStabilize, this);
}

void
PennChord::StopApplication (void)
{
  if (m_socket)
    {
      m_socket->Close ();
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
      m_socket = 0;
    }
  m_auditPingsTimer.Cancel ();
  m_stabilizeTimer.Cancel ();
  m_pingTracker.clear ();
}

void
PennChord::ProcessCommand (std::vector<std::string> tokens)
{
  if (tokens.empty())
    return;

  std::string cmd = tokens[0];
  // normalize to lowercase
  for (char &c : cmd) c = std::tolower (c);

  if (cmd == "join")
    {
      if (tokens.size() < 2)
        {
          ERROR_LOG ("JOIN requires a node number argument");
          return;
        }
      Ipv4Address known = ResolveNodeIpAddress (tokens[1]);
      if (known == Ipv4Address::GetAny())
        {
          ERROR_LOG ("JOIN: could not resolve node " << tokens[1]);
          return;
        }
      DoJoin (known);
      ScheduleStabilize ();
      return;
    }

  if (cmd == "leave")
    {
      // Minimal leave for MS1: break links (stabilization of others will heal)
      m_pred = Ipv4Address::GetAny();
      m_succ = Ipv4Address::GetAny();
      return;
    }

  if (cmd == "ringstate")
    {
      StartRingState ();
      return;
    }

  if (cmd == "ping")
    {
      if (tokens.size() < 3)
        {
          ERROR_LOG ("PING requires: <destNode> <message>");
          return;
        }
      Ipv4Address dest = ResolveNodeIpAddress (tokens[1]);
      std::string msg = tokens[2];
      SendPing (dest, msg);
      return;
    }
}

/* -------------------------- PING (given) -------------------------- */

void
PennChord::SendPing (Ipv4Address destAddress, std::string pingMessage)
{
  if (destAddress != Ipv4Address::GetAny ())
    {
      uint32_t transactionId = GetNextTransactionId ();
      CHORD_LOG ("Sending PING_REQ to Node: " << ReverseLookup(destAddress)
                 << " IP: " << destAddress << " Message: " << pingMessage
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

      /* ---- MS1 messages ---- */
      case PennChordMessage::JOIN_FIND:
      {
        PennChordMessage::JoinFind jf = message.GetJoinFind ();
        // If I'm not in any ring yet, make myself my successor
        if (m_succ == Ipv4Address::GetAny())
          m_succ = m_self;

        uint32_t curr = Hash32 (m_self);
        uint32_t succ = Hash32 (m_succ);
        uint32_t key  = jf.joinerHash;

        // single-node ring
        if (m_succ == m_self)
          {
            PennChordMessage reply (PennChordMessage::JOIN_REPLY, GetNextTransactionId());
            reply.SetJoinReply (m_self);
            SendTo (jf.joiner, reply);
          }
        else if (InIntervalWrapAware (key, curr, succ))
          {
            PennChordMessage reply (PennChordMessage::JOIN_REPLY, GetNextTransactionId());
            reply.SetJoinReply (m_succ);
            SendTo (jf.joiner, reply);
          }
        else
          {
            // Greedy forward to successor
            PennChordMessage fwd (PennChordMessage::JOIN_FIND, GetNextTransactionId());
            fwd.SetJoinFind (jf.joiner, jf.joinerHash, (jf.origin == Ipv4Address::GetAny()? m_self : jf.origin));
            SendTo (m_succ, fwd);
          }
        break;
      }

      case PennChordMessage::JOIN_REPLY:
      {
        // This reply is addressed to the joiner (me)
        PennChordMessage::JoinReply jr = message.GetJoinReply ();
        m_succ = jr.successor;  // set successor
        // let stabilization/notify settle the predecessor pointers
        ScheduleStabilize ();
        break;
      }

      case PennChordMessage::STAB_REQ:
      {
        // Reply with my predecessor
        PennChordMessage rsp (PennChordMessage::STAB_RSP, GetNextTransactionId());
        rsp.SetStabilizeRsp (m_pred);
        SendTo (sourceAddress, rsp);
        break;
      }

      case PennChordMessage::STAB_RSP:
      {
        PennChordMessage::StabilizeRsp sr = message.GetStabilizeRsp ();
        Ipv4Address x = sr.predecessor; // predecessor of my successor
        if (m_succ == Ipv4Address::GetAny())
          {
            // nothing to do
          }
        else
          {
            uint32_t selfH = Hash32 (m_self);
            uint32_t succH = Hash32 (m_succ);
            uint32_t xH    = Hash32 (x);

            // If x is between me and my successor, adopt x as my successor
            if (x != Ipv4Address::GetAny() && InIntervalWrapAware (xH, selfH, succH))
              {
                m_succ = x;
              }
            // In any case, notify my (possibly updated) successor
            PennChordMessage ntf (PennChordMessage::NOTIFY, GetNextTransactionId());
            ntf.SetNotify (m_self);
            SendTo (m_succ, ntf);
          }
        ScheduleStabilize ();
        break;
      }

      case PennChordMessage::NOTIFY:
      {
        PennChordMessage::NotifyMsg n = message.GetNotify ();
        Ipv4Address cand = n.candidate;
        uint32_t selfH = Hash32 (m_self);
        uint32_t predH = Hash32 (m_pred);
        uint32_t candH = Hash32 (cand);

        if (m_pred == Ipv4Address::GetAny() || InIntervalWrapAware (candH, predH, selfH))
          {
            m_pred = cand;
          }
        break;
      }

      case PennChordMessage::RINGSTATE:
      {
        PennChordMessage::RingStateMsg rs = message.GetRingState ();
        // Always log once at this node
        LogRingStateOnce ();

        // If my successor is the origin, I'm the predecessor of origin -> end
        if (m_succ == rs.origin)
          {
            GraderLogs::EndOfRingState ();
          }
        else
          {
            PennChordMessage fwd (PennChordMessage::RINGSTATE, GetNextTransactionId());
            fwd.SetRingState (rs.origin);
            SendTo (m_succ, fwd);
          }
        break;
      }

      default:
        ERROR_LOG ("Unknown Message Type!");
        break;
    }
}

void
PennChord::ProcessPingReq (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort)
{
  std::string fromNode = ReverseLookup (sourceAddress);
  CHORD_LOG ("Received PING_REQ, From Node: " << fromNode << ", Message: " << message.GetPingReq().pingMessage);
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
  std::map<uint32_t, Ptr<PingRequest> >::iterator iter = m_pingTracker.find (message.GetTransactionId ());
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

/* -------------------------- Chord helpers -------------------------- */

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
PennChord::SendTo (Ipv4Address dst, const PennChordMessage &msg)
{
  Ptr<Packet> p = Create<Packet> ();
  PennChordMessage copy = msg; // AddHeader requires non-const
  p->AddHeader (copy);
  m_socket->SendTo (p, 0, InetSocketAddress (dst, m_appPort));
}

void
PennChord::DoCreateRing ()
{
  // Create a single-node ring
  m_pred = m_self;
  m_succ = m_self;
}

void
PennChord::DoJoin (Ipv4Address knownNode)
{
  if (knownNode == m_self)
    {
      DoCreateRing ();
      return;
    }

  // Ask the known node to find my successor
  uint32_t myH = Hash32 (m_self);
  PennChordMessage q (PennChordMessage::JOIN_FIND, GetNextTransactionId());
  q.SetJoinFind (m_self, myH, m_self);
  SendTo (knownNode, q);
}

void
PennChord::ScheduleStabilize ()
{
  if (!m_stabilizeTimer.IsRunning())
    m_stabilizeTimer.Schedule (m_stabilizeInterval);
}

void
PennChord::DoStabilize ()
{
  if (m_succ == Ipv4Address::GetAny())
    {
      // not in ring yet
      ScheduleStabilize ();
      return;
    }
  // ask successor for its predecessor
  PennChordMessage req (PennChordMessage::STAB_REQ, GetNextTransactionId());
  req.SetStabilizeReq ();
  SendTo (m_succ, req);

  // reschedule periodically
  ScheduleStabilize ();
}

bool
PennChord::InIntervalOpenClosed (uint32_t key, uint32_t a, uint32_t b) const
{
  // (a, b]
  if (a < b) return (key > a && key <= b);
  if (a > b) return (key > a || key <= b); // wrap-around
  // a == b means full circle; treat as true
  return true;
}

bool
PennChord::InIntervalWrapAware (uint32_t key, uint32_t a, uint32_t b) const
{
  return InIntervalOpenClosed (key, a, b);
}

uint32_t
PennChord::Hash32 (Ipv4Address ip) const
{
  // Use helper provided by the project to get a stable 32-bit hash
  return CreateShaKey (ip);
}

uint32_t
PennChord::NodeIdFromIp (Ipv4Address ip) const
{
  std::map<Ipv4Address, uint32_t>::const_iterator it = m_addressNodeMap.find (ip);
  if (it != m_addressNodeMap.end()) return it->second;
  return 0;
}

/* -------------------------- RINGSTATE -------------------------- */

void
PennChord::LogRingStateOnce ()
{
  Ipv4Address curr = m_self;
  Ipv4Address pred = (m_pred == Ipv4Address::GetAny()) ? Ipv4Address::GetAny() : m_pred;
  Ipv4Address succ = (m_succ == Ipv4Address::GetAny()) ? Ipv4Address::GetAny() : m_succ;

  uint32_t currId  = NodeIdFromIp (curr);
  uint32_t predId  = NodeIdFromIp (pred);
  uint32_t succId  = NodeIdFromIp (succ);

  uint32_t currH   = Hash32 (curr);
  uint32_t predH   = Hash32 (pred);
  uint32_t succH   = Hash32 (succ);

  GraderLogs::RingState (currId, curr, currH,
                         predId, pred, predH,
                         succId, succ, succH);
}

void
PennChord::StartRingState ()
{
  if (m_succ == Ipv4Address::GetAny())
    {
      ERROR_LOG ("RINGSTATE: node " << ReverseLookup(m_self) << " not in any ring");
      return;
    }

  // Print my line first
  LogRingStateOnce ();

  if (m_succ == m_self)
    {
      // single-node ring: I'm predecessor of origin
      GraderLogs::EndOfRingState ();
      return;
    }

  PennChordMessage msg (PennChordMessage::RINGSTATE, GetNextTransactionId());
  msg.SetRingState (m_self);
  SendTo (m_succ, msg);
}

/* -------------------------- Ping auditing -------------------------- */

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
          m_pingTracker.erase (iter++);
          m_pingFailureFn (pingRequest->GetDestinationAddress(), pingRequest->GetPingMessage ());
        }
      else
        {
          ++iter;
        }
    }
  m_auditPingsTimer.Schedule (m_pingTimeout);
}
