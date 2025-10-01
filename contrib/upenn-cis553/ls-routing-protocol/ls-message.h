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

#ifndef LS_MESSAGE_H
#define LS_MESSAGE_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include <vector>
#include <utility>
#include <string>

using namespace ns3;

#define IPV4_ADDRESS_SIZE 4

class LSMessage : public Header
  {
  public:
    LSMessage();
    virtual ~LSMessage();

    enum MessageType
      {
      PING_REQ,
      PING_RSP,
      HELLO_REQ,
      HELLO_RSP,
      LSA_m,
      };

    LSMessage(LSMessage::MessageType messageType, uint32_t sequenceNumber, uint8_t ttl, Ipv4Address originatorAddress);

    void SetMessageType(MessageType messageType);
    MessageType GetMessageType() const;

    void SetSequenceNumber(uint32_t sequenceNumber);
    uint32_t GetSequenceNumber() const;

    void SetOriginatorAddress(Ipv4Address originatorAddress);
    Ipv4Address GetOriginatorAddress() const;

    void SetTTL(uint8_t ttl);
    uint8_t GetTTL() const;

  private:
    MessageType m_messageType;
    uint32_t m_sequenceNumber;
    Ipv4Address m_originatorAddress;
    uint8_t m_ttl;

  public:
    static TypeId GetTypeId(void);
    virtual TypeId GetInstanceTypeId(void) const;
    void Print(std::ostream& os) const;
    uint32_t GetSerializedSize(void) const;
    void Serialize(Buffer::Iterator start) const;
    uint32_t Deserialize(Buffer::Iterator start);

    // Ping Request and Response
    struct PingReq
      {
      void Print(std::ostream& os) const;
      uint32_t GetSerializedSize(void) const;
      void Serialize(Buffer::Iterator& start) const;
      uint32_t Deserialize(Buffer::Iterator& start);
      Ipv4Address destinationAddress;
      std::string pingMessage;
      };

    struct PingRsp
      {
      void Print(std::ostream& os) const;
      uint32_t GetSerializedSize(void) const;
      void Serialize(Buffer::Iterator& start) const;
      uint32_t Deserialize(Buffer::Iterator& start);
      Ipv4Address destinationAddress;
      std::string pingMessage;
      };

    // Hello Request and Response
    struct HelloReq
      {
      void Print(std::ostream& os) const;
      uint32_t GetSerializedSize(void) const;
      void Serialize(Buffer::Iterator& start) const;
      uint32_t Deserialize(Buffer::Iterator& start);
      Ipv4Address destinationAddress;
      std::string helloMessage;
      };

    struct HelloRsp
      {
      void Print(std::ostream& os) const;
      uint32_t GetSerializedSize(void) const;
      void Serialize(Buffer::Iterator& start) const;
      uint32_t Deserialize(Buffer::Iterator& start);
      Ipv4Address destinationAddress;
      std::string helloMessage;
      };
    
    // Link State Advertisement (LSA) -- RENAME LSA STRUCT AND MEMBERS
    struct Lsa
      {
      void Print(std::ostream& os) const;
      uint32_t GetSerializedSize(void) const;
      void Serialize(Buffer::Iterator& start) const;
      uint32_t Deserialize(Buffer::Iterator& start);
      // Payload - Renamed from lsaMessage
      std::vector<std::pair<uint32_t, uint32_t>> linkVector;
      };

  private:
    struct
      {
      PingReq pingReq;
      PingRsp pingRsp;
      HelloReq helloReq;
      HelloRsp helloRsp;
      Lsa lsa; // RENAME UNION MEMBER
      } m_message;

  public:
    PingReq GetPingReq();
    HelloReq GetHelloReq();
    Lsa GetLsa(); // RENAME GETTER FUNCTION
    
    void SetPingReq(Ipv4Address destinationAddress, std::string message);
    // RENAME SETTER FUNCTION AND USE CONST REFERENCE FOR EFFICIENCY
    void SetLsa(const std::vector<std::pair<uint32_t, uint32_t>>& links);
    void SetHelloReq(Ipv4Address destinationAddress, std::string message);
    
    PingRsp GetPingRsp();
    HelloRsp GetHelloRsp();
    
    void SetPingRsp(Ipv4Address destinationAddress, std::string message);
    void SetHelloRsp(Ipv4Address destinationAddress, std::string message);
  }; // class LSMessage

static inline std::ostream&
operator<< (std::ostream& os, const LSMessage& message)
  {
  message.Print(os);
  return os;
  }

#endif
