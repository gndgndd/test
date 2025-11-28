/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef PENN_CHORD_MESSAGE_H
#define PENN_CHORD_MESSAGE_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include "ns3/packet.h"
#include "ns3/object.h"

using namespace ns3;

class PennChordMessage : public Header
{
public:
    PennChordMessage ();
    PennChordMessage (uint8_t type, uint32_t txId);
    virtual ~PennChordMessage ();

    enum MessageType {
        PING_REQ = 1,
        PING_RSP = 2,
        FIND_SUCCESSOR_REQ = 3,
        FIND_SUCCESSOR_RSP = 4,
        NOTIFY_REQ = 5,
        GET_PREDECESSOR_REQ = 6,
        GET_PREDECESSOR_RSP = 7,
        RING_STATE = 8
    };

    void SetMessageType (uint8_t type);
    uint8_t GetMessageType () const;
    void SetTransactionId (uint32_t txId);
    uint32_t GetTransactionId () const;

    static TypeId GetTypeId (void);
    virtual TypeId GetInstanceTypeId (void) const;
    void Print (std::ostream &os) const;
    uint32_t GetSerializedSize (void) const;
    void Serialize (Buffer::Iterator start) const;
    uint32_t Deserialize (Buffer::Iterator start);

    // --- Message Data Structures ---
    struct PingData {
        std::string msg;
        uint32_t GetSize() const;
        void Serialize(Buffer::Iterator &i) const;
        uint32_t Deserialize(Buffer::Iterator &i);
    };

    struct FindSuccReq {
        uint32_t key;
        bool isApp;
        uint32_t GetSize() const;
        void Serialize(Buffer::Iterator &i) const;
        uint32_t Deserialize(Buffer::Iterator &i);
    };

    struct FindSuccRsp {
        Ipv4Address addr;
        bool isApp;
        uint32_t GetSize() const;
        void Serialize(Buffer::Iterator &i) const;
        uint32_t Deserialize(Buffer::Iterator &i);
    };

    struct AddressPayload {
        Ipv4Address addr;
        uint32_t GetSize() const;
        void Serialize(Buffer::Iterator &i) const;
        uint32_t Deserialize(Buffer::Iterator &i);
    };

    // Accessors
    void SetPingReq(std::string s);
    void SetPingRsp(std::string s);
    
    // Fixed: Accessors return the struct to match usage .GetPingReq().msg
    PingData GetPingReq();
    PingData GetPingRsp();

    void SetFindSuccReq(uint32_t key, bool isApp);
    FindSuccReq GetFindSuccReq();

    void SetFindSuccRsp(Ipv4Address addr, bool isApp);
    FindSuccRsp GetFindSuccRsp();

    void SetNotifyReq(Ipv4Address addr);
    AddressPayload GetNotifyReq();

    void SetGetPredRsp(Ipv4Address addr);
    AddressPayload GetGetPredRsp();

private:
    uint8_t m_type;
    uint32_t m_txId;

    PingData m_ping;
    FindSuccReq m_findSuccReq;
    FindSuccRsp m_findSuccRsp;
    AddressPayload m_addrPayload; 
};
#endif