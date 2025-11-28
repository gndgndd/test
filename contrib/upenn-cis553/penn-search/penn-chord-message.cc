/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/penn-chord-message.h"
#include "ns3/log.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("PennChordMessage");
NS_OBJECT_ENSURE_REGISTERED (PennChordMessage);

PennChordMessage::PennChordMessage () { }
PennChordMessage::~PennChordMessage () { }

PennChordMessage::PennChordMessage (PennChordMessage::MessageType messageType, uint32_t transactionId)
{
  m_messageType = messageType;
  m_transactionId = transactionId;
}

TypeId PennChordMessage::GetTypeId (void)
{
  static TypeId tid = TypeId ("PennChordMessage")
    .SetParent<Header> ()
    .AddConstructor<PennChordMessage> ();
  return tid;
}

TypeId PennChordMessage::GetInstanceTypeId (void) const { return GetTypeId (); }

uint32_t PennChordMessage::GetSerializedSize (void) const
{
  uint32_t size = sizeof (uint8_t) + sizeof (uint32_t);
  switch (m_messageType)
    {
      case PING_REQ: size += m_message.pingReq.GetSerializedSize (); break;
      case PING_RSP: size += m_message.pingRsp.GetSerializedSize (); break;
      case LOOKUP_REQ: size += m_message.lookupReq.GetSerializedSize (); break;
      case LOOKUP_FORWARD: size += m_message.lookupForward.GetSerializedSize (); break;
      case LOOKUP_RSP: size += m_message.lookupRsp.GetSerializedSize (); break;
      // MS2
      case GET_PREDECESSOR_REQ: size += m_message.getPredReq.GetSerializedSize (); break;
      case GET_PREDECESSOR_RSP: size += m_message.getPredRsp.GetSerializedSize (); break;
      case NOTIFY_REQ: size += m_message.notifyReq.GetSerializedSize (); break;
      case RINGSTATE_REQ: size += m_message.ringStateReq.GetSerializedSize (); break;
      default: NS_ASSERT (false);
    }
  return size;
}

void PennChordMessage::Print (std::ostream &os) const
{
  os << "\n****PennChordMessage Dump****\n";
  os << "messageType: " << m_messageType << "\n";
  os << "transactionId: " << m_transactionId << "\nPayload:\n";

  switch (m_messageType)
    {
      case PING_REQ: m_message.pingReq.Print (os); break;
      case PING_RSP: m_message.pingRsp.Print (os); break;
      case LOOKUP_REQ: m_message.lookupReq.Print (os); break;
      case LOOKUP_FORWARD: m_message.lookupForward.Print (os); break;
      case LOOKUP_RSP: m_message.lookupRsp.Print (os); break;
      // MS2
      case GET_PREDECESSOR_REQ: m_message.getPredReq.Print (os); break;
      case GET_PREDECESSOR_RSP: m_message.getPredRsp.Print (os); break;
      case NOTIFY_REQ: m_message.notifyReq.Print (os); break;
      case RINGSTATE_REQ: m_message.ringStateReq.Print (os); break;
      default: break;
    }
  os << "\n****END OF MESSAGE****\n";
}

void PennChordMessage::Serialize (Buffer::Iterator start) const
{
  Buffer::Iterator i = start;
  i.WriteU8 (m_messageType);
  i.WriteHtonU32 (m_transactionId);

  switch (m_messageType)
    {
      case PING_REQ: m_message.pingReq.Serialize (i); break;
      case PING_RSP: m_message.pingRsp.Serialize (i); break;
      case LOOKUP_REQ: m_message.lookupReq.Serialize (i); break;
      case LOOKUP_FORWARD: m_message.lookupForward.Serialize (i); break;
      case LOOKUP_RSP: m_message.lookupRsp.Serialize (i); break;
      // MS2
      case GET_PREDECESSOR_REQ: m_message.getPredReq.Serialize (i); break;
      case GET_PREDECESSOR_RSP: m_message.getPredRsp.Serialize (i); break;
      case NOTIFY_REQ: m_message.notifyReq.Serialize (i); break;
      case RINGSTATE_REQ: m_message.ringStateReq.Serialize (i); break;
      default: NS_ASSERT (false);
    }
}

uint32_t PennChordMessage::Deserialize (Buffer::Iterator start)
{
  Buffer::Iterator i = start;
  m_messageType = (MessageType) i.ReadU8 ();
  m_transactionId = i.ReadNtohU32 ();
  uint32_t size = sizeof (uint8_t) + sizeof (uint32_t);

  switch (m_messageType)
    {
      case PING_REQ: size += m_message.pingReq.Deserialize (i); break;
      case PING_RSP: size += m_message.pingRsp.Deserialize (i); break;
      case LOOKUP_REQ: size += m_message.lookupReq.Deserialize (i); break;
      case LOOKUP_FORWARD: size += m_message.lookupForward.Deserialize (i); break;
      case LOOKUP_RSP: size += m_message.lookupRsp.Deserialize (i); break;
      // MS2
      case GET_PREDECESSOR_REQ: size += m_message.getPredReq.Deserialize (i); break;
      case GET_PREDECESSOR_RSP: size += m_message.getPredRsp.Deserialize (i); break;
      case NOTIFY_REQ: size += m_message.notifyReq.Deserialize (i); break;
      case RINGSTATE_REQ: size += m_message.ringStateReq.Deserialize (i); break;
      default: NS_ASSERT (false);
    }
  return size;
}

// --- Payload Implementations ---

// PING / LOOKUP existing implementations assumed handled by copy-paste or macro
// For brevity, here are the NEW ones

// GET_PREDECESSOR_REQ
uint32_t PennChordMessage::GetPredecessorReq::GetSerializedSize() const { return 0; }
void PennChordMessage::GetPredecessorReq::Print(std::ostream &os) const { os << "GetPredReq"; }
void PennChordMessage::GetPredecessorReq::Serialize(Buffer::Iterator &start) const { }
uint32_t PennChordMessage::GetPredecessorReq::Deserialize(Buffer::Iterator &start) { return 0; }

// GET_PREDECESSOR_RSP
uint32_t PennChordMessage::GetPredecessorRsp::GetSerializedSize() const { return IPV4_ADDRESS_SIZE; }
void PennChordMessage::GetPredecessorRsp::Print(std::ostream &os) const { os << "GetPredRsp: " << predecessor; }
void PennChordMessage::GetPredecessorRsp::Serialize(Buffer::Iterator &start) const { start.WriteHtonU32(predecessor.Get()); }
uint32_t PennChordMessage::GetPredecessorRsp::Deserialize(Buffer::Iterator &start) { predecessor = Ipv4Address(start.ReadNtohU32()); return GetSerializedSize(); }

// NOTIFY_REQ
uint32_t PennChordMessage::NotifyReq::GetSerializedSize() const { return IPV4_ADDRESS_SIZE; }
void PennChordMessage::NotifyReq::Print(std::ostream &os) const { os << "NotifyReq: " << potentialPredecessor; }
void PennChordMessage::NotifyReq::Serialize(Buffer::Iterator &start) const { start.WriteHtonU32(potentialPredecessor.Get()); }
uint32_t PennChordMessage::NotifyReq::Deserialize(Buffer::Iterator &start) { potentialPredecessor = Ipv4Address(start.ReadNtohU32()); return GetSerializedSize(); }

// RINGSTATE_REQ
uint32_t PennChordMessage::RingStateReq::GetSerializedSize() const { return IPV4_ADDRESS_SIZE; }
void PennChordMessage::RingStateReq::Print(std::ostream &os) const { os << "RingStateReq: initiator=" << initiator; }
void PennChordMessage::RingStateReq::Serialize(Buffer::Iterator &start) const { start.WriteHtonU32(initiator.Get()); }
uint32_t PennChordMessage::RingStateReq::Deserialize(Buffer::Iterator &start) { initiator = Ipv4Address(start.ReadNtohU32()); return GetSerializedSize(); }


// --- Existing PING/LOOKUP Implementations (Must be included) ---
// (Copying minimal version for completion context - user should keep their existing logic here)
uint32_t PennChordMessage::PingReq::GetSerializedSize() const { return sizeof(uint16_t) + pingMessage.length(); }
void PennChordMessage::PingReq::Print(std::ostream &os) const { os << pingMessage; }
void PennChordMessage::PingReq::Serialize(Buffer::Iterator &start) const { start.WriteU16(pingMessage.length()); start.Write((uint8_t*)pingMessage.c_str(), pingMessage.length()); }
uint32_t PennChordMessage::PingReq::Deserialize(Buffer::Iterator &start) { uint16_t len=start.ReadU16(); char* b=(char*)malloc(len); start.Read((uint8_t*)b,len); pingMessage=std::string(b,len); free(b); return GetSerializedSize(); }

// PING RSP (Same as req)
uint32_t PennChordMessage::PingRsp::GetSerializedSize() const { return sizeof(uint16_t) + pingMessage.length(); }
void PennChordMessage::PingRsp::Print(std::ostream &os) const { os << pingMessage; }
void PennChordMessage::PingRsp::Serialize(Buffer::Iterator &start) const { start.WriteU16(pingMessage.length()); start.Write((uint8_t*)pingMessage.c_str(), pingMessage.length()); }
uint32_t PennChordMessage::PingRsp::Deserialize(Buffer::Iterator &start) { uint16_t len=start.ReadU16(); char* b=(char*)malloc(len); start.Read((uint8_t*)b,len); pingMessage=std::string(b,len); free(b); return GetSerializedSize(); }

// LOOKUP REQ
uint32_t PennChordMessage::LookupReq::GetSerializedSize() const { return 4+4+4; }
void PennChordMessage::LookupReq::Print(std::ostream &os) const { os << lookupKey; }
void PennChordMessage::LookupReq::Serialize(Buffer::Iterator &i) const { i.WriteHtonU32(lookupKey); i.WriteHtonU32(originator.Get()); i.WriteHtonU32(lastHop.Get()); }
uint32_t PennChordMessage::LookupReq::Deserialize(Buffer::Iterator &i) { lookupKey=i.ReadNtohU32(); originator=Ipv4Address(i.ReadNtohU32()); lastHop=Ipv4Address(i.ReadNtohU32()); return GetSerializedSize(); }

// LOOKUP FWD
uint32_t PennChordMessage::LookupForward::GetSerializedSize() const { return 4+4+4; }
void PennChordMessage::LookupForward::Print(std::ostream &os) const { os << lookupKey; }
void PennChordMessage::LookupForward::Serialize(Buffer::Iterator &i) const { i.WriteHtonU32(lookupKey); i.WriteHtonU32(originator.Get()); i.WriteHtonU32(lastHop.Get()); }
uint32_t PennChordMessage::LookupForward::Deserialize(Buffer::Iterator &i) { lookupKey=i.ReadNtohU32(); originator=Ipv4Address(i.ReadNtohU32()); lastHop=Ipv4Address(i.ReadNtohU32()); return GetSerializedSize(); }

// LOOKUP RSP
uint32_t PennChordMessage::LookupRsp::GetSerializedSize() const { return 4+4; }
void PennChordMessage::LookupRsp::Print(std::ostream &os) const { os << lookupKey; }
void PennChordMessage::LookupRsp::Serialize(Buffer::Iterator &i) const { i.WriteHtonU32(lookupKey); i.WriteHtonU32(ownerNode.Get()); }
uint32_t PennChordMessage::LookupRsp::Deserialize(Buffer::Iterator &i) { lookupKey=i.ReadNtohU32(); ownerNode=Ipv4Address(i.ReadNtohU32()); return GetSerializedSize(); }


// --- Setters/Getters ---
void PennChordMessage::SetMessageType (MessageType messageType) { m_messageType = messageType; }
PennChordMessage::MessageType PennChordMessage::GetMessageType () const { return m_messageType; }
void PennChordMessage::SetTransactionId (uint32_t transactionId) { m_transactionId = transactionId; }
uint32_t PennChordMessage::GetTransactionId (void) const { return m_transactionId; }

PennChordMessage::PingReq PennChordMessage::GetPingReq () { return m_message.pingReq; }
void PennChordMessage::SetPingReq (std::string message) { m_messageType = PING_REQ; m_message.pingReq.pingMessage = message; }
PennChordMessage::PingRsp PennChordMessage::GetPingRsp () { return m_message.pingRsp; }
void PennChordMessage::SetPingRsp (std::string message) { m_messageType = PING_RSP; m_message.pingRsp.pingMessage = message; }

PennChordMessage::LookupReq PennChordMessage::GetLookupReq () { return m_message.lookupReq; }
void PennChordMessage::SetLookupReq (uint32_t key, Ipv4Address origin, Ipv4Address lastHop) {
    m_messageType = LOOKUP_REQ; m_message.lookupReq.lookupKey = key; m_message.lookupReq.originator = origin; m_message.lookupReq.lastHop = lastHop;
}
PennChordMessage::LookupForward PennChordMessage::GetLookupForward () { return m_message.lookupForward; }
void PennChordMessage::SetLookupForward (uint32_t key, Ipv4Address origin, Ipv4Address lastHop) {
    m_messageType = LOOKUP_FORWARD; m_message.lookupForward.lookupKey = key; m_message.lookupForward.originator = origin; m_message.lookupForward.lastHop = lastHop;
}
PennChordMessage::LookupRsp PennChordMessage::GetLookupRsp () { return m_message.lookupRsp; }
void PennChordMessage::SetLookupRsp (uint32_t key, Ipv4Address owner) {
    m_messageType = LOOKUP_RSP; m_message.lookupRsp.lookupKey = key; m_message.lookupRsp.ownerNode = owner;
}

// MS2 Setters/Getters
PennChordMessage::GetPredecessorReq PennChordMessage::GetGetPredecessorReq() { return m_message.getPredReq; }
void PennChordMessage::SetGetPredecessorReq() { m_messageType = GET_PREDECESSOR_REQ; }

PennChordMessage::GetPredecessorRsp PennChordMessage::GetGetPredecessorRsp() { return m_message.getPredRsp; }
void PennChordMessage::SetGetPredecessorRsp(Ipv4Address pred) { m_messageType = GET_PREDECESSOR_RSP; m_message.getPredRsp.predecessor = pred; }

PennChordMessage::NotifyReq PennChordMessage::GetNotifyReq() { return m_message.notifyReq; }
void PennChordMessage::SetNotifyReq(Ipv4Address potentialPred) { m_messageType = NOTIFY_REQ; m_message.notifyReq.potentialPredecessor = potentialPred; }

PennChordMessage::RingStateReq PennChordMessage::GetRingStateReq() { return m_message.ringStateReq; }
void PennChordMessage::SetRingStateReq(Ipv4Address initiator) { m_messageType = RINGSTATE_REQ; m_message.ringStateReq.initiator = initiator; }