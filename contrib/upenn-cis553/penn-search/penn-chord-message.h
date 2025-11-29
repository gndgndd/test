/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef PENN_CHORD_MESSAGE_H
#define PENN_CHORD_MESSAGE_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include "ns3/object.h"
#include "ns3/packet.h"

using namespace ns3;

#define IPV4_ADDRESS_SIZE 4

class PennChordMessage : public Header
{
  public:
    PennChordMessage ();
    virtual ~PennChordMessage ();

    enum MessageType
    {
      PING_REQ = 1,
      PING_RSP = 2,
      // MS2A Lookup Messages
      LOOKUP_REQ = 3,
      LOOKUP_RSP = 4,
      LOOKUP_FORWARD = 5,
      // MS1 Stabilization Messages
      STABILIZE_REQ = 6,
      STABILIZE_RSP = 7,
      NOTIFY_PKT = 8,
      RINGSTATE_MSG = 9,
      // Data Transfer (Optional/Stub)
      LEAVE_SUCCESSOR = 10,
      LEAVE_PREDECESSOR = 11
    };

    PennChordMessage (PennChordMessage::MessageType messageType, uint32_t transactionId);

    void SetMessageType (MessageType messageType);
    MessageType GetMessageType () const;
    void SetTransactionId (uint32_t transactionId);
    uint32_t GetTransactionId () const;

    static TypeId GetTypeId (void);
    virtual TypeId GetInstanceTypeId (void) const;
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator start) const;
    uint32_t Deserialize (Buffer::Iterator start);

    // --- STRUCT DEFINITIONS ---

    struct PingReq {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        std::string pingMessage;
    };

    struct PingRsp {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        std::string pingMessage;
    };

    struct LookupReq {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        uint32_t lookupKey;
        Ipv4Address originator;
        Ipv4Address lastHop;
    };

    struct LookupForward {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        uint32_t lookupKey;
        Ipv4Address originator;
        Ipv4Address lastHop;
    };

    struct LookupRsp {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        uint32_t lookupKey;
        Ipv4Address ownerNode;
    };

    struct StabilizeReq {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        Ipv4Address senderIp; 
    };

    struct StabilizeRsp {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        Ipv4Address predecessorIp;
    };

    struct NotifyPkt {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        Ipv4Address candidateIp; 
    };

    struct RingstateMsg {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        Ipv4Address initiatorNode;
    };

  private:
    MessageType m_messageType;
    uint32_t m_transactionId;

    // CHANGED FROM UNION TO STRUCT to support std::string members
    struct {
        PingReq pingReq;
        PingRsp pingRsp;
        LookupReq lookupReq;
        LookupForward lookupForward;
        LookupRsp lookupRsp;
        StabilizeReq stabilizeReq;
        StabilizeRsp stabilizeRsp;
        NotifyPkt notifyPkt;
        RingstateMsg ringstateMsg;
    } m_message;

  public:
    // --- ACCESSORS ---

    PingReq GetPingReq ();
    void SetPingReq (std::string message);

    PingRsp GetPingRsp ();
    void SetPingRsp (std::string message);

    LookupReq GetLookupReq ();
    void SetLookupReq (uint32_t key, Ipv4Address origin, Ipv4Address lastHop);

    LookupForward GetLookupForward ();
    void SetLookupForward (uint32_t key, Ipv4Address origin, Ipv4Address lastHop);

    LookupRsp GetLookupRsp ();
    void SetLookupRsp (uint32_t key, Ipv4Address owner);

    StabilizeReq GetStabilizeReq();
    void SetStabilizeReq(Ipv4Address senderIp);

    StabilizeRsp GetStabilizeRsp();
    void SetStabilizeRsp(Ipv4Address predecessorIp);

    NotifyPkt GetNotifyPkt();
    void SetNotifyPkt(Ipv4Address candidateIp);

    RingstateMsg getRingstateMsg();
    void setRingstateMsg(Ipv4Address initiator);

}; 

static inline std::ostream& operator<< (std::ostream& os, const PennChordMessage& message) {
  message.Print (os);
  return os;
}
#endif