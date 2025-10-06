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
  DVMessage();
  ~DVMessage() override;

  enum MessageType {
    PING_REQ = 1,
    PING_RSP = 2,
    HELLO_REQ,
    HELLO_RSP,
    DV_UPDATE        // advertises vector of {dest,cost}
  };

  DVMessage (MessageType type, uint32_t seq, uint8_t ttl, Ipv4Address origin);

  // common header fields
  void         SetMessageType (MessageType t);
  MessageType  GetMessageType () const;
  void         SetSequenceNumber (uint32_t s);
  uint32_t     GetSequenceNumber () const;
  void         SetOriginatorAddress (Ipv4Address a);
  Ipv4Address  GetOriginatorAddress () const;
  void         SetTTL (uint8_t ttl);
  uint8_t      GetTTL () const;

  // ns-3 Header plumbing
  static TypeId GetTypeId (void);
  TypeId GetInstanceTypeId (void) const override;
  void Print (std::ostream &os) const override;
  uint32_t GetSerializedSize () const override;
  void Serialize (Buffer::Iterator start) const override;
  uint32_t Deserialize (Buffer::Iterator start) override;

  // payload types
  struct HelloReq {
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize () const;
    void Serialize (Buffer::Iterator &start) const;
    uint32_t Deserialize (Buffer::Iterator &start);
    std::string helloMessage;
  };

  struct HelloRsp {
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize () const;
    void Serialize (Buffer::Iterator &start) const;
    uint32_t Deserialize (Buffer::Iterator &start);
    Ipv4Address sourceAddress;
    std::string helloMessage;
  };

  struct PingReq {
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize () const;
    void Serialize (Buffer::Iterator &start) const;
    uint32_t Deserialize (Buffer::Iterator &start);
    Ipv4Address destinationAddress;
    std::string pingMessage;
  };

  struct PingRsp {
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize () const;
    void Serialize (Buffer::Iterator &start) const;
    uint32_t Deserialize (Buffer::Iterator &start);
    Ipv4Address destinationAddress;
    std::string pingMessage;
  };

  // MS2 DV payloads
  struct DvVectorItem { Ipv4Address dest; uint32_t cost; };
  struct DvUpdate {
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize () const;
    void Serialize (Buffer::Iterator &start) const;
    uint32_t Deserialize (Buffer::Iterator &start);
    std::vector<DvVectorItem> vec;
  };

  // payload accessors/mutators
  HelloReq GetHelloReq();
  void     SetHelloReq(std::string hello);
  HelloRsp GetHelloRsp();
  void     SetHelloRsp(Ipv4Address sourceAddress, std::string hello);

  PingReq  GetPingReq();
  void     SetPingReq (Ipv4Address dst, std::string msg);
  PingRsp  GetPingRsp();
  void     SetPingRsp (Ipv4Address dst, std::string msg);

  DvUpdate GetDvUpdate() const;
  void     SetDvUpdate(const std::vector<DvVectorItem>& items);

private:
  MessageType m_messageType{PING_REQ};
  uint32_t    m_sequenceNumber{0};
  Ipv4Address m_originatorAddress;
  uint8_t     m_ttl{1};

  struct {
    PingReq  pingReq;
    PingRsp  pingRsp;
    HelloReq helloReq;
    HelloRsp helloRsp;
    DvUpdate dvUpdate;
  } m_message;
};

static inline std::ostream& operator<<(std::ostream& os, const DVMessage& m) { m.Print(os); return os; }

#endif
