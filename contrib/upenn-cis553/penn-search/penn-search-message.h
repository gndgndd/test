/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef PENN_SEARCH_MESSAGE_H
#define PENN_SEARCH_MESSAGE_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include "ns3/packet.h"
#include <vector>

using namespace ns3;

class PennSearchMessage : public Header
{
public:
    PennSearchMessage ();
    PennSearchMessage (uint8_t type, uint32_t txId);
    virtual ~PennSearchMessage ();

    enum MessageType {
        PING_REQ = 1, PING_RSP = 2,
        PUBLISH_REQ = 3, PUBLISH_RSP = 4,
        SEARCH_REQ = 5, SEARCH_RSP = 6
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

    // Payload Structures
    struct PingData { std::string msg; uint32_t GetSize() const; void Serialize(Buffer::Iterator &i) const; uint32_t Deserialize(Buffer::Iterator &i); };
    struct PublishData { std::string k, v; uint32_t GetSize() const; void Serialize(Buffer::Iterator &i) const; uint32_t Deserialize(Buffer::Iterator &i); };
    struct SearchReq { 
        Ipv4Address src; std::vector<std::string> terms; uint32_t idx; std::vector<std::string> docs; 
        uint32_t GetSize() const; void Serialize(Buffer::Iterator &i) const; uint32_t Deserialize(Buffer::Iterator &i); 
    };
    struct SearchRsp { std::vector<std::string> results; uint32_t GetSize() const; void Serialize(Buffer::Iterator &i) const; uint32_t Deserialize(Buffer::Iterator &i); };

    // Setters/Getters
    void SetPingReq(std::string s); PingData GetPingReq();
    void SetPingRsp(std::string s); PingData GetPingRsp();
    void SetPublishReq(std::string k, std::string v); PublishData GetPublishReq();
    void SetSearchReq(Ipv4Address ip, std::vector<std::string> t, uint32_t i, std::vector<std::string> d); SearchReq GetSearchReq();
    void SetSearchRsp(std::vector<std::string> r); SearchRsp GetSearchRsp();

private:
    uint8_t m_type;
    uint32_t m_txId;
    
    PingData m_ping;
    PublishData m_pub;
    SearchReq m_searchReq;
    SearchRsp m_searchRsp;
};
#endif