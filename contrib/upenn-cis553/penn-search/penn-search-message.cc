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
        // MS2 Message types
      case SEARCH_REQ:
        size += m_message.searchReq.GetSerializedSize ();
        break;
      case SEARCH_RSP:
        size += m_message.searchRsp.GetSerializedSize ();
        break;
      case PUBLISH_REQ:
        size += m_message.publishReq.GetSerializedSize ();
        break;
      case STORE_REQ:
        size += m_message.storeReq.GetSerializedSize ();
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
          // MS2 Message types
      case SEARCH_REQ:
        m_message.searchReq.Print (os);
        break;
      case SEARCH_RSP:
        m_message.searchRsp.Print (os);
        break;
      case PUBLISH_REQ:
        m_message.publishReq.Print (os);
        break;
      case STORE_REQ:
        m_message.storeReq.Print (os);
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
        // MS2 Message types
      case SEARCH_REQ:
        m_message.searchReq.Serialize (i);
        break;
      case SEARCH_RSP:
        m_message.searchRsp.Serialize (i);
        break;
      case PUBLISH_REQ:
        m_message.publishReq.Serialize (i);
        break;
      case STORE_REQ:
        m_message.storeReq.Serialize (i);
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
          // MS2 Message types
      case SEARCH_REQ:
        size += m_message.searchReq.Deserialize (i);
        break;
      case SEARCH_RSP:
        size += m_message.searchRsp.Deserialize (i);
        break;
      case PUBLISH_REQ:
        size += m_message.publishReq.Deserialize (i);
        break;
      case STORE_REQ:
        size += m_message.storeReq.Deserialize (i);
        break;
      default:
        NS_ASSERT (false);
    }
  return size;
}

/* ========================================================================
 *                 MS2 SEARCH_REQ
 * ======================================================================== */

 uint32_t
 PennSearchMessage::SearchReq::GetSerializedSize(void) const
 {
     // For each string: 2 byte length + bytes of the string
     return  sizeof(uint16_t) + originIp.length()
           + sizeof(uint16_t) + remainingTerms.length()
           + sizeof(uint16_t) + currentDocs.length()
           + sizeof(uint16_t) + currentKeyword.length();
 }

 void
 PennSearchMessage::SearchReq::Serialize(Buffer::Iterator &start) const
 {
     start.WriteU16(originIp.length());
     start.Write((uint8_t*)originIp.c_str(), originIp.length());

     start.WriteU16(remainingTerms.length());
     start.Write((uint8_t*)remainingTerms.c_str(), remainingTerms.length());

     start.WriteU16(currentDocs.length());
     start.Write((uint8_t*)currentDocs.c_str(), currentDocs.length());

     start.WriteU16(currentKeyword.length());
     start.Write((uint8_t*)currentKeyword.c_str(), currentKeyword.length());
 }

 uint32_t
 PennSearchMessage::SearchReq::Deserialize(Buffer::Iterator &start)
 {
     uint16_t len;
     char* buf;

     len = start.ReadU16();
     buf = (char*)malloc(len);
     start.Read((uint8_t*)buf, len);
     originIp.assign(buf, len);
     free(buf);

     len = start.ReadU16();
     buf = (char*)malloc(len);
     start.Read((uint8_t*)buf, len);
     remainingTerms.assign(buf, len);
     free(buf);

     len = start.ReadU16();
     buf = (char*)malloc(len);
     start.Read((uint8_t*)buf, len);
     currentDocs.assign(buf, len);
     free(buf);

     len = start.ReadU16();
     buf = (char*)malloc(len);
     start.Read((uint8_t*)buf, len);
     currentKeyword.assign(buf, len);
     free(buf);

     return GetSerializedSize();
 }

 void
 PennSearchMessage::SearchReq::Print(std::ostream &os) const
 {
     os << "SearchReq:: originIp=" << originIp
        << " remainingTerms="    << remainingTerms
        << " currentDocs="       << currentDocs
        << " currentKeyword="    << currentKeyword
        << "\n";
 }

 /* ========================================================================
  *                 MS2 SEARCH_RSP
  * ======================================================================== */

  uint32_t
  PennSearchMessage::SearchRsp::GetSerializedSize(void) const
  {
      return  sizeof(uint16_t) + originIp.length()
            + sizeof(uint16_t) + finalDocs.length();
  }

  void
  PennSearchMessage::SearchRsp::Serialize(Buffer::Iterator &start) const
  {
      start.WriteU16(originIp.length());
      start.Write((uint8_t*)originIp.c_str(), originIp.length());

      start.WriteU16(finalDocs.length());
      start.Write((uint8_t*)finalDocs.c_str(), finalDocs.length());
  }

  uint32_t
  PennSearchMessage::SearchRsp::Deserialize(Buffer::Iterator &start)
  {
      uint16_t len;
      char* buf;

      len = start.ReadU16();
      buf = (char*)malloc(len);
      start.Read((uint8_t*)buf, len);
      originIp.assign(buf, len);
      free(buf);

      len = start.ReadU16();
      buf = (char*)malloc(len);
      start.Read((uint8_t*)buf, len);
      finalDocs.assign(buf, len);
      free(buf);

      return GetSerializedSize();
  }

  void
  PennSearchMessage::SearchRsp::Print(std::ostream &os) const
  {
      os << "SearchRsp:: originIp=" << originIp
         << " finalDocs="           << finalDocs
         << "\n";
  }

  /* ========================================================================
   *                 MS2 PUBLISH_REQ
   * ======================================================================== */

  uint32_t
  PennSearchMessage::PublishReq::GetSerializedSize(void) const
  {
      return sizeof(uint16_t) + keyword.length() +
             sizeof(uint16_t) + docId.length();
  }

  void
  PennSearchMessage::PublishReq::Serialize(Buffer::Iterator &start) const
  {
      start.WriteU16(keyword.length());
      start.Write((uint8_t*)keyword.c_str(), keyword.length());

      start.WriteU16(docId.length());
      start.Write((uint8_t*)docId.c_str(), docId.length());
  }

  uint32_t
  PennSearchMessage::PublishReq::Deserialize(Buffer::Iterator &start)
  {
      uint16_t len;
      char* buf;

      len = start.ReadU16();
      buf = (char*)malloc(len);
      start.Read((uint8_t*)buf, len);
      keyword.assign(buf, len);
      free(buf);

      len = start.ReadU16();
      buf = (char*)malloc(len);
      start.Read((uint8_t*)buf, len);
      docId.assign(buf, len);
      free(buf);

      return GetSerializedSize();
  }

  void
  PennSearchMessage::PublishReq::Print(std::ostream &os) const
  {
      os << "PublishReq:: keyword=" << keyword
         << " docId="               << docId
         << "\n";
  }

  /* ========================================================================
   *                 MS2 STORE_REQ
   * ======================================================================== */

  uint32_t
  PennSearchMessage::StoreReq::GetSerializedSize(void) const
  {
      return sizeof(uint16_t) + keyword.length() +
             sizeof(uint16_t) + docId.length();
  }

  void
  PennSearchMessage::StoreReq::Serialize(Buffer::Iterator &start) const
  {
      start.WriteU16(keyword.length());
      start.Write((uint8_t*)keyword.c_str(), keyword.length());

      start.WriteU16(docId.length());
      start.Write((uint8_t*)docId.c_str(), docId.length());
  }

  uint32_t
  PennSearchMessage::StoreReq::Deserialize(Buffer::Iterator &start)
  {
      uint16_t len;
      char* buf;

      len = start.ReadU16();
      buf = (char*)malloc(len);
      start.Read((uint8_t*)buf, len);
      keyword.assign(buf, len);
      free(buf);

      len = start.ReadU16();
      buf = (char*)malloc(len);
      start.Read((uint8_t*)buf, len);
      docId.assign(buf, len);
      free(buf);

      return GetSerializedSize();
  }

  void
  PennSearchMessage::StoreReq::Print(std::ostream &os) const
  {
      os << "StoreReq:: keyword=" << keyword
         << " docId="             << docId
         << "\n";
  }


/* ============================================================================
 *               M S 2    G E T T E R S   A N D   S E T T E R S
 * ============================================================================
 */

PennSearchMessage::SearchReq
PennSearchMessage::GetSearchReq ()
{
  return m_message.searchReq;
}

void
PennSearchMessage::SetSearchReq(const std::string &originIp,
                                const std::string &remainingTerms,
                                const std::string &currentDocs,
                                const std::string &currentKeyword)
{
    m_messageType = SEARCH_REQ;
    m_message.searchReq.originIp = originIp;
    m_message.searchReq.remainingTerms = remainingTerms;
    m_message.searchReq.currentDocs = currentDocs;
    m_message.searchReq.currentKeyword = currentKeyword;
}

PennSearchMessage::SearchRsp
PennSearchMessage::GetSearchRsp ()
{
  return m_message.searchRsp;
}

void
PennSearchMessage::SetSearchRsp(const std::string &originIp,
                                const std::string &finalDocs)
{
    m_messageType = SEARCH_RSP;
    m_message.searchRsp.originIp = originIp;
    m_message.searchRsp.finalDocs = finalDocs;
}

PennSearchMessage::PublishReq
PennSearchMessage::GetPublishReq ()
{
  return m_message.publishReq;
}

void
PennSearchMessage::SetPublishReq(const std::string &keyword,
                                 const std::string &docId)
{
    m_messageType = PUBLISH_REQ;
    m_message.publishReq.keyword = keyword;
    m_message.publishReq.docId = docId;
}

PennSearchMessage::StoreReq
PennSearchMessage::GetStoreReq ()
{
  return m_message.storeReq;
}

void
PennSearchMessage::SetStoreReq(const std::string &keyword,
                               const std::string &docId)
{
    m_messageType = STORE_REQ;
    m_message.storeReq.keyword = keyword;
    m_message.storeReq.docId = docId;
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