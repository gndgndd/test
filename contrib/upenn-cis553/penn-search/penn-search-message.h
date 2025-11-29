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
// Added to support vector serialization used in penn-search.cc
#include <vector>
#include <string>

using namespace ns3;

#define IPV4_ADDRESS_SIZE 4

class PennSearchMessage : public Header
{
public:
  // =================================================================
  // SKELETON & CORE INTERFACE
  // =================================================================
  PennSearchMessage ();
  virtual ~PennSearchMessage ();


  enum MessageType
    {
      // Skeleton Types
      PING_REQ = 1,
      PING_RSP = 2,
      
      // Extended Protocol Types
      PUBLISH_REQ = 3,
      PUBLISH_RSP = 4,
      SEARCH_REQ = 5,
      SEARCH_RSP = 6,
      REJOIN_REQ = 7,
    };

  PennSearchMessage (PennSearchMessage::MessageType messageType, uint32_t transactionId);

  void SetMessageType (MessageType messageType);
  MessageType GetMessageType () const;

  void SetTransactionId (uint32_t transactionId);
  uint32_t GetTransactionId () const;

private:
  MessageType m_messageType;
  uint32_t m_transactionId;

public:
  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  void Print (std::ostream &os) const;
  uint32_t GetSerializedSize (void) const;
  void Serialize (Buffer::Iterator start) const;
  uint32_t Deserialize (Buffer::Iterator start);

  // =================================================================
  // PAYLOAD STRUCTURES
  // =================================================================
  
  // --- Skeleton Payloads ---
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

  // --- Search Protocol Payloads ---
  struct SearchReq
  {
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator &start) const;
    uint32_t Deserialize (Buffer::Iterator &start);

    Ipv4Address requester;
    std::vector<std::string> keywords;
    std::vector<std::string> returnDocs;
    uint32_t keywordIndex;
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

  // --- Publish Protocol Payloads ---
  struct PublishReq
  {
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator &start) const;
    uint32_t Deserialize (Buffer::Iterator &start);

    std::string keyword;
    std::vector<std::string> docID;
  };

  struct PublishRsp
  {
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator &start) const;
    uint32_t Deserialize (Buffer::Iterator &start);
    // Empty payload for ACK
  };

  // --- Maintenance Payloads ---
  struct RejoinReq
  {
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator &start) const;
    uint32_t Deserialize (Buffer::Iterator &start);

    Ipv4Address requestor;
  };


private:
  struct
    {
      PingReq pingReq;
      PingRsp pingRsp;
      SearchReq searchReq;
      SearchRsp searchRsp;
      PublishReq publishReq;
      PublishRsp publishRsp;
      RejoinReq rejoinReq;
    } m_message;
  
public:
  // =================================================================
  // ACCESSORS
  // =================================================================

  // --- Skeleton Accessors ---
  PingReq GetPingReq ();
  void SetPingReq (std::string message);

  PingRsp GetPingRsp ();
  void SetPingRsp (std::string message);

  // --- Search Accessors ---
  void SetSearchReq (Ipv4Address requester, std::vector<std::string> keywords, std::vector<std::string> returnDocs, uint32_t index);
  SearchReq GetSearchReq ();

  void SetSearchRsp (Ipv4Address requester, std::vector<std::string> results);
  SearchRsp GetSearchRsp ();

  // --- Publish Accessors ---
  void SetPublishReq (std::string keyword, std::vector<std::string> docID);
  PublishReq GetPublishReq ();

  void SetPublishRsp ();
  PublishRsp GetPublishRsp ();

  // --- Maintenance Accessors ---
  void SetRejoinReq (Ipv4Address requestor);
  RejoinReq GetRejoinReq ();

}; // class PennSearchMessage

static inline std::ostream& operator<< (std::ostream& os, const PennSearchMessage& message)
{
  message.Print (os);
  return os;
}

#endif