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

#ifndef PENN_SEARCH_MESSAGE_H
#define PENN_SEARCH_MESSAGE_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include "ns3/packet.h"
#include "ns3/object.h"

using namespace ns3;

#define IPV4_ADDRESS_SIZE 4

class PennSearchMessage : public Header
{
  public:
    PennSearchMessage ();
    virtual ~PennSearchMessage ();


    enum MessageType
      {
        PING_REQ = 1,
        PING_RSP = 2,
        // Define extra message types when needed 
        PUBLISH_REQ = 3,
        PUBLISH_RSP = 4,    
        REJOIN_REQ = 5,
        SEARCH_REQ = 6,
        SEARCH_RSP = 7,
      };

    PennSearchMessage (PennSearchMessage::MessageType messageType, uint32_t transactionId);

    /**
    *  \brief Sets message type
    *  \param messageType message type
    */
    void SetMessageType (MessageType messageType);

    /**
     *  \returns message type
     */
    MessageType GetMessageType () const;

    /**
     *  \brief Sets Transaction Id
     *  \param transactionId Transaction Id of the request
     */
    void SetTransactionId (uint32_t transactionId);

    /**
     *  \returns Transaction Id
     */
    uint32_t GetTransactionId () const;

  private:
    /**
     *  \cond
     */
    MessageType m_messageType;
    uint32_t m_transactionId;
    /**
     *  \endcond
     */
  public:
    static TypeId GetTypeId (void);
    virtual TypeId GetInstanceTypeId (void) const;
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator start) const;
    uint32_t Deserialize (Buffer::Iterator start);

    
    struct PingReq
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        // Payload
        std::string pingMessage;
      };

    struct PingRsp
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        // Payload
        std::string pingMessage;
      };

    /**
     * Publish request: this is the request to publish a keyword and docID
     */
    struct PublishReq
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        // Payload
        std::string keyword;
        std::vector<std::string> docID;
      };

    /**
     * Publish response: this is the response to a publish request
     */
    struct PublishRsp
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        // no payload
      };

    /**
     * Rejoin Req: this is what a search node sends to a successor when it rejoins to get back it's documents
     */
    struct RejoinReq
    {
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);

      Ipv4Address requester;
    };

    struct SearchReq
    {
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);

      Ipv4Address requester;
      std::vector<std::string> keywords;
      std::vector<std::string> returnDocs;
      uint32_t keywordIndex = 0;
    };

    struct SearchRsp
    {
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);

      Ipv4Address requester;
      std::vector<std::string> results;
    };
    
    
    

  private:
    struct
      {
        PingReq pingReq;
        PingRsp pingRsp;
        PublishReq publishReq;
        PublishRsp publishRsp;
        RejoinReq rejoinReq;
        SearchReq searchReq;
        SearchRsp searchRsp;
      } m_message;
    
  public:
    /**
     *  \returns PingReq Struct
     */
    PingReq GetPingReq ();

    /**
     *  \brief Sets PingReq message params
     *  \param message Payload String
     */

    void SetPingReq (std::string message);

    /**
     * \returns PingRsp Struct
     */
    PingRsp GetPingRsp ();
    /**
     *  \brief Sets PingRsp message params
     *  \param message Payload String
     */
    void SetPingRsp (std::string message);

    /**
     *  \returns PublishReq Struct
     */
    PublishReq GetPublishReq ();

    /**
     *  \brief Sets PublishReq message params
     *  \param message Payload String
     */
    void SetPublishReq (std::string keyword, const std::vector<std::string>& docID);

    /**
     *  \returns PublishRsp Struct
     */
    PublishRsp GetPublishRsp ();

    /**
     *  \brief Sets PublishRsp message params
     *  \param message Payload String
     */
    void SetPublishRsp ();

    void SetRejoinReq (Ipv4Address requesterIp);
    RejoinReq GetRejoinReq ();

    void SetSearchReq(Ipv4Address requester, std::vector<std::string>& keywords, std::vector<std::string>& returnDocs, uint32_t keywordIndex);
    SearchReq GetSearchReq();

    void SetSearchRsp(Ipv4Address requester, std::vector<std::string>& returnDocs);
    SearchRsp GetSearchRsp();
}; // class PennSearchMessage

static inline std::ostream& operator<< (std::ostream& os, const PennSearchMessage& message)
{
  message.Print (os);
  return os;
}

#endif
