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


using namespace ns3;

#define IPV4_ADDRESS_SIZE 4

class LSMessage : public Header
  {
  public:
    LSMessage();
    virtual ~LSMessage();

    // TODO: Define extra message types in enum when needed
    enum MessageType
      {
      PING_REQ,
      PING_RSP,
      HELLO_REQ,  // new
      HELLO_RSP,  // new 
      LSA_m,  //new
      };

    LSMessage(LSMessage::MessageType messageType, uint32_t sequenceNumber, uint8_t ttl, Ipv4Address originatorAddress);

    /**
     *  \brief Sets message type
     *  \param messageType message type
     */
    void SetMessageType(MessageType messageType);

    /**
     *  \returns message type
     */
    MessageType GetMessageType() const;

    /**
     *  \brief Sets Sequence Number
     *  \param sequenceNumber Sequence Number of the request
     */
    void SetSequenceNumber(uint32_t sequenceNumber);

    /**
     *  \returns Sequence Number
     */
    uint32_t GetSequenceNumber() const;

    /**
     *  \brief Sets Originator IP Address
     *  \param originatorAddress Originator IPV4 address
     */
    void SetOriginatorAddress(Ipv4Address originatorAddress);

    /**
     *  \returns Originator IPV4 address
     */
    Ipv4Address GetOriginatorAddress() const;

    /**
     *  \brief Sets Time To Live of the message
     *  \param ttl TTL of the message
     */
    void SetTTL(uint8_t ttl);

    /**
     *  \returns TTL of the message
     */
    uint8_t GetTTL() const;

  private:
    /**
     *  \cond
     */
    MessageType m_messageType;
    uint32_t m_sequenceNumber;
    Ipv4Address m_originatorAddress;
    uint8_t m_ttl;
    /**
     *  \endcond
     */
  public:
    static TypeId GetTypeId(void);
    virtual TypeId GetInstanceTypeId(void) const;
    void Print(std::ostream& os) const;
    uint32_t GetSerializedSize(void) const;
    void Serialize(Buffer::Iterator start) const;
    uint32_t Deserialize(Buffer::Iterator start);

    struct PingReq
      {
      void Print(std::ostream& os) const;
      uint32_t GetSerializedSize(void) const;
      void Serialize(Buffer::Iterator& start) const;
      uint32_t Deserialize(Buffer::Iterator& start);
      // Payload
      Ipv4Address destinationAddress;
      std::string pingMessage;
      };

    struct PingRsp
      {
      void Print(std::ostream& os) const;
      uint32_t GetSerializedSize(void) const;
      void Serialize(Buffer::Iterator& start) const;
      uint32_t Deserialize(Buffer::Iterator& start);
      // Payload
      Ipv4Address destinationAddress;
      std::string pingMessage;
      };
    //********************* new *********************//
    struct HelloReq
      {
      void Print(std::ostream& os) const;
      uint32_t GetSerializedSize(void) const;
      void Serialize(Buffer::Iterator& start) const;
      uint32_t Deserialize(Buffer::Iterator& start);
      // Payload
      Ipv4Address destinationAddress;
      std::string helloMessage;
      };
    //********************* new *********************//
    struct HelloRsp
      {
      void Print(std::ostream& os) const;
      uint32_t GetSerializedSize(void) const;
      void Serialize(Buffer::Iterator& start) const;
      uint32_t Deserialize(Buffer::Iterator& start);
      // Payload
      Ipv4Address destinationAddress;
      std::string helloMessage;
      };
   
    
    typedef std::vector<std::pair<uint32_t, uint32_t>> neighborInfo;
    

    struct LsA
      {
      void Print(std::ostream& os) const;
      uint32_t GetSerializedSize(void) const;
      void Serialize(Buffer::Iterator& start) const;
      uint32_t Deserialize(Buffer::Iterator& start);
      // Payload
      //Ipv4Address destinationAddress;
      neighborInfo lsaMessage;
      };

   

  private:
    struct
      {
      PingReq pingReq;
      PingRsp pingRsp;
      //******************* new ******************//
      HelloReq helloReq;
      HelloRsp helloRsp;
      LsA lsA;
      } m_message;
    


  public:
    /**
     *  \returns PingReq Struct
     */
    PingReq GetPingReq();
    //******************* new ******************//
    HelloReq GetHelloReq();
    LsA GetLsA();
    /**
     *  \brief Sets PingReq message params
     *  \param message Payload String
     */

    void SetPingReq(Ipv4Address destinationAddress, std::string message);
    void SetHelloReq(Ipv4Address destinationAddress, std::string message); //**** new ****//
    void SetLsA (neighborInfo lsaMessage);
    /**
     * \returns PingRsp Struct
     */
    PingRsp GetPingRsp();
    //******************* new ******************//
    HelloRsp GetHelloRsp();
    /**
     *  \brief Sets PingRsp message params
     *  \param message Payload String
     */
    void SetPingRsp(Ipv4Address destinationAddress, std::string message);
    void SetHelloRsp(Ipv4Address destinationAddress, std::string message); //**** new ****//
  }; // class LSMessage

static inline std::ostream&
operator<< (std::ostream& os, const LSMessage& message)
  {
  message.Print(os);
  return os;
  }

#endif
