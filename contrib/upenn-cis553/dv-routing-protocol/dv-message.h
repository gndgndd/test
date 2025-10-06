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

#ifndef DV_MESSAGE_H
#define DV_MESSAGE_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include "ns3/packet.h"
#include "ns3/object.h"
#include <string>
#include <vector>   // MS2 related: needed for DV vector payload (list of {dest,cost} entries)
#include <cstdint>  // MS2 related: defines fixed-width integer types like uint32_t for cost

using namespace ns3;

#define IPV4_ADDRESS_SIZE 4

class DVMessage : public Header
{
  public:
    DVMessage ();
    virtual ~DVMessage ();


    enum MessageType
      {
        PING_REQ = 1,
        PING_RSP = 2,
        // Define extra message types when needed
        HELLO_REQ,    // New type for neighbor discovery HELLO messages
        HELLO_RSP,    // New type for HELLO reply messages
        DV_UPDATE     // MS2 related: new type that advertises this node's distance-vector (routing) entries
                     //  used for periodic + triggered DV advertisements
      };

    DVMessage (DVMessage::MessageType messageType, uint32_t sequenceNumber, uint8_t ttl, Ipv4Address originatorAddress);

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
     *  \brief Sets Sequence Number
     *  \param sequenceNumber Sequence Number of the request
     */
    void SetSequenceNumber (uint32_t sequenceNumber);

    /**
     *  \returns Sequence Number
     */
    uint32_t GetSequenceNumber () const;

    /**
     *  \brief Sets Originator IP Address
     *  \param originatorAddress Originator IPV4 address
     */
    void SetOriginatorAddress (Ipv4Address originatorAddress);

    /**
     *  \returns Originator IPV4 address
     */
    Ipv4Address GetOriginatorAddress () const;

    /**
     *  \brief Sets Time To Live of the message
     *  \param ttl TTL of the message
     */
    void SetTTL (uint8_t ttl);

    /**
     *  \returns TTL of the message
     */
    uint8_t GetTTL () const;

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
    static TypeId GetTypeId (void);
    virtual TypeId GetInstanceTypeId (void) const;
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator start) const;
    uint32_t Deserialize (Buffer::Iterator start);

    struct HelloReq
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        // Payload
        std::string helloMessage;
      };

    struct HelloRsp
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        // Payload
        Ipv4Address sourceAddress; // The address of the node sending the reply
        std::string helloMessage;
      };

    struct PingReq
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        // Payload
        Ipv4Address destinationAddress;
        std::string pingMessage;
      };

    struct PingRsp
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        // Payload
        Ipv4Address destinationAddress;
        std::string pingMessage;
      };

      /** ---------- MS2 payloads ---------- */
 struct DvVectorItem                      // MS2 related: one DV entry (advertised route)
 {                                        // contains the destination and hop-count cost
   Ipv4Address dest;   // destination
   uint32_t    cost;   // hop-count / metric
 };

 struct DvUpdate                          // MS2 related: message body for DV_UPDATE; a list of DV entries
 {
   void Print (std::ostream &os) const;   // MS2 related: pretty-print the advertised vector for logging
   uint32_t GetSerializedSize (void) const; // MS2 related: compute bytes needed to serialize the vector
   void Serialize (Buffer::Iterator &start) const;  // MS2 related: write vector items to the packet buffer
   uint32_t Deserialize (Buffer::Iterator &start);  // MS2 related: read vector items from the packet buffer
   std::vector<DvVectorItem> vec;         // MS2 related: the advertised distance-vector (dest,cost pairs)
 };

  private:
    struct
      {
        PingReq pingReq;
        PingRsp pingRsp;
        HelloReq helloReq;   // New member for HELLO_REQ messages
        HelloRsp helloRsp;   // New member for HELLO_RSP messages
        DvUpdate  dvUpdate;    // MS2 related: carries the DV vector for DV_UPDATE messages
                              //  this is the payload your timers will serialize & send
      } m_message;

  public:
      /**
     * \returns Hello Struct
     */
    HelloReq GetHelloReq();

    /**
     * \brief Sets Hello message params
     */
    void SetHelloReq(std::string helloMessage);

    /**
     * \returns HelloRsp Struct
     */
    HelloRsp GetHelloRsp();

    /**
     * \brief Sets HelloRsp message params
     * \param senderAddress The address of the node sending the reply
     */
    void SetHelloRsp(Ipv4Address sourceAddress, std::string helloMessage);

    /**
     *  \returns PingReq Struct
     */
    PingReq GetPingReq ();

    /**
     *  \brief Sets PingReq message params
     *  \param message Payload String
     */

    void SetPingReq (Ipv4Address destinationAddress, std::string message);

    /**
     * \returns PingRsp Struct
     */
    PingRsp GetPingRsp ();
    /**
     *  \brief Sets PingRsp message params
     *  \param message Payload String
     */
    void SetPingRsp (Ipv4Address destinationAddress, std::string message);

    /** ---------- MS2 accessors ---------- */
    DvUpdate GetDvUpdate() const;                                   // MS2 related: expose the vector for processing/logging
    void SetDvUpdate(const std::vector<DvVectorItem>& items);       // MS2 related: set the vector before (periodic/triggered) send

}; // class DVMessage


static inline std::ostream& operator<< (std::ostream& os, const DVMessage& message)
{
message.Print (os);
return os;
}

#endif
