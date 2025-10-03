/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/ls-message.h"
#include "ns3/log.h"

#include <vector>
#include <utility>
#include <string>
#include <map>
#include <memory>
#include <cassert>
#include <algorithm>
#include <cstdlib>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LSMessage");
NS_OBJECT_ENSURE_REGISTERED(LSMessage);

// ---------------------------------------------------------------------------
// Boilerplate
// ---------------------------------------------------------------------------
LSMessage::LSMessage() {}

LSMessage::LSMessage(const LSMessage& other)
  : Header(other),
    m_messageType(other.m_messageType),
    m_sequenceNumber(other.m_sequenceNumber),
    m_originatorAddress(other.m_originatorAddress),
    m_ttl(other.m_ttl)
{
  if (other.m_payload) { m_payload = other.m_payload->Clone(); }
}

LSMessage& LSMessage::operator=(const LSMessage& other)
{
  if (this != &other)
  {
    Header::operator=(other);
    m_messageType       = other.m_messageType;
    m_sequenceNumber    = other.m_sequenceNumber;
    m_originatorAddress = other.m_originatorAddress;
    m_ttl               = other.m_ttl;
    if (other.m_payload) { m_payload = other.m_payload->Clone(); } else { m_payload.reset(); }
  }
  return *this;
}

LSMessage::~LSMessage() {}

LSMessage::LSMessage(LSMessage::MessageType t, uint32_t seq, uint8_t ttl, Ipv4Address org)
  : m_messageType(t), m_sequenceNumber(seq), m_originatorAddress(org), m_ttl(ttl) {}

TypeId LSMessage::GetTypeId(void)
{
  static TypeId tid = TypeId("LSMessage").SetParent<Header>().AddConstructor<LSMessage>();
  return tid;
}

TypeId LSMessage::GetInstanceTypeId(void) const { return GetTypeId(); }

// ---------------------------------------------------------------------------
// Header (de)serialization and printing
// ---------------------------------------------------------------------------
uint32_t LSMessage::GetSerializedSize(void) const
{
  uint32_t size = sizeof(uint8_t) + sizeof(uint32_t) + IPV4_ADDRESS_SIZE + sizeof(uint8_t);
  if (m_payload) size += m_payload->GetSerializedSize();
  return size;
}

void LSMessage::Print(std::ostream &os) const
{
  os << "\n== LSMessage ==\n"
     << "type=" << m_messageType << " seq=" << m_sequenceNumber
     << " ttl=" << unsigned(m_ttl) << " origin=" << m_originatorAddress << "\n";
  if (m_payload)
  {
    os << "payload: ";
    m_payload->Print(os);
  }
  os << "\n";
}

void LSMessage::Serialize(Buffer::Iterator start) const
{
  Buffer::Iterator i = start;
  i.WriteU8(m_messageType);
  i.WriteHtonU32(m_sequenceNumber);
  i.WriteU8(m_ttl);
  i.WriteHtonU32(m_originatorAddress.Get());
  if (m_payload) { m_payload->Serialize(i); }
}

uint32_t LSMessage::Deserialize(Buffer::Iterator start)
{
  Buffer::Iterator i = start;
  m_messageType       = (MessageType)i.ReadU8();
  m_sequenceNumber    = i.ReadNtohU32();
  m_ttl               = i.ReadU8();
  m_originatorAddress = Ipv4Address(i.ReadNtohU32());

  uint32_t size = sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint8_t) + IPV4_ADDRESS_SIZE;

  // Instantiate payload
  switch (m_messageType)
  {
    case PING_REQ:  m_payload.reset(new PingReq());  break;
    case PING_RSP:  m_payload.reset(new PingRsp());  break;
    case HELLO_REQ: m_payload.reset(new HelloReq()); break;
    case HELLO_RSP: m_payload.reset(new HelloRsp()); break;
    case LSA_m:     m_payload.reset(new Lsa());      break;
    default: NS_ASSERT_MSG(false, "Unknown message type during deserialization");
  }

  if (m_payload) size += m_payload->Deserialize(i);
  return size;
}

// ---------------------------------------------------------------------------
// Small helpers for string payloads
// ---------------------------------------------------------------------------
static void serializeStringPayload(Buffer::Iterator &start, const Ipv4Address &destAddr, const std::string &message)
{
  start.WriteHtonU32(destAddr.Get());
  start.WriteU16(message.length());
  start.Write(reinterpret_cast<const uint8_t*>(message.data()), message.length());
}

static uint32_t deserializeStringPayload(Buffer::Iterator &start, Ipv4Address &destAddr, std::string &message)
{
  destAddr = Ipv4Address(start.ReadNtohU32());
  uint16_t length = start.ReadU16();
  char *buf = static_cast<char*>(malloc(length + 1));
  start.Read(reinterpret_cast<uint8_t*>(buf), length);
  buf[length] = '\0';
  message.assign(buf, length);
  free(buf);
  return IPV4_ADDRESS_SIZE + sizeof(uint16_t) + length;
}

// ---------------------------------------------------------------------------
// PING_REQ
// ---------------------------------------------------------------------------
std::unique_ptr<LSMessage::MessagePayload> LSMessage::PingReq::Clone() const
{ return std::unique_ptr<MessagePayload>(new PingReq(*this)); }

uint32_t LSMessage::PingReq::GetSerializedSize() const
{ return IPV4_ADDRESS_SIZE + sizeof(uint16_t) + pingMessage.length(); }

void LSMessage::PingReq::Print(std::ostream &os) const
{ os << "PingReq(dest=" << destinationAddress << ", msg='" << pingMessage << "')"; }

void LSMessage::PingReq::Serialize(Buffer::Iterator &start) const
{ serializeStringPayload(start, destinationAddress, pingMessage); }

uint32_t LSMessage::PingReq::Deserialize(Buffer::Iterator &start)
{ return deserializeStringPayload(start, destinationAddress, pingMessage); }

// ---------------------------------------------------------------------------
// PING_RSP
// ---------------------------------------------------------------------------
std::unique_ptr<LSMessage::MessagePayload> LSMessage::PingRsp::Clone() const
{ return std::unique_ptr<MessagePayload>(new PingRsp(*this)); }

uint32_t LSMessage::PingRsp::GetSerializedSize() const
{ return IPV4_ADDRESS_SIZE + sizeof(uint16_t) + pingMessage.length(); }

void LSMessage::PingRsp::Print(std::ostream &os) const
{ os << "PingRsp(dest=" << destinationAddress << ", msg='" << pingMessage << "')"; }

void LSMessage::PingRsp::Serialize(Buffer::Iterator &start) const
{ serializeStringPayload(start, destinationAddress, pingMessage); }

uint32_t LSMessage::PingRsp::Deserialize(Buffer::Iterator &start)
{ return deserializeStringPayload(start, destinationAddress, pingMessage); }

// ---------------------------------------------------------------------------
// HELLO_REQ
// ---------------------------------------------------------------------------
std::unique_ptr<LSMessage::MessagePayload> LSMessage::HelloReq::Clone() const
{ return std::unique_ptr<MessagePayload>(new HelloReq(*this)); }

uint32_t LSMessage::HelloReq::GetSerializedSize() const
{ return IPV4_ADDRESS_SIZE + sizeof(uint16_t) + helloMessage.length(); }

void LSMessage::HelloReq::Print(std::ostream &os) const
{ os << "HelloReq(dest=" << destinationAddress << ", msg='" << helloMessage << "')"; }

void LSMessage::HelloReq::Serialize(Buffer::Iterator &start) const
{ serializeStringPayload(start, destinationAddress, helloMessage); }

uint32_t LSMessage::HelloReq::Deserialize(Buffer::Iterator &start)
{ return deserializeStringPayload(start, destinationAddress, helloMessage); }

// ---------------------------------------------------------------------------
// HELLO_RSP
// ---------------------------------------------------------------------------
std::unique_ptr<LSMessage::MessagePayload> LSMessage::HelloRsp::Clone() const
{ return std::unique_ptr<MessagePayload>(new HelloRsp(*this)); }

uint32_t LSMessage::HelloRsp::GetSerializedSize() const
{ return IPV4_ADDRESS_SIZE + sizeof(uint16_t) + helloMessage.length(); }

void LSMessage::HelloRsp::Print(std::ostream &os) const
{ os << "HelloRsp(dest=" << destinationAddress << ", msg='" << helloMessage << "')"; }

void LSMessage::HelloRsp::Serialize(Buffer::Iterator &start) const
{ serializeStringPayload(start, destinationAddress, helloMessage); }

uint32_t LSMessage::HelloRsp::Deserialize(Buffer::Iterator &start)
{ return deserializeStringPayload(start, destinationAddress, helloMessage); }

// ---------------------------------------------------------------------------
// LSA_m
// ---------------------------------------------------------------------------
std::unique_ptr<LSMessage::MessagePayload> LSMessage::Lsa::Clone() const
{ return std::unique_ptr<MessagePayload>(new Lsa(*this)); }

uint32_t LSMessage::Lsa::GetSerializedSize() const
{
  // count + N*(node,cost)
  return sizeof(uint16_t) + static_cast<uint32_t>(linkVector.size()) * (sizeof(uint32_t) + sizeof(uint32_t));
}

void LSMessage::Lsa::Print(std::ostream &os) const
{
  os << "Lsa(";
  for (const auto &pr : linkVector) { os << "(" << pr.first << ":" << pr.second << ") "; }
  os << ")";
}

void LSMessage::Lsa::Serialize(Buffer::Iterator &start) const
{
  start.WriteU16(static_cast<uint16_t>(linkVector.size()));
  for (const auto &pr : linkVector)
  {
    start.WriteHtonU32(pr.first);
    start.WriteHtonU32(pr.second);
  }
}

uint32_t LSMessage::Lsa::Deserialize(Buffer::Iterator &start)
{
  uint16_t count = start.ReadU16();
  linkVector.clear();
  linkVector.reserve(count);
  for (uint16_t i = 0; i < count; ++i)
  {
    uint32_t nodeNum = start.ReadNtohU32();
    uint32_t cost    = start.ReadNtohU32();
    linkVector.emplace_back(nodeNum, cost);
  }
  return GetSerializedSize();
}

// ---------------------------------------------------------------------------
// Accessors/Mutators
// ---------------------------------------------------------------------------
void LSMessage::SetMessageType(MessageType t) { m_messageType = t; }
LSMessage::MessageType LSMessage::GetMessageType() const { return m_messageType; }
void LSMessage::SetSequenceNumber(uint32_t s) { m_sequenceNumber = s; }
uint32_t LSMessage::GetSequenceNumber(void) const { return m_sequenceNumber; }
void LSMessage::SetTTL(uint8_t ttl) { m_ttl = ttl; }
uint8_t LSMessage::GetTTL(void) const { return m_ttl; }
void LSMessage::SetOriginatorAddress(Ipv4Address a) { m_originatorAddress = a; }
Ipv4Address LSMessage::GetOriginatorAddress(void) const { return m_originatorAddress; }

LSMessage::PingReq LSMessage::GetPingReq() const
{ assert(m_messageType == PING_REQ && m_payload); return *static_cast<PingReq*>(m_payload.get()); }

void LSMessage::SetPingReq(Ipv4Address dest, std::string payload)
{
  m_messageType = PING_REQ;
  m_payload.reset(new PingReq());
  static_cast<PingReq*>(m_payload.get())->destinationAddress = dest;
  static_cast<PingReq*>(m_payload.get())->pingMessage = payload;
}

LSMessage::PingRsp LSMessage::GetPingRsp() const
{ assert(m_messageType == PING_RSP && m_payload); return *static_cast<PingRsp*>(m_payload.get()); }

void LSMessage::SetPingRsp(Ipv4Address dest, std::string payload)
{
  m_messageType = PING_RSP;
  m_payload.reset(new PingRsp());
  static_cast<PingRsp*>(m_payload.get())->destinationAddress = dest;
  static_cast<PingRsp*>(m_payload.get())->pingMessage = payload;
}

LSMessage::HelloReq LSMessage::GetHelloReq() const
{ assert(m_messageType == HELLO_REQ && m_payload); return *static_cast<HelloReq*>(m_payload.get()); }

void LSMessage::SetHelloReq(Ipv4Address dest, std::string msg)
{
  m_messageType = HELLO_REQ;
  m_payload.reset(new HelloReq());
  static_cast<HelloReq*>(m_payload.get())->destinationAddress = dest;
  static_cast<HelloReq*>(m_payload.get())->helloMessage = msg;
}

LSMessage::HelloRsp LSMessage::GetHelloRsp() const
{ assert(m_messageType == HELLO_RSP && m_payload); return *static_cast<HelloRsp*>(m_payload.get()); }

void LSMessage::SetHelloRsp(Ipv4Address dest, std::string msg)
{
  m_messageType = HELLO_RSP;
  m_payload.reset(new HelloRsp());
  static_cast<HelloRsp*>(m_payload.get())->destinationAddress = dest;
  static_cast<HelloRsp*>(m_payload.get())->helloMessage = msg;
}

LSMessage::Lsa LSMessage::GetLsa() const
{ assert(m_messageType == LSA_m && m_payload); return *static_cast<Lsa*>(m_payload.get()); }

void LSMessage::SetLsa(const std::vector<std::pair<uint32_t, uint32_t>>& links)
{
  m_messageType = LSA_m;
  m_payload.reset(new Lsa());
  static_cast<Lsa*>(m_payload.get())->linkVector = links;
}
