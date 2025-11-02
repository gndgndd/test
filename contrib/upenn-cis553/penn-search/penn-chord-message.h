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
      PING_REQ      = 1,
      PING_RSP      = 2,
      JOIN_FIND     = 3,   // find successor for a joining node (greedy forwarding)
      JOIN_REPLY    = 4,   // reply with successor
      STAB_REQ      = 5,   // stabilize request (ask successor for its predecessor)
      STAB_RSP      = 6,   // stabilize response (predecessor of successor)
      NOTIFY        = 7,   // notify(candidate) per Chord
      RINGSTATE     = 8    // ringstate traversal (carries origin)
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

    /* ----------- Existing PING payloads ----------- */
    struct PingReq
    {
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);
      std::string pingMessage;
    };

    struct PingRsp
    {
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);
      std::string pingMessage;
    };

    /* ----------- New MS1 payloads ----------- */
    struct JoinFind
    {
      // find successor for "joiner"
      Ipv4Address joiner;
      uint32_t joinerHash;    // 32-bit hash for ordering
      Ipv4Address origin;     // for tracing (first hop), not strictly required

      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);
    };

    struct JoinReply
    {
      Ipv4Address successor;  // successor of the joiner (where it should attach)
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);
    };

    struct StabilizeReq
    {
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);
    };

    struct StabilizeRsp
    {
      Ipv4Address predecessor; // predecessor of the node who replied
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);
    };

    struct NotifyMsg
    {
      Ipv4Address candidate;  // candidate predecessor (sender)
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);
    };

    struct RingStateMsg
    {
      Ipv4Address origin;     // the node that initiated traversal
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);
    };

  private:
    struct
    {
      PingReq      pingReq;
      PingRsp      pingRsp;
      JoinFind     joinFind;
      JoinReply    joinReply;
      StabilizeReq stabReq;
      StabilizeRsp stabRsp;
      NotifyMsg    notify;
      RingStateMsg ringstate;
    } m_message;

  public:
    /* Accessors/Mutators for existing PING types */
    PingReq GetPingReq ();
    void SetPingReq (std::string message);

    PingRsp GetPingRsp ();
    void SetPingRsp (std::string message);

    /* Accessors/Mutators for new types */
    JoinFind GetJoinFind ();
    void SetJoinFind (Ipv4Address joiner, uint32_t joinerHash, Ipv4Address origin);

    JoinReply GetJoinReply ();
    void SetJoinReply (Ipv4Address successor);

    StabilizeReq GetStabilizeReq ();
    void SetStabilizeReq ();

    StabilizeRsp GetStabilizeRsp ();
    void SetStabilizeRsp (Ipv4Address predecessor);

    NotifyMsg GetNotify ();
    void SetNotify (Ipv4Address candidate);

    RingStateMsg GetRingState ();
    void SetRingState (Ipv4Address origin);

}; // class PennChordMessage

static inline std::ostream& operator<< (std::ostream& os, const PennChordMessage& message)
{
  message.Print (os);
  return os;
}

#endif
