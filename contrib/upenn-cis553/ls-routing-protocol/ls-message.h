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

#ifndef NS3_LS_MESSAGE_H
#define NS3_LS_MESSAGE_H

#include "ns3/header.h"
#include "ns3/buffer.h"
#include "ns3/ipv4-address.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <ostream>

#define IPV4_ADDRESS_SIZE 4

namespace ns3 {

/**
 * LSMessage: control-plane message used by the LS routing protocol.
 * Supports PING_REQ, PING_RSP, HELLO_REQ, HELLO_RSP, and LSA_m.
 *
 * Implementation uses a small polymorphic payload hierarchy to keep
 * serialization code clean and extensible.
 */
class LSMessage : public Header
{
public:
  enum MessageType : uint8_t
  {
    PING_REQ = 1,
    PING_RSP = 2,
    HELLO_REQ = 3,
    HELLO_RSP = 4,
    LSA_m     = 5
  };

  // ===== ns-3 boilerplate =====
  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;

  // ===== ctors / dtors =====
  LSMessage ();
  LSMessage (MessageType t, uint32_t seq, uint8_t ttl, Ipv4Address origin);
  LSMessage (const LSMessage& other);
  LSMessage& operator= (const LSMessage& other);
  virtual ~LSMessage ();

  // ===== Header overrides =====
  virtual void     Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);
  virtual uint32_t GetSerializedSize (void) const;
  virtual void     Print (std::ostream &os) const;

  // ===== accessors (fixed header) =====
  void         SetMessageType (MessageType t);
  MessageType  GetMessageType () const;

  void         SetSequenceNumber (uint32_t s);
  uint32_t     GetSequenceNumber (void) const;

  void         SetTTL (uint8_t ttl);
  uint8_t      GetTTL (void) const;

  void         SetOriginatorAddress (Ipv4Address a);
  Ipv4Address  GetOriginatorAddress (void) const;

  // ===== payload accessors (by type) =====
  // Ping
  struct PingReq;
  struct PingRsp;
  PingReq  GetPingReq () const;
  void     SetPingReq (Ipv4Address dest, std::string payload);
  PingRsp  GetPingRsp () const;
  void     SetPingRsp (Ipv4Address dest, std::string payload);

  // Hello
  struct HelloReq;
  struct HelloRsp;
  HelloReq GetHelloReq () const;
  void     SetHelloReq (Ipv4Address dest, std::string message);
  HelloRsp GetHelloRsp () const;
  void     SetHelloRsp (Ipv4Address dest, std::string message);

  // LSA
  struct Lsa;
  Lsa      GetLsa () const;
  void     SetLsa (const std::vector<std::pair<uint32_t,uint32_t>>& links);

  // ===== payload base =====
  struct MessagePayload
  {
    virtual ~MessagePayload () {}

    virtual std::unique_ptr<MessagePayload> Clone () const = 0;
    virtual uint32_t GetSerializedSize () const = 0;
    virtual void     Print (std::ostream& os) const = 0;
    virtual void     Serialize (Buffer::Iterator &start) const = 0;
    virtual uint32_t Deserialize (Buffer::Iterator &start) = 0;
  };

  // ===== concrete payloads =====
  struct PingReq : public MessagePayload
  {
    Ipv4Address destinationAddress;
    std::string pingMessage;

    std::unique_ptr<MessagePayload> Clone () const override;
    uint32_t GetSerializedSize () const override;
    void     Print (std::ostream& os) const override;
    void     Serialize (Buffer::Iterator &start) const override;
    uint32_t Deserialize (Buffer::Iterator &start) override;
  };

  struct PingRsp : public MessagePayload
  {
    Ipv4Address destinationAddress;
    std::string pingMessage;

    std::unique_ptr<MessagePayload> Clone () const override;
    uint32_t GetSerializedSize () const override;
    void     Print (std::ostream& os) const override;
    void     Serialize (Buffer::Iterator &start) const override;
    uint32_t Deserialize (Buffer::Iterator &start) override;
  };

  struct HelloReq : public MessagePayload
  {
    Ipv4Address destinationAddress;
    std::string helloMessage;

    std::unique_ptr<MessagePayload> Clone () const override;
    uint32_t GetSerializedSize () const override;
    void     Print (std::ostream& os) const override;
    void     Serialize (Buffer::Iterator &start) const override;
    uint32_t Deserialize (Buffer::Iterator &start) override;
  };

  struct HelloRsp : public MessagePayload
  {
    Ipv4Address destinationAddress;
    std::string helloMessage;

    std::unique_ptr<MessagePayload> Clone () const override;
    uint32_t GetSerializedSize () const override;
    void     Print (std::ostream& os) const override;
    void     Serialize (Buffer::Iterator &start) const override;
    uint32_t Deserialize (Buffer::Iterator &start) override;
  };

  struct Lsa : public MessagePayload
  {
    // Vector of (neighborNodeId, cost)
    std::vector<std::pair<uint32_t,uint32_t>> linkVector;

    std::unique_ptr<MessagePayload> Clone () const override;
    uint32_t GetSerializedSize () const override;
    void     Print (std::ostream& os) const override;
    void     Serialize (Buffer::Iterator &start) const override;
    uint32_t Deserialize (Buffer::Iterator &start) override;
  };

private:
  MessageType  m_messageType { PING_REQ };
  uint32_t     m_sequenceNumber { 0 };
  Ipv4Address  m_originatorAddress;
  uint8_t      m_ttl { 0 };

  std::unique_ptr<MessagePayload> m_payload;
};

} // namespace ns3

#endif // NS3_LS_MESSAGE_H
