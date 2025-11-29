/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/penn-chord-message.h"
#include "ns3/log.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("PennChordMessage");
NS_OBJECT_ENSURE_REGISTERED (PennChordMessage);

PennChordMessage::PennChordMessage () {}
PennChordMessage::~PennChordMessage () {}

PennChordMessage::PennChordMessage (PennChordMessage::MessageType messageType, uint32_t transactionId)
{
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
      case RINGSTATE_MSG: size += m_message.ringstateMsg.GetSerializedSize (); break;
      case STABILIZE_REQ: size += m_message.stabilizeReq.GetSerializedSize (); break;
      case STABILIZE_RSP: size += m_message.stabilizeRsp.GetSerializedSize (); break; 
      case NOTIFY_MSG: size += m_message.notifyMsg.GetSerializedSize (); break;    
      case SET_SUCC_REQ: size += m_message.setSuccReq.GetSerializedSize (); break;
      default: NS_ASSERT (false);
  }
  return size;
}

void PennChordMessage::Print (std::ostream &os) const {
  os << "\n****PennChordMessage Dump****\n" ;
  os << "messageType: " << m_messageType << "\n";
  os << "transactionId: " << m_transactionId << "\n";
  os << "PAYLOAD:: \n";
  switch (m_messageType) {
      case PING_REQ: m_message.pingReq.Print (os); break;
      case PING_RSP: m_message.pingRsp.Print (os); break;
      case LOOKUP_REQ: m_message.lookupReq.Print (os); break;
      case LOOKUP_FORWARD: m_message.lookupForward.Print (os); break;
      case LOOKUP_RSP: m_message.lookupRsp.Print (os); break;
      case RINGSTATE_MSG: m_message.ringstateMsg.Print (os); break;
      case STABILIZE_REQ: m_message.stabilizeReq.Print (os); break;
      case STABILIZE_RSP: m_message.stabilizeRsp.Print (os); break;
      case NOTIFY_MSG: m_message.notifyMsg.Print (os); break;
      case SET_SUCC_REQ: m_message.setSuccReq.Print (os); break;
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
      case RINGSTATE_MSG: m_message.ringstateMsg.Serialize (i); break;
      case STABILIZE_REQ: m_message.stabilizeReq.Serialize (i); break;
      case STABILIZE_RSP: m_message.stabilizeRsp.Serialize (i); break;
      case NOTIFY_MSG: m_message.notifyMsg.Serialize (i); break;
      case SET_SUCC_REQ: m_message.setSuccReq.Serialize (i); break;
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
      case RINGSTATE_MSG: size += m_message.ringstateMsg.Deserialize (i); break;
      case STABILIZE_REQ: size += m_message.stabilizeReq.Deserialize (i); break;
      case STABILIZE_RSP: size += m_message.stabilizeRsp.Deserialize (i); break;
      case NOTIFY_MSG: size += m_message.notifyMsg.Deserialize (i); break;
      case SET_SUCC_REQ: size += m_message.setSuccReq.Deserialize (i); break;
      default: NS_ASSERT (false);
  }
  return size;
}

// ... Struct Implementations ...

uint32_t PennChordMessage::PingReq::GetSerializedSize (void) const { return sizeof(uint16_t) + pingMessage.length(); }
void PennChordMessage::PingReq::Print (std::ostream &os) const { os << "PingReq:: Message: " << pingMessage << "\n"; }
void PennChordMessage::PingReq::Serialize (Buffer::Iterator &start) const {
  start.WriteU16 (pingMessage.length ());
  start.Write ((uint8_t *) (const_cast<char*> (pingMessage.c_str())), pingMessage.length());
}
uint32_t PennChordMessage::PingReq::Deserialize (Buffer::Iterator &start) {
  uint16_t length = start.ReadU16 ();
  char* str = (char*) malloc (length);
  start.Read ((uint8_t*)str, length);
  pingMessage = std::string (str, length);
  free (str);
  return PingReq::GetSerializedSize ();
}

uint32_t PennChordMessage::PingRsp::GetSerializedSize (void) const { return sizeof(uint16_t) + pingMessage.length(); }
void PennChordMessage::PingRsp::Print (std::ostream &os) const { os << "PingReq:: Message: " << pingMessage << "\n"; }
void PennChordMessage::PingRsp::Serialize (Buffer::Iterator &start) const {
  start.WriteU16 (pingMessage.length ());
  start.Write ((uint8_t *) (const_cast<char*> (pingMessage.c_str())), pingMessage.length());
}
uint32_t PennChordMessage::PingRsp::Deserialize (Buffer::Iterator &start) {
  uint16_t length = start.ReadU16 ();
  char* str = (char*) malloc (length);
  start.Read ((uint8_t*)str, length);
  pingMessage = std::string (str, length);
  free (str);
  return PingRsp::GetSerializedSize ();
}

// LOOKUP SERIALIZATION
uint32_t PennChordMessage::LookupReq::GetSerializedSize () const { return sizeof(uint32_t) + IPV4_ADDRESS_SIZE + IPV4_ADDRESS_SIZE; }
void PennChordMessage::LookupReq::Print (std::ostream &os) const { os << "LookupReq\n"; }
void PennChordMessage::LookupReq::Serialize (Buffer::Iterator &i) const { i.WriteHtonU32 (lookupKey); i.WriteHtonU32 (originator.Get ()); i.WriteHtonU32 (lastHop.Get ()); }
uint32_t PennChordMessage::LookupReq::Deserialize (Buffer::Iterator &i) { lookupKey = i.ReadNtohU32 (); originator = Ipv4Address(i.ReadNtohU32 ()); lastHop = Ipv4Address(i.ReadNtohU32 ()); return GetSerializedSize (); }

uint32_t PennChordMessage::LookupForward::GetSerializedSize () const { return sizeof(uint32_t) + IPV4_ADDRESS_SIZE + IPV4_ADDRESS_SIZE; }
void PennChordMessage::LookupForward::Print (std::ostream &os) const { os << "LookupForward\n"; }
void PennChordMessage::LookupForward::Serialize (Buffer::Iterator &i) const { i.WriteHtonU32 (lookupKey); i.WriteHtonU32 (originator.Get ()); i.WriteHtonU32 (lastHop.Get ()); }
uint32_t PennChordMessage::LookupForward::Deserialize (Buffer::Iterator &i) { lookupKey = i.ReadNtohU32 (); originator = Ipv4Address(i.ReadNtohU32 ()); lastHop = Ipv4Address(i.ReadNtohU32 ()); return GetSerializedSize (); }

uint32_t PennChordMessage::LookupRsp::GetSerializedSize () const { return sizeof(uint32_t) + IPV4_ADDRESS_SIZE; }
void PennChordMessage::LookupRsp::Print (std::ostream &os) const { os << "LookupRsp\n"; }
void PennChordMessage::LookupRsp::Serialize (Buffer::Iterator &i) const { i.WriteHtonU32 (lookupKey); i.WriteHtonU32 (ownerNode.Get ()); }
uint32_t PennChordMessage::LookupRsp::Deserialize (Buffer::Iterator &i) { lookupKey = i.ReadNtohU32 (); ownerNode = Ipv4Address(i.ReadNtohU32 ()); return GetSerializedSize (); }

uint32_t PennChordMessage::RingstateMsg::GetSerializedSize () const { return IPV4_ADDRESS_SIZE; }
void PennChordMessage::RingstateMsg::Print (std::ostream &os) const { os << "Ringstate\n"; }
void PennChordMessage::RingstateMsg::Serialize (Buffer::Iterator &i) const { i.WriteHtonU32 (initiatorNode.Get ()); }
uint32_t PennChordMessage::RingstateMsg::Deserialize (Buffer::Iterator &i) { initiatorNode = Ipv4Address(i.ReadNtohU32 ()); return GetSerializedSize (); }

uint32_t PennChordMessage::StabilizeReq::GetSerializedSize () const { return IPV4_ADDRESS_SIZE; }
void PennChordMessage::StabilizeReq::Print (std::ostream &os) const { os << "StabilizeReq\n"; }
void PennChordMessage::StabilizeReq::Serialize (Buffer::Iterator &i) const { i.WriteHtonU32 (requestingNode.Get ()); }
uint32_t PennChordMessage::StabilizeReq::Deserialize (Buffer::Iterator &i) { requestingNode = Ipv4Address(i.ReadNtohU32 ()); return GetSerializedSize (); }

uint32_t PennChordMessage::StabilizeRsp::GetSerializedSize () const { return IPV4_ADDRESS_SIZE; }
void PennChordMessage::StabilizeRsp::Print (std::ostream &os) const { os << "StabilizeRsp\n"; }
void PennChordMessage::StabilizeRsp::Serialize (Buffer::Iterator &i) const { i.WriteHtonU32 (responsePredessor.Get ()); }
uint32_t PennChordMessage::StabilizeRsp::Deserialize (Buffer::Iterator &i) { responsePredessor = Ipv4Address(i.ReadNtohU32 ()); return GetSerializedSize (); }

uint32_t PennChordMessage::NotifyMsg::GetSerializedSize () const { return IPV4_ADDRESS_SIZE; }
void PennChordMessage::NotifyMsg::Print (std::ostream &os) const { os << "NotifyMsg\n"; }
void PennChordMessage::NotifyMsg::Serialize (Buffer::Iterator &i) const { i.WriteHtonU32 (potentialPredessor.Get ()); }
uint32_t PennChordMessage::NotifyMsg::Deserialize (Buffer::Iterator &i) { potentialPredessor = Ipv4Address(i.ReadNtohU32 ()); return GetSerializedSize (); }

// NEW: SetSuccReq Implementation
uint32_t PennChordMessage::SetSuccReq::GetSerializedSize () const { return IPV4_ADDRESS_SIZE; }
void PennChordMessage::SetSuccReq::Print (std::ostream &os) const { os << "SetSuccReq\n"; }
void PennChordMessage::SetSuccReq::Serialize (Buffer::Iterator &i) const { i.WriteHtonU32 (newSuccessor.Get ()); }
uint32_t PennChordMessage::SetSuccReq::Deserialize (Buffer::Iterator &i) { newSuccessor = Ipv4Address(i.ReadNtohU32 ()); return GetSerializedSize (); }

// Accessors
PennChordMessage::PingReq PennChordMessage::GetPingReq () { return m_message.pingReq; }
void PennChordMessage::SetPingReq (std::string pingMessage) { m_messageType = PING_REQ; m_message.pingReq.pingMessage = pingMessage; }
PennChordMessage::PingRsp PennChordMessage::GetPingRsp () { return m_message.pingRsp; }
void PennChordMessage::SetPingRsp (std::string pingMessage) { m_messageType = PING_RSP; m_message.pingRsp.pingMessage = pingMessage; }

PennChordMessage::LookupReq PennChordMessage::GetLookupReq () { return m_message.lookupReq; }
void PennChordMessage::SetLookupReq (uint32_t key, Ipv4Address origin, Ipv4Address lastHop) { m_messageType = LOOKUP_REQ; m_message.lookupReq.lookupKey = key; m_message.lookupReq.originator = origin; m_message.lookupReq.lastHop = lastHop; }
PennChordMessage::LookupForward PennChordMessage::GetLookupForward () { return m_message.lookupForward; }
void PennChordMessage::SetLookupForward (uint32_t key, Ipv4Address origin, Ipv4Address lastHop) { m_messageType = LOOKUP_FORWARD; m_message.lookupForward.lookupKey = key; m_message.lookupForward.originator = origin; m_message.lookupForward.lastHop = lastHop; }
PennChordMessage::LookupRsp PennChordMessage::GetLookupRsp () { return m_message.lookupRsp; }
void PennChordMessage::SetLookupRsp (uint32_t key, Ipv4Address owner) { m_messageType = LOOKUP_RSP; m_message.lookupRsp.lookupKey = key; m_message.lookupRsp.ownerNode = owner; }
PennChordMessage::RingstateMsg PennChordMessage::GetRingstateMsg () { return m_message.ringstateMsg; }
void PennChordMessage::SetRingstateMsg (Ipv4Address initiator) { m_messageType = RINGSTATE_MSG; m_message.ringstateMsg.initiatorNode = initiator; }
PennChordMessage::StabilizeReq PennChordMessage::GetStabilizeReq () { return m_message.stabilizeReq; }
void PennChordMessage::SetStabilizeReq (Ipv4Address requestor) { m_messageType = STABILIZE_REQ; m_message.stabilizeReq.requestingNode = requestor; }
PennChordMessage::StabilizeRsp PennChordMessage::GetStabilizeRsp () { return m_message.stabilizeRsp; }
void PennChordMessage::SetStabilizeRsp (Ipv4Address pred) { m_messageType = STABILIZE_RSP; m_message.stabilizeRsp.responsePredessor = pred; }
PennChordMessage::NotifyMsg PennChordMessage::GetNotifyMsg () { return m_message.notifyMsg; }
void PennChordMessage::SetNotifyMsg (Ipv4Address potPred) { m_messageType = NOTIFY_MSG; m_message.notifyMsg.potentialPredessor = potPred; }

// NEW Accessors
PennChordMessage::SetSuccReq PennChordMessage::GetSetSuccReq() { return m_message.setSuccReq; }
void PennChordMessage::SetSetSuccReq(Ipv4Address newSucc) { m_messageType = SET_SUCC_REQ; m_message.setSuccReq.newSuccessor = newSucc; }

void PennChordMessage::SetMessageType (MessageType messageType) { m_messageType = messageType; }
PennChordMessage::MessageType PennChordMessage::GetMessageType () const { return m_messageType; }
void PennChordMessage::SetTransactionId (uint32_t transactionId) { m_transactionId = transactionId; }
uint32_t PennChordMessage::GetTransactionId (void) const { return m_transactionId; }