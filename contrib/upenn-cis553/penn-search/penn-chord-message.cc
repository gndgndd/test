/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/penn-chord-message.h"
#include "ns3/log.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("PennChordMessage");
NS_OBJECT_ENSURE_REGISTERED (PennChordMessage);

PennChordMessage::PennChordMessage () {}
PennChordMessage::~PennChordMessage () {}

PennChordMessage::PennChordMessage (PennChordMessage::MessageType messageType, uint32_t transactionId) {
  m_messageType = messageType;
  m_transactionId = transactionId;
}

TypeId PennChordMessage::GetTypeId (void) {
  static TypeId tid = TypeId ("PennChordMessage").SetParent<Header> ().AddConstructor<PennChordMessage> ();
  return tid;
}

TypeId PennChordMessage::GetInstanceTypeId (void) const { return GetTypeId (); }

uint32_t PennChordMessage::GetSerializedSize (void) const {
  uint32_t size = sizeof (uint8_t) + sizeof (uint32_t);
  switch (m_messageType) {
      case PING_REQ: size += m_message.pingReq.GetSerializedSize (); break;
      case PING_RSP: size += m_message.pingRsp.GetSerializedSize (); break;
      case LOOKUP_REQ: size += m_message.lookupReq.GetSerializedSize (); break;
      case LOOKUP_FORWARD: size += m_message.lookupForward.GetSerializedSize (); break;
      case LOOKUP_RSP: size += m_message.lookupRsp.GetSerializedSize (); break;
      case STABILIZE_REQ: size += m_message.stabilizeReq.GetSerializedSize (); break;
      case STABILIZE_RSP: size += m_message.stabilizeRsp.GetSerializedSize (); break;
      case NOTIFY_PKT: size += m_message.notifyPkt.GetSerializedSize (); break;
      case RINGSTATE_MSG: size += m_message.ringstateMsg.GetSerializedSize (); break;
      default: NS_ASSERT (false);
    }
  return size;
}

void PennChordMessage::Print (std::ostream &os) const {
  os << "\n****PennChordMessage Dump****\n messageType: " << m_messageType << "\n transactionId: " << m_transactionId << "\n";
  switch (m_messageType) {
      case PING_REQ: m_message.pingReq.Print (os); break;
      case PING_RSP: m_message.pingRsp.Print (os); break;
      case LOOKUP_REQ: m_message.lookupReq.Print (os); break;
      case LOOKUP_FORWARD: m_message.lookupForward.Print (os); break;
      case LOOKUP_RSP: m_message.lookupRsp.Print (os); break;
      case STABILIZE_REQ: m_message.stabilizeReq.Print (os); break;
      case STABILIZE_RSP: m_message.stabilizeRsp.Print (os); break;
      case NOTIFY_PKT: m_message.notifyPkt.Print (os); break;
      case RINGSTATE_MSG: m_message.ringstateMsg.Print (os); break;
      default: break;  
    }
  os << "\n****END OF MESSAGE****\n";
}

void PennChordMessage::Serialize (Buffer::Iterator start) const {
  Buffer::Iterator i = start;
  i.WriteU8 (m_messageType);
  i.WriteHtonU32 (m_transactionId);
  switch (m_messageType) {
      case PING_REQ: m_message.pingReq.Serialize (i); break;
      case PING_RSP: m_message.pingRsp.Serialize (i); break;
      case LOOKUP_REQ: m_message.lookupReq.Serialize (i); break;
      case LOOKUP_FORWARD: m_message.lookupForward.Serialize (i); break;
      case LOOKUP_RSP: m_message.lookupRsp.Serialize (i); break;
      case STABILIZE_REQ: m_message.stabilizeReq.Serialize (i); break;
      case STABILIZE_RSP: m_message.stabilizeRsp.Serialize (i); break;
      case NOTIFY_PKT: m_message.notifyPkt.Serialize (i); break;
      case RINGSTATE_MSG: m_message.ringstateMsg.Serialize (i); break;
      default: NS_ASSERT (false);   
    }
}

uint32_t PennChordMessage::Deserialize (Buffer::Iterator start) {
  uint32_t size;
  Buffer::Iterator i = start;
  m_messageType = (MessageType) i.ReadU8 ();
  m_transactionId = i.ReadNtohU32 ();
  size = sizeof (uint8_t) + sizeof (uint32_t);
  switch (m_messageType) {
      case PING_REQ: size += m_message.pingReq.Deserialize (i); break;
      case PING_RSP: size += m_message.pingRsp.Deserialize (i); break;
      case LOOKUP_REQ: size += m_message.lookupReq.Deserialize (i); break;
      case LOOKUP_FORWARD: size += m_message.lookupForward.Deserialize (i); break;
      case LOOKUP_RSP: size += m_message.lookupRsp.Deserialize (i); break;
      case STABILIZE_REQ: size += m_message.stabilizeReq.Deserialize (i); break;
      case STABILIZE_RSP: size += m_message.stabilizeRsp.Deserialize (i); break;
      case NOTIFY_PKT: size += m_message.notifyPkt.Deserialize (i); break;
      case RINGSTATE_MSG: size += m_message.ringstateMsg.Deserialize (i); break;
      default: NS_ASSERT (false);
    }
  return size;
}

/* STRUCT IMPLEMENTATIONS */

// PingReq
uint32_t PennChordMessage::PingReq::GetSerializedSize (void) const { return sizeof(uint16_t) + pingMessage.length(); }
void PennChordMessage::PingReq::Print (std::ostream &os) const { os << "PingReq: " << pingMessage << "\n"; }
void PennChordMessage::PingReq::Serialize (Buffer::Iterator &start) const { start.WriteU16 (pingMessage.length ()); start.Write ((uint8_t *) (const_cast<char*> (pingMessage.c_str())), pingMessage.length()); }
uint32_t PennChordMessage::PingReq::Deserialize (Buffer::Iterator &start) { uint16_t len = start.ReadU16 (); char* str = (char*) malloc (len); start.Read ((uint8_t*)str, len); pingMessage = std::string (str, len); free (str); return GetSerializedSize (); }
void PennChordMessage::SetPingReq (std::string msg) { m_messageType = PING_REQ; m_message.pingReq.pingMessage = msg; }
PennChordMessage::PingReq PennChordMessage::GetPingReq () { return m_message.pingReq; }

// PingRsp
uint32_t PennChordMessage::PingRsp::GetSerializedSize (void) const { return sizeof(uint16_t) + pingMessage.length(); }
void PennChordMessage::PingRsp::Print (std::ostream &os) const { os << "PingRsp: " << pingMessage << "\n"; }
void PennChordMessage::PingRsp::Serialize (Buffer::Iterator &start) const { start.WriteU16 (pingMessage.length ()); start.Write ((uint8_t *) (const_cast<char*> (pingMessage.c_str())), pingMessage.length()); }
uint32_t PennChordMessage::PingRsp::Deserialize (Buffer::Iterator &start) { uint16_t len = start.ReadU16 (); char* str = (char*) malloc (len); start.Read ((uint8_t*)str, len); pingMessage = std::string (str, len); free (str); return GetSerializedSize (); }
void PennChordMessage::SetPingRsp (std::string msg) { m_messageType = PING_RSP; m_message.pingRsp.pingMessage = msg; }
PennChordMessage::PingRsp PennChordMessage::GetPingRsp () { return m_message.pingRsp; }

// LookupReq
uint32_t PennChordMessage::LookupReq::GetSerializedSize () const { return sizeof(uint32_t) + IPV4_ADDRESS_SIZE + IPV4_ADDRESS_SIZE; }
void PennChordMessage::LookupReq::Print (std::ostream &os) const { os << "LookupReq: Key=" << lookupKey << " Origin=" << originator << "\n"; }
void PennChordMessage::LookupReq::Serialize (Buffer::Iterator &i) const { i.WriteHtonU32 (lookupKey); i.WriteHtonU32 (originator.Get ()); i.WriteHtonU32 (lastHop.Get ()); }
uint32_t PennChordMessage::LookupReq::Deserialize (Buffer::Iterator &i) { lookupKey = i.ReadNtohU32 (); originator = Ipv4Address(i.ReadNtohU32 ()); lastHop = Ipv4Address(i.ReadNtohU32 ()); return GetSerializedSize (); }
void PennChordMessage::SetLookupReq (uint32_t key, Ipv4Address origin, Ipv4Address hop) { m_messageType = LOOKUP_REQ; m_message.lookupReq.lookupKey = key; m_message.lookupReq.originator = origin; m_message.lookupReq.lastHop = hop; }
PennChordMessage::LookupReq PennChordMessage::GetLookupReq () { return m_message.lookupReq; }

// LookupForward
uint32_t PennChordMessage::LookupForward::GetSerializedSize () const { return sizeof(uint32_t) + IPV4_ADDRESS_SIZE + IPV4_ADDRESS_SIZE; }
void PennChordMessage::LookupForward::Print (std::ostream &os) const { os << "LookupForward: Key=" << lookupKey << "\n"; }
void PennChordMessage::LookupForward::Serialize (Buffer::Iterator &i) const { i.WriteHtonU32 (lookupKey); i.WriteHtonU32 (originator.Get ()); i.WriteHtonU32 (lastHop.Get ()); }
uint32_t PennChordMessage::LookupForward::Deserialize (Buffer::Iterator &i) { lookupKey = i.ReadNtohU32 (); originator = Ipv4Address(i.ReadNtohU32 ()); lastHop = Ipv4Address(i.ReadNtohU32 ()); return GetSerializedSize (); }
void PennChordMessage::SetLookupForward (uint32_t key, Ipv4Address origin, Ipv4Address hop) { m_messageType = LOOKUP_FORWARD; m_message.lookupForward.lookupKey = key; m_message.lookupForward.originator = origin; m_message.lookupForward.lastHop = hop; }
PennChordMessage::LookupForward PennChordMessage::GetLookupForward () { return m_message.lookupForward; }

// LookupRsp
uint32_t PennChordMessage::LookupRsp::GetSerializedSize () const { return sizeof(uint32_t) + IPV4_ADDRESS_SIZE; }
void PennChordMessage::LookupRsp::Print (std::ostream &os) const { os << "LookupRsp: Key=" << lookupKey << " Owner=" << ownerNode << "\n"; }
void PennChordMessage::LookupRsp::Serialize (Buffer::Iterator &i) const { i.WriteHtonU32 (lookupKey); i.WriteHtonU32 (ownerNode.Get ()); }
uint32_t PennChordMessage::LookupRsp::Deserialize (Buffer::Iterator &i) { lookupKey = i.ReadNtohU32 (); ownerNode = Ipv4Address(i.ReadNtohU32 ()); return GetSerializedSize (); }
void PennChordMessage::SetLookupRsp (uint32_t key, Ipv4Address owner) { m_messageType = LOOKUP_RSP; m_message.lookupRsp.lookupKey = key; m_message.lookupRsp.ownerNode = owner; }
PennChordMessage::LookupRsp PennChordMessage::GetLookupRsp () { return m_message.lookupRsp; }

// StabilizeReq
uint32_t PennChordMessage::StabilizeReq::GetSerializedSize (void) const { return IPV4_ADDRESS_SIZE; }
void PennChordMessage::StabilizeReq::Print (std::ostream &os) const { os << "StabilizeReq: Sender=" << senderIp << "\n"; }
void PennChordMessage::StabilizeReq::Serialize (Buffer::Iterator &start) const { start.WriteHtonU32(senderIp.Get()); }
uint32_t PennChordMessage::StabilizeReq::Deserialize (Buffer::Iterator &start) { senderIp = Ipv4Address(start.ReadNtohU32()); return GetSerializedSize(); }
void PennChordMessage::SetStabilizeReq (Ipv4Address sender) { m_messageType = STABILIZE_REQ; m_message.stabilizeReq.senderIp = sender; }
PennChordMessage::StabilizeReq PennChordMessage::GetStabilizeReq () { return m_message.stabilizeReq; }

// StabilizeRsp
uint32_t PennChordMessage::StabilizeRsp::GetSerializedSize (void) const { return IPV4_ADDRESS_SIZE; }
void PennChordMessage::StabilizeRsp::Print (std::ostream &os) const { os << "StabilizeRsp: Pred=" << predecessorIp << "\n"; }
void PennChordMessage::StabilizeRsp::Serialize (Buffer::Iterator &start) const { start.WriteHtonU32(predecessorIp.Get()); }
uint32_t PennChordMessage::StabilizeRsp::Deserialize (Buffer::Iterator &start) { predecessorIp = Ipv4Address(start.ReadNtohU32()); return GetSerializedSize(); }
void PennChordMessage::SetStabilizeRsp (Ipv4Address pred) { m_messageType = STABILIZE_RSP; m_message.stabilizeRsp.predecessorIp = pred; }
PennChordMessage::StabilizeRsp PennChordMessage::GetStabilizeRsp () { return m_message.stabilizeRsp; }

// NotifyPkt
uint32_t PennChordMessage::NotifyPkt::GetSerializedSize (void) const { return IPV4_ADDRESS_SIZE; }
void PennChordMessage::NotifyPkt::Print (std::ostream &os) const { os << "NotifyPkt: Candidate=" << candidateIp << "\n"; }
void PennChordMessage::NotifyPkt::Serialize (Buffer::Iterator &start) const { start.WriteHtonU32(candidateIp.Get()); }
uint32_t PennChordMessage::NotifyPkt::Deserialize (Buffer::Iterator &start) { candidateIp = Ipv4Address(start.ReadNtohU32()); return GetSerializedSize(); }
void PennChordMessage::SetNotifyPkt (Ipv4Address cand) { m_messageType = NOTIFY_PKT; m_message.notifyPkt.candidateIp = cand; }
PennChordMessage::NotifyPkt PennChordMessage::GetNotifyPkt () { return m_message.notifyPkt; }

// RingstateMsg
uint32_t PennChordMessage::RingstateMsg::GetSerializedSize () const { return IPV4_ADDRESS_SIZE; }
void PennChordMessage::RingstateMsg::Print (std::ostream &os) const { os << "RingstateMsg: Initiator=" << initiatorNode << "\n"; }
void PennChordMessage::RingstateMsg::Serialize (Buffer::Iterator &i) const { i.WriteHtonU32 (initiatorNode.Get ()); }
uint32_t PennChordMessage::RingstateMsg::Deserialize (Buffer::Iterator &i) { initiatorNode = Ipv4Address(i.ReadNtohU32 ()); return GetSerializedSize (); }
void PennChordMessage::setRingstateMsg (Ipv4Address init) { m_messageType = RINGSTATE_MSG; m_message.ringstateMsg.initiatorNode = init; }
PennChordMessage::RingstateMsg PennChordMessage::getRingstateMsg () { return m_message.ringstateMsg; }


// --- MISSING ACCESSORS (ADDED TO FIX LINKER ERROR) ---

void PennChordMessage::SetMessageType (MessageType messageType) {
  m_messageType = messageType;
}

PennChordMessage::MessageType PennChordMessage::GetMessageType () const {
  return m_messageType;
}

void PennChordMessage::SetTransactionId (uint32_t transactionId) {
  m_transactionId = transactionId;
}

uint32_t PennChordMessage::GetTransactionId (void) const {
  return m_transactionId;
}