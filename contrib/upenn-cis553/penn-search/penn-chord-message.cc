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

PennChordMessage::PennChordMessage ()
{
}

PennChordMessage::~PennChordMessage ()
{
}

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


uint32_t
PennChordMessage::GetSerializedSize (void) const
{
  // size of messageType, transaction id
  uint32_t size = sizeof (uint8_t) + sizeof (uint32_t);
  switch (m_messageType)
    {
      case PING_REQ:
        size += m_message.pingReq.GetSerializedSize ();
        break;
      case PING_RSP:
        size += m_message.pingRsp.GetSerializedSize ();
        break;
      // MS2A Added Lookup Messages
      case LOOKUP_REQ:
        size += m_message.lookupReq.GetSerializedSize ();
        break;
      case LOOKUP_FORWARD:
        size += m_message.lookupForward.GetSerializedSize ();
        break;
      case LOOKUP_RSP:
        size += m_message.lookupRsp.GetSerializedSize ();
        break;
      default:
        NS_ASSERT (false);
    }
  return size;
}

void
PennChordMessage::Print (std::ostream &os) const
{
  os << "\n****PennChordMessage Dump****\n" ;
  os << "messageType: " << m_messageType << "\n";
  os << "transactionId: " << m_transactionId << "\n";
  os << "PAYLOAD:: \n";

  switch (m_messageType)
    {
      case PING_REQ:
        m_message.pingReq.Print (os);
        break;
      case PING_RSP:
        m_message.pingRsp.Print (os);
        break;
      //MS2A LOOKUP MESSAGES
      case LOOKUP_REQ:
        m_message.lookupReq.Print (os);
        break;
      case LOOKUP_FORWARD:
        m_message.lookupForward.Print (os);
        break;
      case LOOKUP_RSP:
        m_message.lookupRsp.Print (os);
        break;
      default:
        break;
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
      case PING_REQ:
        m_message.pingReq.Serialize (i);
        break;
      case PING_RSP:
        m_message.pingRsp.Serialize (i);
        break;
      // MS2A LOOKUP SERIALIZATION
      case LOOKUP_REQ:
        m_message.lookupReq.Serialize (i);
        break;
      case LOOKUP_FORWARD:
        m_message.lookupForward.Serialize (i);
        break;
      case LOOKUP_RSP:
        m_message.lookupRsp.Serialize (i);
        break;
      default:
        NS_ASSERT (false);
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
      case PING_REQ:
        size += m_message.pingReq.Deserialize (i);
        break;
      case PING_RSP:
        size += m_message.pingRsp.Deserialize (i);
        break;
      // MS2A LOOKUP DESERIALIZATION
      case LOOKUP_REQ:
        size += m_message.lookupReq.Deserialize (i);
        break;
      case LOOKUP_FORWARD:
        size += m_message.lookupForward.Deserialize (i);
        break;
      case LOOKUP_RSP:
        size += m_message.lookupRsp.Deserialize (i);
        break;
      default:
        NS_ASSERT (false);
    }
  return size;
}

/* PING_REQ */

uint32_t
PennChordMessage::PingReq::GetSerializedSize (void) const
{
  uint32_t size;
  size = sizeof(uint16_t) + pingMessage.length();
  return size;
}

void
PennChordMessage::PingReq::Print (std::ostream &os) const
{
  os << "PingReq:: Message: " << pingMessage << "\n";
}

void
PennChordMessage::PingReq::Serialize (Buffer::Iterator &start) const
{
  start.WriteU16 (pingMessage.length ());
  start.Write ((uint8_t *) (const_cast<char*> (pingMessage.c_str())), pingMessage.length());
}

uint32_t
PennChordMessage::PingReq::Deserialize (Buffer::Iterator &start)
{
  uint16_t length = start.ReadU16 ();
  char* str = (char*) malloc (length);
  start.Read ((uint8_t*)str, length);
  pingMessage = std::string (str, length);
  free (str);
  return PingReq::GetSerializedSize ();
}

void
PennChordMessage::SetPingReq (std::string pingMessage)
{
  if (m_messageType == 0)
    {
      m_messageType = PING_REQ;
    }
  else
    {
      NS_ASSERT (m_messageType == PING_REQ);
    }
  m_message.pingReq.pingMessage = pingMessage;
}

PennChordMessage::PingReq
PennChordMessage::GetPingReq ()
{
  return m_message.pingReq;
}

/* PING_RSP */

uint32_t
PennChordMessage::PingRsp::GetSerializedSize (void) const
{
  uint32_t size;
  size = sizeof(uint16_t) + pingMessage.length();
  return size;
}

void
PennChordMessage::PingRsp::Print (std::ostream &os) const
{
  os << "PingReq:: Message: " << pingMessage << "\n";
}

void
PennChordMessage::PingRsp::Serialize (Buffer::Iterator &start) const
{
  start.WriteU16 (pingMessage.length ());
  start.Write ((uint8_t *) (const_cast<char*> (pingMessage.c_str())), pingMessage.length());
}

uint32_t
PennChordMessage::PingRsp::Deserialize (Buffer::Iterator &start)
{
  uint16_t length = start.ReadU16 ();
  char* str = (char*) malloc (length);
  start.Read ((uint8_t*)str, length);
  pingMessage = std::string (str, length);
  free (str);
  return PingRsp::GetSerializedSize ();
}

void
PennChordMessage::SetPingRsp (std::string pingMessage)
{
  if (m_messageType == 0)
    {
      m_messageType = PING_RSP;
    }
  else
    {
      NS_ASSERT (m_messageType == PING_RSP);
    }
  m_message.pingRsp.pingMessage = pingMessage;
}

PennChordMessage::PingRsp
PennChordMessage::GetPingRsp ()
{
  return m_message.pingRsp;
}

//MS2A LOOKUP MESSAGES

uint32_t
PennChordMessage::LookupReq::GetSerializedSize () const
{
  return sizeof(uint32_t) + IPV4_ADDRESS_SIZE + IPV4_ADDRESS_SIZE;
}

void
PennChordMessage::LookupReq::Print (std::ostream &os) const
{
  os << "LookupReq:: lookupKey=" << lookupKey
     << " originator=" << originator
     << " lastHop=" << lastHop << "\n";
}

void
PennChordMessage::LookupReq::Serialize (Buffer::Iterator &i) const
{
  i.WriteHtonU32 (lookupKey);
  i.WriteHtonU32 (originator.Get ());
  i.WriteHtonU32 (lastHop.Get ());
}

uint32_t
PennChordMessage::LookupReq::Deserialize (Buffer::Iterator &i)
{
  lookupKey = i.ReadNtohU32 ();
  originator = Ipv4Address(i.ReadNtohU32 ());
  lastHop = Ipv4Address(i.ReadNtohU32 ());
  return GetSerializedSize ();
}

uint32_t
PennChordMessage::LookupForward::GetSerializedSize () const
{
  return sizeof(uint32_t) + IPV4_ADDRESS_SIZE + IPV4_ADDRESS_SIZE;
}

void
PennChordMessage::LookupForward::Print (std::ostream &os) const
{
  os << "LookupForward:: lookupKey=" << lookupKey
     << " originator=" << originator
     << " lastHop=" << lastHop << "\n";
}

void
PennChordMessage::LookupForward::Serialize (Buffer::Iterator &i) const
{
  i.WriteHtonU32 (lookupKey);
  i.WriteHtonU32 (originator.Get ());
  i.WriteHtonU32 (lastHop.Get ());
}

uint32_t
PennChordMessage::LookupForward::Deserialize (Buffer::Iterator &i)
{
  lookupKey = i.ReadNtohU32 ();
  originator = Ipv4Address(i.ReadNtohU32 ());
  lastHop = Ipv4Address(i.ReadNtohU32 ());
  return GetSerializedSize ();
}

uint32_t
PennChordMessage::LookupRsp::GetSerializedSize () const
{
  return sizeof(uint32_t) + IPV4_ADDRESS_SIZE;
}

void
PennChordMessage::LookupRsp::Print (std::ostream &os) const
{
  os << "LookupRsp:: lookupKey=" << lookupKey
     << " ownerNode=" << ownerNode << "\n";
}

void
PennChordMessage::LookupRsp::Serialize (Buffer::Iterator &i) const
{
  i.WriteHtonU32 (lookupKey);
  i.WriteHtonU32 (ownerNode.Get ());
}

uint32_t
PennChordMessage::LookupRsp::Deserialize (Buffer::Iterator &i)
{
  lookupKey = i.ReadNtohU32 ();
  ownerNode = Ipv4Address(i.ReadNtohU32 ());
  return GetSerializedSize ();
}

// Accessors

PennChordMessage::LookupReq
PennChordMessage::GetLookupReq ()
{
  return m_message.lookupReq;
}

void
PennChordMessage::SetLookupReq (uint32_t key, Ipv4Address origin, Ipv4Address lastHop)
{
  m_messageType = LOOKUP_REQ;
  m_message.lookupReq.lookupKey = key;
  m_message.lookupReq.originator = origin;
  m_message.lookupReq.lastHop = lastHop;
}

PennChordMessage::LookupForward
PennChordMessage::GetLookupForward ()
{
  return m_message.lookupForward;
}

void
PennChordMessage::SetLookupForward (uint32_t key, Ipv4Address origin, Ipv4Address lastHop)
{
  m_messageType = LOOKUP_FORWARD;
  m_message.lookupForward.lookupKey = key;
  m_message.lookupForward.originator = origin;
  m_message.lookupForward.lastHop = lastHop;
}

PennChordMessage::LookupRsp
PennChordMessage::GetLookupRsp ()
{
  return m_message.lookupRsp;
}

void
PennChordMessage::SetLookupRsp (uint32_t key, Ipv4Address owner)
{
  m_messageType = LOOKUP_RSP;
  m_message.lookupRsp.lookupKey = key;
  m_message.lookupRsp.ownerNode = owner;
}

void
PennChordMessage::SetMessageType (MessageType messageType)
{
  m_messageType = messageType;
}

PennChordMessage::MessageType
PennChordMessage::GetMessageType () const
{
  return m_messageType;
}

void
PennChordMessage::SetTransactionId (uint32_t transactionId)
{
  m_transactionId = transactionId;
}

uint32_t
PennChordMessage::GetTransactionId (void) const
{
  return m_transactionId;
}

