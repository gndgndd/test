/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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

TypeId 
PennChordMessage::GetTypeId (void)
{
  static TypeId tid = TypeId ("PennChordMessage")
    .SetParent<Header> ()
    .AddConstructor<PennChordMessage> ()
  ;
  return tid;
}

TypeId
PennChordMessage::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

static inline void WriteIpv4 (Buffer::Iterator &i, Ipv4Address a)
{
  uint32_t raw = a.Get ();
  i.WriteHtonU32 (raw);
}
static inline Ipv4Address ReadIpv4 (Buffer::Iterator &i)
{
  return Ipv4Address (i.ReadNtohU32 ());
}

/* ===== Core Header ===== */

uint32_t
PennChordMessage::GetSerializedSize (void) const
{
  uint32_t size = sizeof (uint8_t) + sizeof (uint32_t);
  switch (m_messageType)
    {
      case PING_REQ:   size += m_message.pingReq.GetSerializedSize (); break;
      case PING_RSP:   size += m_message.pingRsp.GetSerializedSize (); break;
      case JOIN_FIND:  size += m_message.joinFind.GetSerializedSize (); break;
      case JOIN_REPLY: size += m_message.joinReply.GetSerializedSize (); break;
      case STAB_REQ:   size += m_message.stabReq.GetSerializedSize (); break;
      case STAB_RSP:   size += m_message.stabRsp.GetSerializedSize (); break;
      case NOTIFY:     size += m_message.notify.GetSerializedSize (); break;
      case RINGSTATE:  size += m_message.ringstate.GetSerializedSize (); break;
      default: NS_ASSERT (false);
    }
  return size;
}

void
PennChordMessage::Print (std::ostream &os) const
{
  os << "\n****PennChordMessage Dump****\n" ;
  os << "messageType: " << (uint32_t)m_messageType << "\n";
  os << "transactionId: " << m_transactionId << "\n";
  os << "PAYLOAD:: \n";
  switch (m_messageType)
    {
      case PING_REQ:   m_message.pingReq.Print (os); break;
      case PING_RSP:   m_message.pingRsp.Print (os); break;
      case JOIN_FIND:  m_message.joinFind.Print (os); break;
      case JOIN_REPLY: m_message.joinReply.Print (os); break;
      case STAB_REQ:   m_message.stabReq.Print (os); break;
      case STAB_RSP:   m_message.stabRsp.Print (os); break;
      case NOTIFY:     m_message.notify.Print (os); break;
      case RINGSTATE:  m_message.ringstate.Print (os); break;
      default: break;
    }
  os << "\n****END OF MESSAGE****\n";
}

void
PennChordMessage::Serialize (Buffer::Iterator start) const
{
  Buffer::Iterator i = start;
  i.WriteU8 (m_messageType);
  i.WriteHtonU32 (m_transactionId);

  switch (m_messageType)
    {
      case PING_REQ:   m_message.pingReq.Serialize (i); break;
      case PING_RSP:   m_message.pingRsp.Serialize (i); break;
      case JOIN_FIND:  m_message.joinFind.Serialize (i); break;
      case JOIN_REPLY: m_message.joinReply.Serialize (i); break;
      case STAB_REQ:   m_message.stabReq.Serialize (i); break;
      case STAB_RSP:   m_message.stabRsp.Serialize (i); break;
      case NOTIFY:     m_message.notify.Serialize (i); break;
      case RINGSTATE:  m_message.ringstate.Serialize (i); break;
      default: NS_ASSERT (false);
    }
}

uint32_t 
PennChordMessage::Deserialize (Buffer::Iterator start)
{
  uint32_t size;
  Buffer::Iterator i = start;
  m_messageType = (MessageType) i.ReadU8 ();
  m_transactionId = i.ReadNtohU32 ();
  size = sizeof (uint8_t) + sizeof (uint32_t);

  switch (m_messageType)
    {
      case PING_REQ:   size += m_message.pingReq.Deserialize (i); break;
      case PING_RSP:   size += m_message.pingRsp.Deserialize (i); break;
      case JOIN_FIND:  size += m_message.joinFind.Deserialize (i); break;
      case JOIN_REPLY: size += m_message.joinReply.Deserialize (i); break;
      case STAB_REQ:   size += m_message.stabReq.Deserialize (i); break;
      case STAB_RSP:   size += m_message.stabRsp.Deserialize (i); break;
      case NOTIFY:     size += m_message.notify.Deserialize (i); break;
      case RINGSTATE:  size += m_message.ringstate.Deserialize (i); break;
      default: NS_ASSERT (false);
    }
  return size;
}

/* ===== PING_REQ ===== */
uint32_t PennChordMessage::PingReq::GetSerializedSize () const { return sizeof(uint16_t) + pingMessage.length(); }
void     PennChordMessage::PingReq::Print (std::ostream &os) const { os << "PingReq:: Message: " << pingMessage << "\n"; }
void     PennChordMessage::PingReq::Serialize (Buffer::Iterator &i) const { i.WriteU16 (pingMessage.length()); i.Write ((uint8_t*)(const_cast<char*>(pingMessage.c_str())), pingMessage.length()); }
uint32_t PennChordMessage::PingReq::Deserialize (Buffer::Iterator &i) { uint16_t l=i.ReadU16(); char* s=(char*)malloc(l); i.Read((uint8_t*)s,l); pingMessage=std::string(s,l); free(s); return GetSerializedSize(); }
void     PennChordMessage::SetPingReq (std::string m) { if (m_messageType==0) m_messageType=PING_REQ; else NS_ASSERT (m_messageType==PING_REQ); m_message.pingReq.pingMessage=m; }
PennChordMessage::PingReq PennChordMessage::GetPingReq () { return m_message.pingReq; }

/* ===== PING_RSP ===== */
uint32_t PennChordMessage::PingRsp::GetSerializedSize () const { return sizeof(uint16_t) + pingMessage.length(); }
void     PennChordMessage::PingRsp::Print (std::ostream &os) const { os << "PingReq:: Message: " << pingMessage << "\n"; }
void     PennChordMessage::PingRsp::Serialize (Buffer::Iterator &i) const { i.WriteU16 (pingMessage.length()); i.Write ((uint8_t*)(const_cast<char*>(pingMessage.c_str())), pingMessage.length()); }
uint32_t PennChordMessage::PingRsp::Deserialize (Buffer::Iterator &i) { uint16_t l=i.ReadU16(); char* s=(char*)malloc(l); i.Read((uint8_t*)s,l); pingMessage=std::string(s,l); free(s); return GetSerializedSize(); }
void     PennChordMessage::SetPingRsp (std::string m) { if (m_messageType==0) m_messageType=PING_RSP; else NS_ASSERT (m_messageType==PING_RSP); m_message.pingRsp.pingMessage=m; }
PennChordMessage::PingRsp PennChordMessage::GetPingRsp () { return m_message.pingRsp; }

/* ===== JOIN_FIND ===== */
uint32_t PennChordMessage::JoinFind::GetSerializedSize () const { return IPV4_ADDRESS_SIZE + sizeof(uint32_t) + IPV4_ADDRESS_SIZE; }
void     PennChordMessage::JoinFind::Print (std::ostream &os) const { os << "JoinFind:: joiner=" << joiner << " hash=" << joinerHash << " origin=" << origin << "\n"; }
void     PennChordMessage::JoinFind::Serialize (Buffer::Iterator &i) const { WriteIpv4(i, joiner); i.WriteHtonU32(joinerHash); WriteIpv4(i, origin); }
uint32_t PennChordMessage::JoinFind::Deserialize (Buffer::Iterator &i) { joiner=ReadIpv4(i); joinerHash=i.ReadNtohU32(); origin=ReadIpv4(i); return GetSerializedSize(); }
void     PennChordMessage::SetJoinFind (Ipv4Address joiner, uint32_t joinerHash, Ipv4Address origin) { if (m_messageType==0) m_messageType=JOIN_FIND; else NS_ASSERT(m_messageType==JOIN_FIND); m_message.joinFind.joiner=joiner; m_message.joinFind.joinerHash=joinerHash; m_message.joinFind.origin=origin; }
PennChordMessage::JoinFind PennChordMessage::GetJoinFind () { return m_message.joinFind; }

/* ===== JOIN_REPLY ===== */
uint32_t PennChordMessage::JoinReply::GetSerializedSize () const { return IPV4_ADDRESS_SIZE; }
void     PennChordMessage::JoinReply::Print (std::ostream &os) const { os << "JoinReply:: successor=" << successor << "\n"; }
void     PennChordMessage::JoinReply::Serialize (Buffer::Iterator &i) const { WriteIpv4(i, successor); }
uint32_t PennChordMessage::JoinReply::Deserialize (Buffer::Iterator &i) { successor=ReadIpv4(i); return GetSerializedSize(); }
void     PennChordMessage::SetJoinReply (Ipv4Address successor) { if (m_messageType==0) m_messageType=JOIN_REPLY; else NS_ASSERT(m_messageType==JOIN_REPLY); m_message.joinReply.successor=successor; }
PennChordMessage::JoinReply PennChordMessage::GetJoinReply () { return m_message.joinReply; }

/* ===== STAB_REQ ===== */
uint32_t PennChordMessage::StabilizeReq::GetSerializedSize () const { return 0; }
void     PennChordMessage::StabilizeReq::Print (std::ostream &os) const { os << "StabilizeReq\n"; }
void     PennChordMessage::StabilizeReq::Serialize (Buffer::Iterator &i) const { (void)i; }
uint32_t PennChordMessage::StabilizeReq::Deserialize (Buffer::Iterator &i) { (void)i; return 0; }
void     PennChordMessage::SetStabilizeReq () { if (m_messageType==0) m_messageType=STAB_REQ; else NS_ASSERT(m_messageType==STAB_REQ); }
PennChordMessage::StabilizeReq PennChordMessage::GetStabilizeReq () { return m_message.stabReq; }

/* ===== STAB_RSP ===== */
uint32_t PennChordMessage::StabilizeRsp::GetSerializedSize () const { return IPV4_ADDRESS_SIZE; }
void     PennChordMessage::StabilizeRsp::Print (std::ostream &os) const { os << "StabilizeRsp:: predecessor=" << predecessor << "\n"; }
void     PennChordMessage::StabilizeRsp::Serialize (Buffer::Iterator &i) const { WriteIpv4(i, predecessor); }
uint32_t PennChordMessage::StabilizeRsp::Deserialize (Buffer::Iterator &i) { predecessor=ReadIpv4(i); return GetSerializedSize(); }
void     PennChordMessage::SetStabilizeRsp (Ipv4Address predecessor) { if (m_messageType==0) m_messageType=STAB_RSP; else NS_ASSERT(m_messageType==STAB_RSP); m_message.stabRsp.predecessor=predecessor; }
PennChordMessage::StabilizeRsp PennChordMessage::GetStabilizeRsp () { return m_message.stabRsp; }

/* ===== NOTIFY ===== */
uint32_t PennChordMessage::NotifyMsg::GetSerializedSize () const { return IPV4_ADDRESS_SIZE; }
void     PennChordMessage::NotifyMsg::Print (std::ostream &os) const { os << "Notify:: candidate=" << candidate << "\n"; }
void     PennChordMessage::NotifyMsg::Serialize (Buffer::Iterator &i) const { WriteIpv4(i, candidate); }
uint32_t PennChordMessage::NotifyMsg::Deserialize (Buffer::Iterator &i) { candidate=ReadIpv4(i); return GetSerializedSize(); }
void     PennChordMessage::SetNotify (Ipv4Address candidate) { if (m_messageType==0) m_messageType=NOTIFY; else NS_ASSERT(m_messageType==NOTIFY); m_message.notify.candidate=candidate; }
PennChordMessage::NotifyMsg PennChordMessage::GetNotify () { return m_message.notify; }

/* ===== RINGSTATE ===== */
uint32_t PennChordMessage::RingStateMsg::GetSerializedSize () const { return IPV4_ADDRESS_SIZE; }
void     PennChordMessage::RingStateMsg::Print (std::ostream &os) const { os << "RingState:: origin=" << origin << "\n"; }
void     PennChordMessage::RingStateMsg::Serialize (Buffer::Iterator &i) const { WriteIpv4(i, origin); }
uint32_t PennChordMessage::RingStateMsg::Deserialize (Buffer::Iterator &i) { origin=ReadIpv4(i); return GetSerializedSize(); }
void     PennChordMessage::SetRingState (Ipv4Address origin) { if (m_messageType==0) m_messageType=RINGSTATE; else NS_ASSERT(m_messageType==RINGSTATE); m_message.ringstate.origin=origin; }
PennChordMessage::RingStateMsg PennChordMessage::GetRingState () { return m_message.ringstate; }

/* ===== Core setters ===== */
void PennChordMessage::SetMessageType (MessageType t) { m_messageType = t; }
PennChordMessage::MessageType PennChordMessage::GetMessageType () const { return m_messageType; }
void PennChordMessage::SetTransactionId (uint32_t id) { m_transactionId = id; }
uint32_t PennChordMessage::GetTransactionId () const { return m_transactionId; }
