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
      PING_REQ = 1,
      PING_RSP = 2,
      // Define extra message types when needed
      FIND_SUCCESSOR_REQ = 3,
      FIND_SUCCESSOR_RSP = 4,
      STABILIZE_REQ = 5,
      STABILIZE_RSP = 6,
      NOTIFY_PKT = 7,
      RINGSTATE_PKT = 8,
      LEAVE_SUCCESSOR = 9,
      LEAVE_PREDECESSOR = 10,
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
    bool m_isLookup;  // true if this is a lookup message
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

    // isLookup getter and setter
    void SetIsLookup (bool isLookup);
    bool GetIsLookup () const;
    
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

    struct FindSuccessorReq
    {
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);

      uint32_t idToFind;
      Ipv4Address requestorIp;
    };

    struct FindSuccessorRsp
    {
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);

      Ipv4Address successorIp;
    };

    struct StabilizeReq
    {
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);

      Ipv4Address sender;
      Ipv4Address receiver;
    };

    struct StabilizeRsp
    {
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);

      Ipv4Address sender;
    };

    struct NotifyPkt
    {
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);

      Ipv4Address newPredecessor;
    };

    struct RingstatePkt
    {
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);

      Ipv4Address endRingState;
    };

    struct LeaveSuccessor
    {
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);

      Ipv4Address sender; 
      Ipv4Address newPred;
    };

    struct LeavePredecessor
    {
      void Print (std::ostream &os) const;
      uint32_t GetSerializedSize (void) const;
      void Serialize (Buffer::Iterator &start) const;
      uint32_t Deserialize (Buffer::Iterator &start);

      Ipv4Address sender; 
      Ipv4Address newSucc;
    };

  private:
    struct
      {
        PingReq pingReq;
        PingRsp pingRsp;
        FindSuccessorReq findSuccessorReq;
        FindSuccessorRsp findSuccessorRsp;
        StabilizeReq stabilizeReq;
        StabilizeRsp stabilizeRsp;
        NotifyPkt notifyPkt;
        RingstatePkt ringStatePkt;
        LeaveSuccessor leaveSuccessor;
        LeavePredecessor leavePrededecessor;
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

    void SetFindSuccessorReq(uint32_t idToFind, Ipv4Address requestorIp);
    FindSuccessorReq GetFindSuccessorReq();

    void SetFindSuccessorRsp(Ipv4Address successorIp);
    FindSuccessorRsp GetFindSuccessorRsp();

    void SetStabilizeReq(Ipv4Address sender, Ipv4Address recieiver);
    StabilizeReq GetStabilizeReq();

    void SetStabilizeRsp(Ipv4Address sender);
    StabilizeRsp GetStabilizeRsp();

    void SetNotifyPkt(Ipv4Address newPredecessor);
    NotifyPkt GetNotifyPkt();

    void SetRingstatePkt(Ipv4Address endRingState);
    RingstatePkt GetRingstatePkt();

    void SetLeaveSuccessor(Ipv4Address sender, Ipv4Address newPred);
    LeaveSuccessor GetLeaveSuccessor();
    
    void SetLeavePredecessor(Ipv4Address sender, Ipv4Address newSucc);
    LeavePredecessor GetLeavePredecessor();


}; // class PennChordMessage

static inline std::ostream& operator<< (std::ostream& os, const PennChordMessage& message)
{
  message.Print (os);
  return os;
}

#endif
