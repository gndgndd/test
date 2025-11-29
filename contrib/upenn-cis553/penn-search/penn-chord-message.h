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
      LOOKUP_REQ = 3,
      LOOKUP_FORWARD = 4,
      LOOKUP_RSP = 5,
      RINGSTATE_MSG = 6,
      STABILIZE_REQ = 7,
      STABILIZE_RSP = 8,
      NOTIFY_MSG = 9,
      // NEW: Message to force a successor update (Immediate Patch)
      SET_SUCC_REQ = 10 
    };

    PennChordMessage (PennChordMessage::MessageType messageType, uint32_t transactionId);
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

    struct RingstateMsg {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        Ipv4Address initiatorNode;
    };

    struct StabilizeReq {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        Ipv4Address requestingNode;
    };

    struct StabilizeRsp {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        Ipv4Address responsePredessor;
    };

    struct NotifyMsg {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        Ipv4Address potentialPredessor;
    };

    // NEW: Set Successor Payload
    struct SetSuccReq {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        Ipv4Address newSuccessor;
    };

  private:
    struct {
        PingReq pingReq;
        PingRsp pingRsp;
        LookupReq lookupReq;
        LookupForward lookupForward;
        LookupRsp lookupRsp;
        RingstateMsg ringstateMsg;
        StabilizeReq stabilizeReq;
        StabilizeRsp stabilizeRsp;
        NotifyMsg notifyMsg;
        SetSuccReq setSuccReq; // NEW
    } m_message;

  public:
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
    RingstateMsg GetRingstateMsg();
    void SetRingstateMsg(Ipv4Address initiator);
    StabilizeReq GetStabilizeReq();
    void SetStabilizeReq (Ipv4Address requestor);
    StabilizeRsp GetStabilizeRsp();
    void SetStabilizeRsp (Ipv4Address predessor);
    NotifyMsg GetNotifyMsg();
    void SetNotifyMsg (Ipv4Address potPredessor);
    
    // NEW Accessors
    SetSuccReq GetSetSuccReq();
    void SetSetSuccReq(Ipv4Address newSucc);
};

static inline std::ostream& operator<< (std::ostream& os, const PennChordMessage& message)
{
  message.Print (os);
  return os;
}

#endif