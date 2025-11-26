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

#include "ns3/penn-search-message.h"
#include "ns3/log.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("PennSearchMessage");
NS_OBJECT_ENSURE_REGISTERED (PennSearchMessage);

PennSearchMessage::PennSearchMessage ()
{
}

PennSearchMessage::~PennSearchMessage ()
{
}

PennSearchMessage::PennSearchMessage (PennSearchMessage::MessageType messageType, uint32_t transactionId)
{
  m_messageType = messageType;
  m_transactionId = transactionId;
}

TypeId 
PennSearchMessage::GetTypeId (void)
{
  static TypeId tid = TypeId ("PennSearchMessage")
    .SetParent<Header> ()
    .AddConstructor<PennSearchMessage> ()
  ;
  return tid;
}

TypeId
PennSearchMessage::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}


uint32_t
PennSearchMessage::GetSerializedSize (void) const
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
      case PUBLISH_REQ:
        size += m_message.publishReq.GetSerializedSize ();
        break;
      case PUBLISH_RSP:
        size += m_message.publishRsp.GetSerializedSize ();
        break;
      case REJOIN_REQ:
        size += m_message.rejoinReq.GetSerializedSize ();
        break;
      case SEARCH_REQ:
        size += m_message.searchReq.GetSerializedSize ();
        break;
      case SEARCH_RSP:
        size += m_message.searchRsp.GetSerializedSize ();
        break;
      default:
        NS_ASSERT (false);
    }
  return size;
}

void
PennSearchMessage::Print (std::ostream &os) const
{
  os << "\n****PennSearchMessage Dump****\n" ;
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
      case PUBLISH_REQ:
        m_message.publishReq.Print (os);
        break;
      case PUBLISH_RSP:
        m_message.publishRsp.Print (os);
        break;
      case REJOIN_REQ:
        m_message.rejoinReq.Print (os);
        break;
      case SEARCH_REQ:
        m_message.searchReq.Print (os);
        break;
      case SEARCH_RSP:
        m_message.searchRsp.Print (os);
        break;
      default:
        break;  
    }
  os << "\n****END OF MESSAGE****\n";
}

void
PennSearchMessage::Serialize (Buffer::Iterator start) const
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
      case PUBLISH_REQ:
        m_message.publishReq.Serialize (i);
        break;
      case PUBLISH_RSP:
        m_message.publishRsp.Serialize (i);
        break;
      case REJOIN_REQ:
        m_message.rejoinReq.Serialize (i);
        break;
      case SEARCH_REQ:
        m_message.searchReq.Serialize (i);
        break;
      case SEARCH_RSP:
        m_message.searchRsp.Serialize (i);
        break;
      default:
        NS_ASSERT (false);   
    }
}

uint32_t 
PennSearchMessage::Deserialize (Buffer::Iterator start)
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
      case PUBLISH_REQ:
        size += m_message.publishReq.Deserialize (i);
        break;
      case PUBLISH_RSP:
        size += m_message.publishRsp.Deserialize (i);
        break;
      case REJOIN_REQ:
        size += m_message.rejoinReq.Deserialize (i);
        break;
      case SEARCH_REQ: 
        size += m_message.searchReq.Deserialize (i);
        break;
      case SEARCH_RSP:
        size += m_message.searchRsp.Deserialize (i);
        break;
      default:
        NS_ASSERT (false);
    }
  return size;
}

/* PING_REQ */

uint32_t 
PennSearchMessage::PingReq::GetSerializedSize (void) const
{
  uint32_t size;
  size = sizeof(uint16_t) + pingMessage.length();
  return size;
}

void
PennSearchMessage::PingReq::Print (std::ostream &os) const
{
  os << "PingReq:: Message: " << pingMessage << "\n";
}

void
PennSearchMessage::PingReq::Serialize (Buffer::Iterator &start) const
{
  start.WriteU16 (pingMessage.length ());
  start.Write ((uint8_t *) (const_cast<char*> (pingMessage.c_str())), pingMessage.length());
}

uint32_t
PennSearchMessage::PingReq::Deserialize (Buffer::Iterator &start)
{  
  uint16_t length = start.ReadU16 ();
  char* str = (char*) malloc (length);
  start.Read ((uint8_t*)str, length);
  pingMessage = std::string (str, length);
  free (str);
  return PingReq::GetSerializedSize ();
}

void
PennSearchMessage::SetPingReq (std::string pingMessage)
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

PennSearchMessage::PingReq
PennSearchMessage::GetPingReq ()
{
  return m_message.pingReq;
}

/* PING_RSP */

uint32_t 
PennSearchMessage::PingRsp::GetSerializedSize (void) const
{
  uint32_t size;
  size = sizeof(uint16_t) + pingMessage.length();
  return size;
}

void
PennSearchMessage::PingRsp::Print (std::ostream &os) const
{
  os << "PingReq:: Message: " << pingMessage << "\n";
}

void
PennSearchMessage::PingRsp::Serialize (Buffer::Iterator &start) const
{
  start.WriteU16 (pingMessage.length ());
  start.Write ((uint8_t *) (const_cast<char*> (pingMessage.c_str())), pingMessage.length());
}

uint32_t
PennSearchMessage::PingRsp::Deserialize (Buffer::Iterator &start)
{  
  uint16_t length = start.ReadU16 ();
  char* str = (char*) malloc (length);
  start.Read ((uint8_t*)str, length);
  pingMessage = std::string (str, length);
  free (str);
  return PingRsp::GetSerializedSize ();
}

void
PennSearchMessage::SetPingRsp (std::string pingMessage)
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

PennSearchMessage::PingRsp
PennSearchMessage::GetPingRsp ()
{
  return m_message.pingRsp;
}


//
//
//

void
PennSearchMessage::SetMessageType (MessageType messageType)
{
  m_messageType = messageType;
}

PennSearchMessage::MessageType
PennSearchMessage::GetMessageType () const
{
  return m_messageType;
}

void
PennSearchMessage::SetTransactionId (uint32_t transactionId)
{
  m_transactionId = transactionId;
}

uint32_t 
PennSearchMessage::GetTransactionId (void) const
{
  return m_transactionId;
}

/** PUBLISH REQ **/

/**
 * Print publish request
 * \param os The output stream
 */
void
PennSearchMessage::PublishReq::Print (std::ostream &os) const
{
  os << "PublishReq:: Keyword: " << keyword << " DocID: " << "\n";
}

/**
 * Get serialized size of publish request
 * \return The serialized size
 */
uint32_t
PennSearchMessage::PublishReq::GetSerializedSize (void) const
{
  uint32_t size = sizeof(uint16_t) + keyword.size(); // keyword
  size += sizeof(uint32_t); // number of docIDs

  for (const auto& doc : docID) {
    size += sizeof(uint32_t); // length of doc
    size += doc.size();       // doc string
  }
  return size;
}

/**
 * Serialize publish request
 * \param start The buffer iterator
 */
void
PennSearchMessage::PublishReq::Serialize (Buffer::Iterator &start) const
{
  start.WriteU16(keyword.size());
  start.Write((uint8_t *) keyword.data(), keyword.size());

  start.WriteHtonU32(docID.size());
  for (const auto& doc : docID) {
    start.WriteHtonU32(doc.size());
    start.Write((uint8_t *) doc.data(), doc.size());
  }
}

/**
 * Deserialize publish request
 * \param start The buffer iterator
 * \return The serialized size
 */
uint32_t
PennSearchMessage::PublishReq::Deserialize (Buffer::Iterator &start)
{
  uint16_t klen = start.ReadU16();
  keyword.resize(klen);
  start.Read((uint8_t *) keyword.data(), klen);

  uint32_t numDocs = start.ReadNtohU32();
  docID.clear();
  for (uint32_t i = 0; i < numDocs; ++i) {
    uint32_t len = start.ReadNtohU32();
    std::string doc;
    doc.resize(len);
    start.Read((uint8_t *) doc.data(), len);
    docID.push_back(doc);
  }

  return GetSerializedSize();
}

/**
 * Set publish request
 * \param keyword The keyword
 * \param docID The docID
 */
void
PennSearchMessage::SetPublishReq (std::string keyword, const std::vector<std::string>& docID)
{
  if (m_messageType == 0)
    {
      m_messageType = PUBLISH_REQ;
    }
  else
    {
      NS_ASSERT (m_messageType == PUBLISH_REQ);
    }
  m_message.publishReq.keyword = keyword;
  m_message.publishReq.docID = docID;
}

PennSearchMessage::PublishReq
PennSearchMessage::GetPublishReq ()
{
  return m_message.publishReq;
}


/** PUBLISH RSP **/

/**
 * Print publish response
 * \param os The output stream
 */
void
PennSearchMessage::PublishRsp::Print (std::ostream &os) const
{
  os << "PublishRsp:: \n";
}

/**
 * Get serialized size of publish response
 * \return The serialized size
 */
uint32_t
PennSearchMessage::PublishRsp::GetSerializedSize (void) const
{
  return 0;
} 

/**
 * Serialize publish response
 * \param start The buffer iterator
 */
void
PennSearchMessage::PublishRsp::Serialize (Buffer::Iterator &start) const
{
}

/**
 * Deserialize publish response
 * \param start The buffer iterator
 * \return The serialized size
 */
uint32_t
PennSearchMessage::PublishRsp::Deserialize (Buffer::Iterator &start)
{
  return GetSerializedSize();
}

/**
 * Set publish response
 */
void
PennSearchMessage::SetPublishRsp ()
{
  if (m_messageType == 0)
  {
    m_messageType = PUBLISH_RSP;
  } 
  else {
    NS_ASSERT (m_messageType == PUBLISH_RSP);
  }
}

/**
 * Get publish response
 * \return The publish response
 */
PennSearchMessage::PublishRsp
PennSearchMessage::GetPublishRsp ()
{
  return m_message.publishRsp;
}

/*RejoinReq*/
uint32_t PennSearchMessage::RejoinReq::GetSerializedSize() const {
  return IPV4_ADDRESS_SIZE;
}

void PennSearchMessage::RejoinReq::Print(std::ostream &os) const {
  os << "Rejoin - requester = " << requester;
}

void PennSearchMessage::RejoinReq::Serialize(Buffer::Iterator &start) const {
  uint32_t ip = requester.Get();
  start.WriteHtonU32(ip);
}

uint32_t PennSearchMessage::RejoinReq::Deserialize(Buffer::Iterator &start) {
  requester = Ipv4Address(start.ReadNtohU32());
  return GetSerializedSize();
}

void
PennSearchMessage::SetRejoinReq (Ipv4Address requesterIp)
{
  if (m_messageType == 0)
    {
      m_messageType = REJOIN_REQ;
    }
  else
    {
      NS_ASSERT (m_messageType == REJOIN_REQ);
    }
  m_message.rejoinReq.requester = requesterIp;
}

PennSearchMessage::RejoinReq
PennSearchMessage::GetRejoinReq ()
{
  return m_message.rejoinReq;
}

/* SEARCH_REQ */
uint32_t PennSearchMessage::SearchReq::GetSerializedSize() const {
  uint32_t size = sizeof(uint32_t);  // requester IP

  size += sizeof(uint16_t);          // number of keywords
  for (const auto& keyword : keywords) {
    size += sizeof(uint16_t);        // length prefix
    size += keyword.size();          // string bytes
  }

  size += sizeof(uint16_t);          // number of docs
  for (const auto& doc : returnDocs) {
    size += sizeof(uint16_t);        // length prefix
    size += doc.size();              // string bytes
  }

  size += sizeof(uint16_t);          // keywordIndex
  return size;
}

void PennSearchMessage::SearchReq::Print(std::ostream &os) const {
  os << "SearchReq:: requester = " << requester << "\n";
  os << "SearchReq:: keywords = ";
  for (const auto& keyword : keywords) {
    os << keyword << " ";
  }
  os << "\n";
  os << "SearchReq:: returnDocs = ";
  for (const auto& doc : returnDocs) {
    os << doc << " ";
  }
  os << "\n";
  os << "SearchReq:: keywordIndex = " << keywordIndex << "\n";
}

void PennSearchMessage::SearchReq::Serialize(Buffer::Iterator &start) const {
  start.WriteU32(requester.Get());

  // Serialize keywords
  start.WriteU16(keywords.size());
  for (const auto& keyword : keywords) {
    start.WriteU16(keyword.size());
    start.Write((uint8_t*)keyword.data(), keyword.size());
  }

  // Serialize returnDocs
  start.WriteU16(returnDocs.size());
  for (const auto& doc : returnDocs) {
    start.WriteU16(doc.size());
    start.Write((uint8_t*)doc.data(), doc.size());
  }

  start.WriteU16(keywordIndex);
}

uint32_t PennSearchMessage::SearchReq::Deserialize(Buffer::Iterator &start) {
  requester = Ipv4Address(start.ReadU32());

  // Deserialize keywords
  uint16_t numKeywords = start.ReadU16();
  keywords.clear();
  for (uint16_t i = 0; i < numKeywords; ++i) {
    uint16_t len = start.ReadU16();
    std::string keyword(len, '\0');
    start.Read((uint8_t*)keyword.data(), len);
    keywords.push_back(keyword);
  }

  // Deserialize returnDocs
  uint16_t numDocs = start.ReadU16();
  returnDocs.clear();
  for (uint16_t i = 0; i < numDocs; ++i) {
    uint16_t len = start.ReadU16();
    std::string doc(len, '\0');
    start.Read((uint8_t*)doc.data(), len);
    returnDocs.push_back(doc);
  }

  keywordIndex = start.ReadU16();

  return GetSerializedSize();
}

void
PennSearchMessage::SetSearchReq (Ipv4Address requester, std::vector<std::string>& keywords, std::vector<std::string>& returnDocs, uint32_t keywordIndex)
{
  if (m_messageType == 0)
    {
      m_messageType = SEARCH_REQ;
    }
  else
    {
      NS_ASSERT (m_messageType == SEARCH_REQ);
    }
  m_message.searchReq.requester = requester;
  m_message.searchReq.keywords = keywords;
  m_message.searchReq.returnDocs = returnDocs;
  m_message.searchReq.keywordIndex = keywordIndex;
}

PennSearchMessage::SearchReq
PennSearchMessage::GetSearchReq ()
{
  return m_message.searchReq;
}


/* SEARCH_RSP */
uint32_t PennSearchMessage::SearchRsp::GetSerializedSize() const {
  uint32_t size = IPV4_ADDRESS_SIZE;
  size += sizeof(uint32_t); // result count
  for (const auto& result : results) {
    size += sizeof(uint32_t); // length
    size += result.size();    // actual string
  }
  return size;
}

void PennSearchMessage::SearchRsp::Print(std::ostream &os) const {
  os << "SearchRsp:: requester = " << requester << "\n";
  os << "\n";
  os << "SearchRsp:: results = ";
  for (const auto& doc : results) {
    os << doc << " ";
  }
  os << "\n";
}

void PennSearchMessage::SearchRsp::Serialize(Buffer::Iterator &start) const {
  start.WriteHtonU32(requester.Get());

  start.WriteHtonU32(results.size());
  for (const auto& result : results) {
    start.WriteHtonU32(result.size());
    start.Write(reinterpret_cast<const uint8_t*>(result.data()), result.size());
  }
}

uint32_t PennSearchMessage::SearchRsp::Deserialize(Buffer::Iterator &start) {
  requester = Ipv4Address(start.ReadNtohU32());

  uint32_t count = start.ReadNtohU32();
  results.clear();
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t len = start.ReadNtohU32();
    std::string result(len, '\0');
    start.Read(reinterpret_cast<uint8_t*>(&result[0]), len);
    results.push_back(result);
  }

  return GetSerializedSize();
}

void
PennSearchMessage::SetSearchRsp (Ipv4Address requester, std::vector<std::string>& returnDocs)
{
  if (m_messageType == 0)
    {
      m_messageType = SEARCH_RSP;
    }
  else
    {
      NS_ASSERT (m_messageType == SEARCH_RSP);
    }
  m_message.searchRsp.requester = requester;
  m_message.searchRsp.results = returnDocs;
}

PennSearchMessage::SearchRsp
PennSearchMessage::GetSearchRsp ()
{
  return m_message.searchRsp;
}