/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/dv-message.h"
#include "ns3/log.h"
#include "wire-io.hpp"
#include <algorithm>

using namespace ns3;
using namespace wire;

NS_LOG_COMPONENT_DEFINE("DVMessage");
NS_OBJECT_ENSURE_REGISTERED(DVMessage);

DVMessage::DVMessage() = default;
DVMessage::~DVMessage() = default;

DVMessage::DVMessage (DVMessage::MessageType t, uint32_t seq, uint8_t ttl, Ipv4Address origin)
  : m_messageType(t), m_sequenceNumber(seq), m_originatorAddress(origin), m_ttl(ttl) {}

TypeId DVMessage::GetTypeId (void)
{
  static TypeId tid = TypeId("DVMessage").SetParent<Header>().AddConstructor<DVMessage>();
  return tid;
}

TypeId DVMessage::GetInstanceTypeId () const { return GetTypeId(); }

void DVMessage::SetMessageType (MessageType t) { m_messageType = t; }
DVMessage::MessageType DVMessage::GetMessageType () const { return m_messageType; }
void DVMessage::SetSequenceNumber (uint32_t s) { m_sequenceNumber = s; }
uint32_t DVMessage::GetSequenceNumber () const { return m_sequenceNumber; }
void DVMessage::SetOriginatorAddress (Ipv4Address a) { m_originatorAddress = a; }
Ipv4Address DVMessage::GetOriginatorAddress () const { return m_originatorAddress; }
void DVMessage::SetTTL (uint8_t t) { m_ttl = t; }
uint8_t DVMessage::GetTTL () const { return m_ttl; }

uint32_t DVMessage::GetSerializedSize () const
{
  uint32_t sz = sizeof(uint8_t) + sizeof(uint32_t) + IPV4_ADDRESS_SIZE + sizeof(uint8_t);
  switch (m_messageType) {
    case PING_REQ:  sz += m_message.pingReq.GetSerializedSize();  break;
    case PING_RSP:  sz += m_message.pingRsp.GetSerializedSize();  break;
    case HELLO_REQ: sz += m_message.helloReq.GetSerializedSize(); break;
    case HELLO_RSP: sz += m_message.helloRsp.GetSerializedSize(); break;
    case DV_UPDATE: sz += m_message.dvUpdate.GetSerializedSize(); break;
    default: NS_ASSERT(false);
  }
  return sz;
}

void DVMessage::Print (std::ostream &os) const
{
  os << "\n[DVMessage] type=" << m_messageType
     << " seq=" << m_sequenceNumber
     << " ttl=" << unsigned(m_ttl)
     << " origin=" << m_originatorAddress << "\n  payload:\n";
  switch (m_messageType) {
    case PING_REQ:  m_message.pingReq.Print(os);  break;
    case PING_RSP:  m_message.pingRsp.Print(os);  break;
    case HELLO_REQ: m_message.helloReq.Print(os); break;
    case HELLO_RSP: m_message.helloRsp.Print(os); break;
    case DV_UPDATE: m_message.dvUpdate.Print(os); break;
    default: break;
  }
}

void DVMessage::Serialize (Buffer::Iterator i) const
{
  i.WriteU8 (m_messageType);
  i.WriteHtonU32 (m_sequenceNumber);
  i.WriteU8 (m_ttl);
  WriteIpv4(i, m_originatorAddress);

  switch (m_messageType) {
    case PING_REQ:  m_message.pingReq.Serialize(i);  break;
    case PING_RSP:  m_message.pingRsp.Serialize(i);  break;
    case HELLO_REQ: m_message.helloReq.Serialize(i); break;
    case HELLO_RSP: m_message.helloRsp.Serialize(i); break;
    case DV_UPDATE: m_message.dvUpdate.Serialize(i); break;
    default: NS_ASSERT(false);
  }
}

uint32_t DVMessage::Deserialize (Buffer::Iterator i)
{
  m_messageType = static_cast<MessageType>(i.ReadU8());
  m_sequenceNumber = i.ReadNtohU32();
  m_ttl = i.ReadU8();
  m_originatorAddress = ReadIpv4(i);

  uint32_t sz = sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint8_t) + IPV4_ADDRESS_SIZE;
  switch (m_messageType) {
    case PING_REQ:  sz += m_message.pingReq.Deserialize(i);  break;
    case PING_RSP:  sz += m_message.pingRsp.Deserialize(i);  break;
    case HELLO_REQ: sz += m_message.helloReq.Deserialize(i); break;
    case HELLO_RSP: sz += m_message.helloRsp.Deserialize(i); break;
    case DV_UPDATE: sz += m_message.dvUpdate.Deserialize(i); break;
    default: NS_ASSERT(false);
  }
  return sz;
}

/* HelloReq */
uint32_t DVMessage::HelloReq::GetSerializedSize() const { return sizeof(uint16_t) + helloMessage.size(); }
void DVMessage::HelloReq::Print (std::ostream &os) const { os << "    HelloReq msg=\"" << helloMessage << "\"\n"; }
void DVMessage::HelloReq::Serialize (Buffer::Iterator &it) const { WriteU16String(it, helloMessage); }
uint32_t DVMessage::HelloReq::Deserialize (Buffer::Iterator &it) { helloMessage = ReadU16String(it); return GetSerializedSize(); }

/* HelloRsp */
uint32_t DVMessage::HelloRsp::GetSerializedSize () const { return IPV4_ADDRESS_SIZE + sizeof(uint16_t) + helloMessage.size(); }
void DVMessage::HelloRsp::Print (std::ostream &os) const { os << "    HelloRsp from=" << sourceAddress << " msg=\"" << helloMessage << "\"\n"; }
void DVMessage::HelloRsp::Serialize (Buffer::Iterator &it) const { WriteIpv4(it, sourceAddress); WriteU16String(it, helloMessage); }
uint32_t DVMessage::HelloRsp::Deserialize (Buffer::Iterator &it) { sourceAddress = ReadIpv4(it); helloMessage = ReadU16String(it); return GetSerializedSize(); }

/* PingReq */
uint32_t DVMessage::PingReq::GetSerializedSize () const { return IPV4_ADDRESS_SIZE + sizeof(uint16_t) + pingMessage.size(); }
void DVMessage::PingReq::Print (std::ostream &os) const { os << "    PingReq dst=" << destinationAddress << " msg=\"" << pingMessage << "\"\n"; }
void DVMessage::PingReq::Serialize (Buffer::Iterator &it) const { WriteIpv4(it, destinationAddress); WriteU16String(it, pingMessage); }
uint32_t DVMessage::PingReq::Deserialize (Buffer::Iterator &it) { destinationAddress = ReadIpv4(it); pingMessage = ReadU16String(it); return GetSerializedSize(); }

/* PingRsp */
uint32_t DVMessage::PingRsp::GetSerializedSize () const { return IPV4_ADDRESS_SIZE + sizeof(uint16_t) + pingMessage.size(); }
void DVMessage::PingRsp::Print (std::ostream &os) const { os << "    PingRsp dst=" << destinationAddress << " msg=\"" << pingMessage << "\"\n"; }
void DVMessage::PingRsp::Serialize (Buffer::Iterator &it) const { WriteIpv4(it, destinationAddress); WriteU16String(it, pingMessage); }
uint32_t DVMessage::PingRsp::Deserialize (Buffer::Iterator &it) { destinationAddress = ReadIpv4(it); pingMessage = ReadU16String(it); return GetSerializedSize(); }

/* DV update */
void DVMessage::DvUpdate::Print (std::ostream &os) const {
  os << "    DV items=" << vec.size() << "\n";
  for (const auto &e : vec) os << "      dest=" << e.dest << " cost=" << e.cost << "\n";
}
uint32_t DVMessage::DvUpdate::GetSerializedSize () const {
  return sizeof(uint16_t) + vec.size() * (IPV4_ADDRESS_SIZE + sizeof(uint32_t));
}
void DVMessage::DvUpdate::Serialize (Buffer::Iterator &it) const {
  WriteCountU16(it, static_cast<uint16_t>(vec.size()));
  for (const auto &e : vec) { WriteIpv4(it, e.dest); it.WriteHtonU32(e.cost); }
}
uint32_t DVMessage::DvUpdate::Deserialize (Buffer::Iterator &it) {
  uint16_t n = ReadCountU16(it);
  vec.clear(); vec.reserve(n);
  for (uint16_t i=0;i<n;++i) {
    DvVectorItem item;
    item.dest = ReadIpv4(it);
    item.cost = it.ReadNtohU32();
    vec.push_back(item);
  }
  return GetSerializedSize();
}

/* accessors/mutators */
DVMessage::HelloReq DVMessage::GetHelloReq(){ return m_message.helloReq; }
void DVMessage::SetHelloReq(std::string hello){ m_messageType = HELLO_REQ; m_message.helloReq.helloMessage = std::move(hello); }

DVMessage::HelloRsp DVMessage::GetHelloRsp(){ return m_message.helloRsp; }
void DVMessage::SetHelloRsp(Ipv4Address src, std::string hello){ m_messageType = HELLO_RSP; m_message.helloRsp.sourceAddress = src; m_message.helloRsp.helloMessage = std::move(hello); }

DVMessage::PingReq DVMessage::GetPingReq (){ return m_message.pingReq; }
void DVMessage::SetPingReq (Ipv4Address dst, std::string msg){ if(m_messageType==0) m_messageType=PING_REQ; else NS_ASSERT(m_messageType==PING_REQ); m_message.pingReq.destinationAddress=dst; m_message.pingReq.pingMessage=std::move(msg); }

DVMessage::PingRsp DVMessage::GetPingRsp (){ return m_message.pingRsp; }
void DVMessage::SetPingRsp (Ipv4Address dst, std::string msg){ if(m_messageType==0) m_messageType=PING_RSP; else NS_ASSERT(m_messageType==PING_RSP); m_message.pingRsp.destinationAddress=dst; m_message.pingRsp.pingMessage=std::move(msg); }

DVMessage::DvUpdate DVMessage::GetDvUpdate() const { return m_message.dvUpdate; }
void DVMessage::SetDvUpdate(const std::vector<DvVectorItem>& items){ m_messageType = DV_UPDATE; m_message.dvUpdate.vec = items; }
