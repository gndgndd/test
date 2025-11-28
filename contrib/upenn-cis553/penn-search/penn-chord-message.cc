/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "penn-chord-message.h"
#include "ns3/address-utils.h"

using namespace ns3;

NS_OBJECT_ENSURE_REGISTERED (PennChordMessage);

PennChordMessage::PennChordMessage () : m_type (PING_REQ), m_transactionId (0) {}

PennChordMessage::PennChordMessage (MessageType type, uint32_t transactionId)
  : m_type (type), m_transactionId (transactionId) {}

PennChordMessage::~PennChordMessage () {}

TypeId PennChordMessage::GetTypeId (void)
{
  static TypeId tid = TypeId ("PennChordMessage")
    .SetParent<Header> ()
    .AddConstructor<PennChordMessage> ();
  return tid;
}

TypeId PennChordMessage::GetInstanceTypeId (void) const { return GetTypeId (); }

void PennChordMessage::Print (std::ostream &os) const
{
  os << "Type=" << m_type << " Txn=" << m_transactionId;
}

uint32_t PennChordMessage::GetSerializedSize (void) const
{
  uint32_t size = 4 + 4; // Type + TxnId
  
  switch (m_type) {
    case PING_REQ: size += m_pingReq.pingMessage.length() + 4; break;
    case PING_RSP: size += m_pingRsp.pingMessage.length() + 4; break;
    case LOOKUP_REQ: size += 4 + 4 + 4; break; // Key + Origin + LastHop
    case LOOKUP_FORWARD: size += 4 + 4 + 4; break;
    case LOOKUP_RSP: size += 4 + 4; break;
    case GET_PREDECESSOR: break; // No payload
    case PREDECESSOR_RSP: size += 4; break;
    case NOTIFY: size += 4; break;
    case RINGSTATE_REQ: size += 4; break;
    default: break;
  }
  return size;
}

void PennChordMessage::Serialize (Buffer::Iterator start) const
{
  start.WriteHtonU32 (m_type);
  start.WriteHtonU32 (m_transactionId);

  switch (m_type) {
    case PING_REQ:
      start.WriteHtonU32 (m_pingReq.pingMessage.length());
      start.Write ((const uint8_t*)m_pingReq.pingMessage.c_str(), m_pingReq.pingMessage.length());
      break;
    case PING_RSP:
      start.WriteHtonU32 (m_pingRsp.pingMessage.length());
      start.Write ((const uint8_t*)m_pingRsp.pingMessage.c_str(), m_pingRsp.pingMessage.length());
      break;
    case LOOKUP_REQ:
      start.WriteHtonU32 (m_lookupReq.lookupKey);
      WriteTo (start, m_lookupReq.originator);
      WriteTo (start, m_lookupReq.lastHop);
      break;
    case LOOKUP_FORWARD:
      start.WriteHtonU32 (m_lookupFwd.lookupKey);
      WriteTo (start, m_lookupFwd.originator);
      WriteTo (start, m_lookupFwd.lastHop);
      break;
    case LOOKUP_RSP:
      start.WriteHtonU32 (m_lookupRsp.lookupKey);
      WriteTo (start, m_lookupRsp.ownerNode);
      break;
    case PREDECESSOR_RSP:
      WriteTo (start, m_predRsp.predecessorNode);
      break;
    case NOTIFY:
      WriteTo (start, m_notify.potentialPredecessor);
      break;
    case RINGSTATE_REQ:
      WriteTo (start, m_ringState.originNode);
      break;
    default: break;
  }
}

uint32_t PennChordMessage::Deserialize (Buffer::Iterator start)
{
  Buffer::Iterator originalStart = start;
  m_type = (MessageType)start.ReadNtohU32 ();
  m_transactionId = start.ReadNtohU32 ();

  if (m_type == PING_REQ) {
    uint32_t len = start.ReadNtohU32 ();
    char buf[1024];
    start.Read ((uint8_t*)buf, len);
    buf[len] = '\0';
    m_pingReq.pingMessage = std::string(buf);
  } else if (m_type == PING_RSP) {
    uint32_t len = start.ReadNtohU32 ();
    char buf[1024];
    start.Read ((uint8_t*)buf, len);
    buf[len] = '\0';
    m_pingRsp.pingMessage = std::string(buf);
  } else if (m_type == LOOKUP_REQ) {
    m_lookupReq.lookupKey = start.ReadNtohU32 ();
    ReadFrom (start, m_lookupReq.originator);
    ReadFrom (start, m_lookupReq.lastHop);
  } else if (m_type == LOOKUP_FORWARD) {
    m_lookupFwd.lookupKey = start.ReadNtohU32 ();
    ReadFrom (start, m_lookupFwd.originator);
    ReadFrom (start, m_lookupFwd.lastHop);
  } else if (m_type == LOOKUP_RSP) {
    m_lookupRsp.lookupKey = start.ReadNtohU32 ();
    ReadFrom (start, m_lookupRsp.ownerNode);
  } else if (m_type == PREDECESSOR_RSP) {
    ReadFrom (start, m_predRsp.predecessorNode);
  } else if (m_type == NOTIFY) {
    ReadFrom (start, m_notify.potentialPredecessor);
  } else if (m_type == RINGSTATE_REQ) {
    ReadFrom (start, m_ringState.originNode);
  }

  return start.GetDistanceFrom (originalStart);
}

// Getters and Setters implementation
PennChordMessage::MessageType PennChordMessage::GetMessageType() const { return m_type; }
uint32_t PennChordMessage::GetTransactionId() const { return m_transactionId; }

void PennChordMessage::SetPingReq(std::string msg) { m_pingReq.pingMessage = msg; }
PennChordMessage::PingReq PennChordMessage::GetPingReq() const { return m_pingReq; }

void PennChordMessage::SetPingRsp(std::string msg) { m_pingRsp.pingMessage = msg; }
PennChordMessage::PingRsp PennChordMessage::GetPingRsp() const { return m_pingRsp; }

void PennChordMessage::SetLookupReq(uint32_t key, Ipv4Address origin, Ipv4Address hop) { 
  m_lookupReq.lookupKey = key; m_lookupReq.originator = origin; m_lookupReq.lastHop = hop; 
}
PennChordMessage::LookupReq PennChordMessage::GetLookupReq() const { return m_lookupReq; }

void PennChordMessage::SetLookupForward(uint32_t key, Ipv4Address origin, Ipv4Address hop) {
  m_lookupFwd.lookupKey = key; m_lookupFwd.originator = origin; m_lookupFwd.lastHop = hop;
}
PennChordMessage::LookupForward PennChordMessage::GetLookupForward() const { return m_lookupFwd; }

void PennChordMessage::SetLookupRsp(uint32_t key, Ipv4Address owner) {
  m_lookupRsp.lookupKey = key; m_lookupRsp.ownerNode = owner;
}
PennChordMessage::LookupRsp PennChordMessage::GetLookupRsp() const { return m_lookupRsp; }

void PennChordMessage::SetPredecessorRsp(Ipv4Address pred) { m_predRsp.predecessorNode = pred; }
PennChordMessage::PredecessorRsp PennChordMessage::GetPredecessorRsp() const { return m_predRsp; }

void PennChordMessage::SetNotify(Ipv4Address potPred) { m_notify.potentialPredecessor = potPred; }
PennChordMessage::NotifyReq PennChordMessage::GetNotify() const { return m_notify; }

void PennChordMessage::SetRingStateReq(Ipv4Address origin) { m_ringState.originNode = origin; }
PennChordMessage::RingStateReq PennChordMessage::GetRingStateReq() const { return m_ringState; }