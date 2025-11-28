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
      PING_REQ       = 1,
      PING_RSP       = 2,
      // *** MS2A ADDITIONS (Chord Lookup Messages) ***
      LOOKUP_REQ     = 3,   // Lookup request for a key
      LOOKUP_FORWARD = 4,   // Forwards lookup along ring (needed for autograder)
      LOOKUP_RSP     = 5,   // Lookup result returning to requester

      // *** MS2B – Ring maintenance and stabilization messages ***
      RINGSTATE_MSG  = 6,   // Circulates ringstate request around ring
      STABILIZE_REQ  = 7,   // Ask successor for its predecessor
      STABILIZE_RSP  = 8,   // Return predecessor to stabilizing node
      NOTIFY_MSG     = 9    // Notify successor of a new potential predecessor
    };

    PennChordMessage (PennChordMessage::MessageType messageType, uint32_t transactionId);

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
     *  \brief Sets Transaction Id
     *  \param transactionId Transaction Id of the request
     */
    void SetTransactionId (uint32_t transactionId);

    /**
     *  \returns Transaction Id
     */
    uint32_t GetTransactionId () const;

  private:
    /**
     *  \cond
     */
    MessageType m_messageType;
    uint32_t m_transactionId;
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

    struct PingReq
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        // Payload
        std::string pingMessage;
      };

    struct PingRsp
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize (void) const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);
        // Payload
        std::string pingMessage;
      };

    // MS2A Lookup Payload Structures
    struct LookupReq
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);

        uint32_t   lookupKey;   // Hashed key
        Ipv4Address originator; // Node that initiated lookup (for result return)
        Ipv4Address lastHop;    // Previous hop (for hop count logs)
      };

    struct LookupForward
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);

        uint32_t    lookupKey;
        Ipv4Address originator;
        Ipv4Address lastHop;
      };

    struct LookupRsp
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &start) const;
        uint32_t Deserialize (Buffer::Iterator &start);

        uint32_t    lookupKey;
        Ipv4Address ownerNode;  // Node that owns the key
      };

    // MS2B – Ringstate payload
    struct RingState
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &i) const;
        uint32_t Deserialize (Buffer::Iterator &i);

        Ipv4Address initiator;  // Node that started the RINGSTATE
      };

    // MS2B – Stabilize request (no payload needed)
    struct StabilizeReq
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &i) const;
        uint32_t Deserialize (Buffer::Iterator &i);
      };

    // MS2B – Stabilize response (carries predecessor)
    struct StabilizeRsp
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &i) const;
        uint32_t Deserialize (Buffer::Iterator &i);

        Ipv4Address predecessor;
      };

    // MS2B – Notify payload
    struct Notify
      {
        void Print (std::ostream &os) const;
        uint32_t GetSerializedSize () const;
        void Serialize (Buffer::Iterator &i) const;
        uint32_t Deserialize (Buffer::Iterator &i);

        Ipv4Address potentialPred;
      };

  private:
    struct
      {
        PingReq       pingReq;
        PingRsp       pingRsp;
        // MS2A structures
        LookupReq     lookupReq;
        LookupForward lookupForward;
        LookupRsp     lookupRsp;
        // MS2B ring maintenance structures
        RingState     ringState;
        StabilizeReq  stabilizeReq;
        StabilizeRsp  stabilizeRsp;
        Notify        notify;
      } m_message;

  public:
    /**
     *  \returns PingReq Struct
     */
    PingReq GetPingReq ();

    /**
     *  \brief Sets PingReq message params
     *  \param message Payload String
     */
    void SetPingReq (std::string message);

    /**
     * \returns PingRsp Struct
     */
    PingRsp GetPingRsp ();
    /**
     *  \brief Sets PingRsp message params
     *  \param message Payload String
     */
    void SetPingRsp (std::string message);

    // MS2A: Lookup Accessors

    LookupReq GetLookupReq ();
    void SetLookupReq (uint32_t key, Ipv4Address origin, Ipv4Address lastHop);
    LookupForward GetLookupForward ();
    void SetLookupForward (uint32_t key, Ipv4Address origin, Ipv4Address lastHop);
    LookupRsp GetLookupRsp ();
    void SetLookupRsp (uint32_t key, Ipv4Address owner);

    // MS2B: Ring maintenance accessors
    RingState GetRingState ();
    void SetRingState (Ipv4Address initiator);

    StabilizeReq GetStabilizeReq ();
    void SetStabilizeReq ();

    StabilizeRsp GetStabilizeRsp ();
    void SetStabilizeRsp (Ipv4Address predecessor);

    Notify GetNotify ();
    void SetNotify (Ipv4Address potentialPred);

}; // class PennChordMessage

static inline std::ostream& operator<< (std::ostream& os, const PennChordMessage& message)
{
  message.Print (os);
  return os;
}

#endif
