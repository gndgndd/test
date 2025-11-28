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
#include <string>

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
        // MS2 message types
        SEARCH_REQ = 3,  // Multi keyword search
        SEARCH_RSP = 4, // Multi keyword search
        PUBLISH_REQ = 5,  // Inverted list publishing
        STORE_REQ = 6     // Inverted list publishing
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

// MS2 PAYLOAD STRUCTURES

    /* SEARCH_REQ
    * Sent hop by hop during multi keyword resolution.
    * Each hop handles exactly ONE keyword.
    */
   struct SearchReq
   {
       std::string originIp;        // Original requester
       std::string remainingTerms;  // Terms still not processed
       std::string currentDocs;     // Intersection so far
       std::string currentKeyword;  // Term being resolved at this hop

       uint32_t GetSerializedSize() const;
       void Serialize(Buffer::Iterator &start) const;
       uint32_t Deserialize(Buffer::Iterator &start);
       void Print(std::ostream &os) const;
   };

   /* SEARCH_RSP
    * FINAL SEARCH RESPONSE
    * Contains the final result only.
    */
   struct SearchRsp
   {
       std::string originIp;   // Final destination
       std::string finalDocs;  // Completed doc list

       uint32_t GetSerializedSize() const;
       void Serialize(Buffer::Iterator &start) const;
       uint32_t Deserialize(Buffer::Iterator &start);
       void Print(std::ostream &os) const;
   };

      /**
      * PUBLISH_REQ payload
      */
      struct PublishReq
    {
        std::string keyword;     // Keyword being published
        std::string docId;       // Document ID mapped to this keyword

        uint32_t GetSerializedSize() const;
        void Serialize(Buffer::Iterator &start) const;
        uint32_t Deserialize(Buffer::Iterator &start);
        void Print(std::ostream &os) const;
    };

      /**
      * STORE_REQ payload
      */
      struct StoreReq
    {
        std::string keyword;
        std::string docId;

        uint32_t GetSerializedSize() const;
        void Serialize(Buffer::Iterator &start) const;
        uint32_t Deserialize(Buffer::Iterator &start);
        void Print(std::ostream &os) const;
    };

  private:
    struct
      {
        PingReq pingReq;
        PingRsp pingRsp;
        // MS2 message payloads
       SearchReq searchReq;
       SearchRsp searchRsp;
       PublishReq publishReq;
       StoreReq storeReq;
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

//MS2 GETTERS AND SETTERS

    SearchReq GetSearchReq();
    void SetSearchReq(const std::string &originIp,
                      const std::string &remainingTerms,
                      const std::string &currentDocs,
                      const std::string &currentKeyword);

    SearchRsp GetSearchRsp();
    void SetSearchRsp(const std::string &originIp,
                      const std::string &finalDocs);

    PublishReq GetPublishReq();
    void SetPublishReq(const std::string &keyword,
                      const std::string &docId);

    StoreReq GetStoreReq();
    void SetStoreReq(const std::string &keyword,
                    const std::string &docId);

}; // class PennSearchMessage

static inline std::ostream& operator<< (std::ostream& os, const PennSearchMessage& message)
{
  message.Print (os);
  return os;
}

#endif

