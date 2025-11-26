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
  m_isLookup = false;
}

void
PennChordMessage::SetIsLookup (bool flag)
{
  m_isLookup = flag;
}

bool
PennChordMessage::GetIsLookup () const
{
  return m_isLookup;
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
  // size of messageType, transaction id, isLookup bool
  uint32_t size = sizeof (uint8_t) + sizeof (uint32_t) + sizeof (uint8_t);
  switch (m_messageType)
    {
      case PING_REQ:
        size += m_message.pingReq.GetSerializedSize ();
        break;
      case PING_RSP:
        size += m_message.pingRsp.GetSerializedSize ();
        break;
      case FIND_SUCCESSOR_REQ:
        size += m_message.findSuccessorReq.GetSerializedSize ();
        break;
      case FIND_SUCCESSOR_RSP:
        size += m_message.findSuccessorRsp.GetSerializedSize ();
        break;
      case STABILIZE_REQ:
        size += m_message.stabilizeReq.GetSerializedSize ();
        break;
      case STABILIZE_RSP:
        size += m_message.stabilizeRsp.GetSerializedSize ();
        break;
      case NOTIFY_PKT:
        size += m_message.notifyPkt.GetSerializedSize();
        break;
      case RINGSTATE_PKT:
        size += m_message.ringStatePkt.GetSerializedSize();
        break;
      case LEAVE_SUCCESSOR:
        size += m_message.leaveSuccessor.GetSerializedSize();
        break;
      case LEAVE_PREDECESSOR:
        size += m_message.leavePrededecessor.GetSerializedSize();
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
      case FIND_SUCCESSOR_REQ:
        m_message.findSuccessorReq.Print (os);
        break;
      case FIND_SUCCESSOR_RSP:
        m_message.findSuccessorRsp.Print (os);
        break;
      case STABILIZE_REQ:
        m_message.stabilizeReq.Print (os);
        break;
      case STABILIZE_RSP:
        m_message.stabilizeRsp.Print (os);
        break;
      case NOTIFY_PKT:
        m_message.notifyPkt.Print (os);
        break;
      case RINGSTATE_PKT:
        m_message.ringStatePkt.Print (os);
        break;
      case LEAVE_SUCCESSOR:
        m_message.leaveSuccessor.Print (os);
        break;
      case LEAVE_PREDECESSOR:
        m_message.leavePrededecessor.Print (os);
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
  i.WriteU8 (m_isLookup ? 1 : 0); // serialize 1 for true, 0 for false

  switch (m_messageType)
    {
      case PING_REQ:
        m_message.pingReq.Serialize (i);
        break;
      case PING_RSP:
        m_message.pingRsp.Serialize (i);
        break;
      case FIND_SUCCESSOR_REQ:
        m_message.findSuccessorReq.Serialize (i);
        break;
      case FIND_SUCCESSOR_RSP:
        m_message.findSuccessorRsp.Serialize (i);
        break;
      case STABILIZE_REQ: 
        m_message.stabilizeReq.Serialize (i);
        break;
      case STABILIZE_RSP: 
        m_message.stabilizeRsp.Serialize (i);
        break;
      case NOTIFY_PKT:
        m_message.notifyPkt.Serialize (i);
        break;
      case RINGSTATE_PKT:
        m_message.ringStatePkt.Serialize (i);
        break;
      case LEAVE_SUCCESSOR:
        m_message.leaveSuccessor.Serialize (i);
        break;
      case LEAVE_PREDECESSOR:
        m_message.leavePrededecessor.Serialize (i);
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
  m_isLookup = i.ReadU8() == 1; // deserialize 1 for true, 0 for false

  size = sizeof (uint8_t) + sizeof (uint32_t) + sizeof (uint8_t);

  switch (m_messageType)
    {
      case PING_REQ:
        size += m_message.pingReq.Deserialize (i);
        break;
      case PING_RSP:
        size += m_message.pingRsp.Deserialize (i);
        break;
      case FIND_SUCCESSOR_REQ:
        size += m_message.findSuccessorReq.Deserialize (i);
        break;
      case FIND_SUCCESSOR_RSP:
        size += m_message.findSuccessorRsp.Deserialize (i);
        break;
      case STABILIZE_REQ: 
        size += m_message.stabilizeReq.Deserialize (i);
        break;
      case STABILIZE_RSP:
        size += m_message.stabilizeRsp.Deserialize (i);
        break;
      case NOTIFY_PKT:
        size += m_message.notifyPkt.Deserialize (i);
        break;
      case RINGSTATE_PKT:
        size += m_message.ringStatePkt.Deserialize (i);
        break;
      case LEAVE_SUCCESSOR:
        size += m_message.leaveSuccessor.Deserialize (i);
        break;
      case LEAVE_PREDECESSOR:
        size += m_message.leavePrededecessor.Deserialize (i);
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

/*FindSuccessorReq*/
uint32_t 
PennChordMessage::FindSuccessorReq::GetSerializedSize (void) const
{
  return sizeof(uint32_t) + IPV4_ADDRESS_SIZE;
}

void
PennChordMessage::FindSuccessorReq::Print (std::ostream &os) const
{
  os << "FindSuccessorReq:: idToFind = " << idToFind << ", requestorIp = " << requestorIp << "\n";
}

void
PennChordMessage::FindSuccessorReq::Serialize (Buffer::Iterator &start) const
{
  start.WriteHtonU32(idToFind);
  uint32_t ip = requestorIp.Get();
  start.WriteHtonU32(ip);
}

uint32_t
PennChordMessage::FindSuccessorReq::Deserialize (Buffer::Iterator &start)
{ 
  // read id to find 
  idToFind = start.ReadNtohU32();
  // read in ip of requestor
  requestorIp = Ipv4Address(start.ReadNtohU32());
  return FindSuccessorReq::GetSerializedSize ();
}

void
PennChordMessage::SetFindSuccessorReq (uint32_t idToFind, Ipv4Address requestorIp)
{
  if (m_messageType == 0)
    {
      m_messageType = FIND_SUCCESSOR_REQ;
    }
  else
    {
      NS_ASSERT (m_messageType == FIND_SUCCESSOR_REQ);
    }
  m_message.findSuccessorReq.idToFind = idToFind;
  m_message.findSuccessorReq.requestorIp = requestorIp;
}

PennChordMessage::FindSuccessorReq
PennChordMessage::GetFindSuccessorReq ()
{
  return m_message.findSuccessorReq;
}


/*FindSuccessorRsp*/
uint32_t PennChordMessage::FindSuccessorRsp::GetSerializedSize() const {
  return IPV4_ADDRESS_SIZE;
}

void PennChordMessage::FindSuccessorRsp::Print(std::ostream &os) const {
  os << "FindSuccessorRsp:: successorIp = " << successorIp;
}

void PennChordMessage::FindSuccessorRsp::Serialize(Buffer::Iterator &start) const {
  uint32_t ip = successorIp.Get();
  start.WriteHtonU32(ip);
}

uint32_t PennChordMessage::FindSuccessorRsp::Deserialize(Buffer::Iterator &start) {
  successorIp = Ipv4Address(start.ReadNtohU32());
  return GetSerializedSize();
}

void
PennChordMessage::SetFindSuccessorRsp (Ipv4Address succesorIp)
{
  if (m_messageType == 0)
    {
      m_messageType = FIND_SUCCESSOR_RSP;
    }
  else
    {
      NS_ASSERT (m_messageType == FIND_SUCCESSOR_RSP);
    }
  m_message.findSuccessorRsp.successorIp = succesorIp;
}

PennChordMessage::FindSuccessorRsp
PennChordMessage::GetFindSuccessorRsp ()
{
  return m_message.findSuccessorRsp;
}


/*StabilzeReq*/
uint32_t 
PennChordMessage::StabilizeReq::GetSerializedSize (void) const
{
  return IPV4_ADDRESS_SIZE + IPV4_ADDRESS_SIZE;
}

void
PennChordMessage::StabilizeReq::Print (std::ostream &os) const
{
  os << "StabilizeReq: sender = " << sender << ", receiver = " << receiver << "\n";
}

void
PennChordMessage::StabilizeReq::Serialize (Buffer::Iterator &start) const
{
  uint32_t senderIp = sender.Get();
  start.WriteHtonU32(senderIp);
  uint32_t receiverIp = receiver.Get();
  start.WriteHtonU32(receiverIp);
}

uint32_t
PennChordMessage::StabilizeReq::Deserialize (Buffer::Iterator &start)
{ 
  sender = Ipv4Address(start.ReadNtohU32());
  receiver = Ipv4Address(start.ReadNtohU32());
  return StabilizeReq::GetSerializedSize ();
}

void
PennChordMessage::SetStabilizeReq (Ipv4Address sender, Ipv4Address receiver)
{
  if (m_messageType == 0)
    {
      m_messageType = STABILIZE_REQ;
    }
  else
    {
      NS_ASSERT (m_messageType == STABILIZE_REQ);
    }
  m_message.stabilizeReq.sender = sender;
  m_message.stabilizeReq.receiver = receiver;
}

PennChordMessage::StabilizeReq
PennChordMessage::GetStabilizeReq ()
{
  return m_message.stabilizeReq;
}

/*StabilzeRsp*/
uint32_t 
PennChordMessage::StabilizeRsp::GetSerializedSize (void) const
{
  return IPV4_ADDRESS_SIZE;
}

void
PennChordMessage::StabilizeRsp::Print (std::ostream &os) const
{
  os << "StabilizeReq: sender = " << sender << "\n";
}

void
PennChordMessage::StabilizeRsp::Serialize (Buffer::Iterator &start) const
{
  uint32_t senderIp = sender.Get();
  start.WriteHtonU32(senderIp);
}

uint32_t
PennChordMessage::StabilizeRsp::Deserialize (Buffer::Iterator &start)
{ 
  sender = Ipv4Address(start.ReadNtohU32());
  return StabilizeRsp::GetSerializedSize ();
}

void
PennChordMessage::SetStabilizeRsp (Ipv4Address sender)
{
  if (m_messageType == 0)
    {
      m_messageType = STABILIZE_RSP;
    }
  else
    {
      NS_ASSERT (m_messageType == STABILIZE_RSP);
    }
  m_message.stabilizeRsp.sender = sender;
}

PennChordMessage::StabilizeRsp
PennChordMessage::GetStabilizeRsp ()
{
  return m_message.stabilizeRsp;
}


/*NotifyPkt*/
uint32_t 
PennChordMessage::NotifyPkt::GetSerializedSize (void) const
{
  return IPV4_ADDRESS_SIZE;
}

void
PennChordMessage::NotifyPkt::Print (std::ostream &os) const
{
  os << "NotifyPkt: new predecessor = " << newPredecessor << "\n";
}

void
PennChordMessage::NotifyPkt::Serialize (Buffer::Iterator &start) const
{
  uint32_t predecessor = newPredecessor.Get();
  start.WriteHtonU32(predecessor);
}

uint32_t
PennChordMessage::NotifyPkt::Deserialize (Buffer::Iterator &start)
{ 
  newPredecessor = Ipv4Address(start.ReadNtohU32());
  return NotifyPkt::GetSerializedSize ();
}

void
PennChordMessage::SetNotifyPkt (Ipv4Address newPredecessor)
{
  if (m_messageType == 0)
    {
      m_messageType = NOTIFY_PKT;
    }
  else
    {
      NS_ASSERT (m_messageType == NOTIFY_PKT);
    }
  m_message.notifyPkt.newPredecessor = newPredecessor;
}

PennChordMessage::NotifyPkt
PennChordMessage::GetNotifyPkt ()
{
  return m_message.notifyPkt;
}

/*RingstatePkt*/
uint32_t 
PennChordMessage::RingstatePkt::GetSerializedSize (void) const
{
  return IPV4_ADDRESS_SIZE;
}

void
PennChordMessage::RingstatePkt::Print (std::ostream &os) const
{
  os << "RingstatePkt: endRingState =  " << endRingState << "\n";
}

void
PennChordMessage::RingstatePkt::Serialize (Buffer::Iterator &start) const
{
  
  uint32_t endingNode = endRingState.Get();
  start.WriteHtonU32(endingNode);
}

uint32_t
PennChordMessage::RingstatePkt::Deserialize (Buffer::Iterator &start)
{ 
  endRingState = Ipv4Address(start.ReadNtohU32());
  return RingstatePkt::GetSerializedSize ();
}

void
PennChordMessage::SetRingstatePkt (Ipv4Address endRingState)
{
  if (m_messageType == 0)
    {
      m_messageType = RINGSTATE_PKT;
    }
  else
    {
      NS_ASSERT (m_messageType == RINGSTATE_PKT);
    }
  m_message.ringStatePkt.endRingState = endRingState;
}

PennChordMessage::RingstatePkt
PennChordMessage::GetRingstatePkt ()
{
  return m_message.ringStatePkt;
}

/*LeaveSuccessor*/
uint32_t 
PennChordMessage::LeaveSuccessor::GetSerializedSize (void) const
{
  return IPV4_ADDRESS_SIZE + IPV4_ADDRESS_SIZE;
}

void
PennChordMessage::LeaveSuccessor::Print (std::ostream &os) const
{
  os << "LeaveSuccessor updating predecessor of successor to = " <<  newPred << "\n";
}

void
PennChordMessage::LeaveSuccessor::Serialize (Buffer::Iterator &start) const
{
  uint32_t ip = sender.Get();
  start.WriteHtonU32(ip);
  uint32_t newPredIp = newPred.Get();
  start.WriteHtonU32(newPredIp);
}

uint32_t
PennChordMessage::LeaveSuccessor::Deserialize (Buffer::Iterator &start)
{ 
  // read id to find
  sender = Ipv4Address(start.ReadNtohU32());
  // read in ip of requestor
  newPred = Ipv4Address(start.ReadNtohU32());
  return LeaveSuccessor::GetSerializedSize ();
}

void
PennChordMessage::SetLeaveSuccessor(Ipv4Address sender, Ipv4Address newPred)
{
  if (m_messageType == 0)
    {
      m_messageType = LEAVE_SUCCESSOR;
    }
  else
    {
      NS_ASSERT (m_messageType == LEAVE_SUCCESSOR);
    }
  m_message.leaveSuccessor.sender = sender;
  m_message.leaveSuccessor.newPred = newPred;
}

PennChordMessage::LeaveSuccessor
PennChordMessage::GetLeaveSuccessor ()
{
  return m_message.leaveSuccessor;
}

/*LeavePredecessor*/
uint32_t 
PennChordMessage::LeavePredecessor::GetSerializedSize (void) const
{
  return IPV4_ADDRESS_SIZE + IPV4_ADDRESS_SIZE;
}

void
PennChordMessage::LeavePredecessor::Print (std::ostream &os) const
{
  os << "LeaveSuccessor updating successor of predecessor to = " <<  newSucc << "\n";
}

void
PennChordMessage::LeavePredecessor::Serialize (Buffer::Iterator &start) const
{
  uint32_t ip = sender.Get();
  start.WriteHtonU32(ip);
  uint32_t newPredIp = newSucc.Get();
  start.WriteHtonU32(newPredIp);
}

uint32_t
PennChordMessage::LeavePredecessor::Deserialize (Buffer::Iterator &start)
{ 
  // read id to find
  sender = Ipv4Address(start.ReadNtohU32());
  // read in ip of requestor
  newSucc = Ipv4Address(start.ReadNtohU32());
  return LeavePredecessor::GetSerializedSize ();
}

void
PennChordMessage::SetLeavePredecessor(Ipv4Address sender, Ipv4Address newSucc)
{
  if (m_messageType == 0)
    {
      m_messageType = LEAVE_PREDECESSOR;
    }
  else
    {
      NS_ASSERT (m_messageType == LEAVE_PREDECESSOR);
    }
  m_message.leavePrededecessor.sender = sender;
  m_message.leavePrededecessor.newSucc = newSucc;
}

PennChordMessage::LeavePredecessor
PennChordMessage::GetLeavePredecessor ()
{
  return m_message.leavePrededecessor;
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

