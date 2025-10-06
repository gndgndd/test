/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef DV_MESSAGE_H
#define DV_MESSAGE_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include "ns3/packet.h"
#include "ns3/object.h"
#include <string>
#include <vector>
#include <cstdint>

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
    HELLO_REQ = 3,
    HELLO_RSP = 4,
    DV_UPDATE = 5
  };

  DVMessage (DVMessage::MessageType messageType, uint32_t sequenceNumber, uint8_t ttl, Ipv4Address originatorAddress);

  void SetMessageType (MessageType messageType);
  MessageType GetMessageType () const;

  void SetSequenceNumber (uint32_t sequenceNumber);
  uint32_t GetSequenceNumber () const;

  void SetOriginatorAddress (Ipv4Address originatorAddress);
  Ipv4Address GetOriginatorAddress () const;

  void SetTTL (uint8_t ttl);
  uint8_t GetTTL () const;

  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  void Print (std::ostream &os) const override;
  uint32_t GetSerializedSize (void) const override;
  void Serialize (Buffer::Iterator start) const override;
  uint32_t Deserialize (Buffer::Iterator start) override;

  struct HelloReq {
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator &start) const;
    uint32_t Deserialize (Buffer::Iterator &start);
    std::string helloMessage;
  };

  struct HelloRsp {
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator &start) const;
    uint32_t Deserialize (Buffer::Iterator &start);
    Ipv4Address sourceAddress;
    std::string helloMessage;
  };

  struct PingReq {
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator &start) const;
    uint32_t Deserialize (Buffer::Iterator &start);
    Ipv4Address destinationAddress;
    std::string pingMessage;
  };

  struct PingRsp {
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator &start) const;
    uint32_t Deserialize (Buffer::Iterator &start);
    Ipv4Address destinationAddress;
    std::string pingMessage;
  };

  struct DvVectorItem {
    Ipv4Address dest;
    uint32_t    cost;
  };

  struct DvUpdate {
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator &start) const;
    uint32_t Deserialize (Buffer::Iterator &start);
    std::vector<DvVectorItem> vec;
  };

private:
  MessageType m_messageType {PING_REQ};
  uint32_t m_sequenceNumber {0};
  Ipv4Address m_originatorAddress;
  uint8_t m_ttl {1};

  struct {
    PingReq  pingReq;
    PingRsp  pingRsp;
    HelloReq helloReq;
    HelloRsp helloRsp;
    DvUpdate dvUpdate;
  } m_message;

public:
  HelloReq GetHelloReq();                    void SetHelloReq(std::string helloMessage);
  HelloRsp GetHelloRsp();                    void SetHelloRsp(Ipv4Address source, std::string helloMessage);
  PingReq  GetPingReq ();                    void SetPingReq (Ipv4Address destinationAddress, std::string message);
  PingRsp  GetPingRsp ();                    void SetPingRsp (Ipv4Address destinationAddress, std::string message);
  DvUpdate GetDvUpdate() const;              void SetDvUpdate(const std::vector<DvVectorItem>& items);
};

static inline std::ostream& operator<< (std::ostream& os, const DVMessage& message)
{
  message.Print (os);
  return os;
}

#endif
