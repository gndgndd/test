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
      // MS2 Refactor: Distributed Control Messages
      GET_PREDECESSOR_REQ = 6,
      GET_PREDECESSOR_RSP = 7,
      NOTIFY_REQ = 8,
      RINGSTATE_REQ = 9
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

    // --- Payload Structures ---

    struct PingReq {
        std::string pingMessage;
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
    };

    struct PingRsp {
        std::string pingMessage;
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
    };

    struct LookupReq {
        uint32_t lookupKey;
        Ipv4Address originator;
        Ipv4Address lastHop;
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
    };

    struct LookupForward {
        uint32_t lookupKey;
        Ipv4Address originator;
        Ipv4Address lastHop;
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
    };

    struct LookupRsp {
        uint32_t lookupKey;
        Ipv4Address ownerNode;
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
    };

    // -- New Structures for Distributed Logic --

    // Empty payload, just the header is enough
    struct GetPredecessorReq {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
    };

    struct GetPredecessorRsp {
        Ipv4Address predecessor; // The predecessor of the node being asked
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
    };

    struct NotifyReq {
        Ipv4Address potentialPredecessor; // The node asserting itself as predecessor
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
    };

    struct RingStateReq {
        Ipv4Address initiator; // The node that started the ringstate command
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
    };

  private:
    MessageType m_messageType;
    uint32_t m_transactionId;

    struct
      {
        PingReq pingReq;
        PingRsp pingRsp;
        LookupReq lookupReq;
        LookupForward lookupForward;
        LookupRsp lookupRsp;
        // New payloads
        GetPredecessorReq getPredReq;
        GetPredecessorRsp getPredRsp;
        NotifyReq notifyReq;
        RingStateReq ringStateReq;
      } m_message;

  public:
    // Accessors
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

    // New Accessors
    GetPredecessorReq GetGetPredecessorReq ();
    void SetGetPredecessorReq ();

    GetPredecessorRsp GetGetPredecessorRsp ();
    void SetGetPredecessorRsp (Ipv4Address pred);

    NotifyReq GetNotifyReq ();
    void SetNotifyReq (Ipv4Address potentialPred);

    RingStateReq GetRingStateReq ();
    void SetRingStateReq (Ipv4Address initiator);

}; 

static inline std::ostream& operator<< (std::ostream& os, const PennChordMessage& message)
{
  message.Print (os);
  return os;
}

#endif