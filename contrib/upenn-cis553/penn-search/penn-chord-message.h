/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef PENN_CHORD_MESSAGE_H
#define PENN_CHORD_MESSAGE_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include <string>

using namespace ns3;

class PennChordMessage : public Header
{
public:
  enum MessageType {
    PING_REQ = 1,
    PING_RSP = 2,
    LOOKUP_REQ = 3,
    LOOKUP_FORWARD = 4,
    LOOKUP_RSP = 5,
    // New types for MS1/MS2 Distributed Logic
    GET_PREDECESSOR = 6,
    PREDECESSOR_RSP = 7,
    NOTIFY = 8,
    RINGSTATE_REQ = 9
  };

  PennChordMessage ();
  PennChordMessage (MessageType type, uint32_t transactionId);
  virtual ~PennChordMessage ();

  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (std::ostream &os) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);
  virtual uint32_t GetSerializedSize (void) const;

  MessageType GetMessageType () const;
  uint32_t GetTransactionId () const;

  // Structs for payloads
  struct PingReq { std::string pingMessage; };
  struct PingRsp { std::string pingMessage; };
  struct LookupReq { uint32_t lookupKey; Ipv4Address originator; Ipv4Address lastHop; };
  struct LookupForward { uint32_t lookupKey; Ipv4Address originator; Ipv4Address lastHop; };
  struct LookupRsp { uint32_t lookupKey; Ipv4Address ownerNode; };
  
  // Stabilization payloads
  struct PredecessorRsp { Ipv4Address predecessorNode; };
  struct NotifyReq { Ipv4Address potentialPredecessor; };
  
  // Ringstate payload
  struct RingStateReq { Ipv4Address originNode; };

  // Setters
  void SetPingReq (std::string msg);
  void SetPingRsp (std::string msg);
  void SetLookupReq (uint32_t key, Ipv4Address origin, Ipv4Address hop);
  void SetLookupForward (uint32_t key, Ipv4Address origin, Ipv4Address hop);
  void SetLookupRsp (uint32_t key, Ipv4Address owner);
  void SetPredecessorRsp (Ipv4Address pred);
  void SetNotify (Ipv4Address potPred);
  void SetRingStateReq (Ipv4Address origin);

  // Getters
  PingReq GetPingReq () const;
  PingRsp GetPingRsp () const;
  LookupReq GetLookupReq () const;
  LookupForward GetLookupForward () const;
  LookupRsp GetLookupRsp () const;
  PredecessorRsp GetPredecessorRsp () const;
  NotifyReq GetNotify () const;
  RingStateReq GetRingStateReq () const;

private:
  MessageType m_type;
  uint32_t m_transactionId;

  // Payload storage
  PingReq m_pingReq;
  PingRsp m_pingRsp;
  LookupReq m_lookupReq;
  LookupForward m_lookupFwd;
  LookupRsp m_lookupRsp;
  PredecessorRsp m_predRsp;
  NotifyReq m_notify;
  RingStateReq m_ringState;
};

#endif