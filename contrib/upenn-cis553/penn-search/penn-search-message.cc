/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/penn-search-message.h"
#include "ns3/log.h"
#include "ns3/address-utils.h"

using namespace ns3;
NS_LOG_COMPONENT_DEFINE ("PennSearchMessage");
NS_OBJECT_ENSURE_REGISTERED (PennSearchMessage);

PennSearchMessage::PennSearchMessage () : m_type(0), m_txId(0) {}
PennSearchMessage::PennSearchMessage (uint8_t t, uint32_t id) : m_type(t), m_txId(id) {}
PennSearchMessage::~PennSearchMessage () {}

TypeId PennSearchMessage::GetTypeId () { return TypeId ("PennSearchMessage").SetParent<Header>().AddConstructor<PennSearchMessage>(); }
TypeId PennSearchMessage::GetInstanceTypeId () const { return GetTypeId(); }

void PennSearchMessage::SetMessageType(uint8_t t) { m_type = t; }
uint8_t PennSearchMessage::GetMessageType() const { return m_type; }
void PennSearchMessage::SetTransactionId(uint32_t i) { m_txId = i; }
uint32_t PennSearchMessage::GetTransactionId() const { return m_txId; }

// --- Helpers ---
void WriteStr(Buffer::Iterator &i, const std::string &s) { i.WriteU16(s.length()); i.Write((const uint8_t*)s.c_str(), s.length()); }
std::string ReadStr(Buffer::Iterator &i) { uint16_t l=i.ReadU16(); char* b=new char[l]; i.Read((uint8_t*)b,l); std::string s(b,l); delete[] b; return s; }

// Payload Impl
uint32_t PennSearchMessage::PingData::GetSize() const { return 2+msg.length(); }
void PennSearchMessage::PingData::Serialize(Buffer::Iterator &i) const { WriteStr(i,msg); }
uint32_t PennSearchMessage::PingData::Deserialize(Buffer::Iterator &i) { msg=ReadStr(i); return GetSize(); }

uint32_t PennSearchMessage::PublishData::GetSize() const { return 4+k.length()+v.length(); }
void PennSearchMessage::PublishData::Serialize(Buffer::Iterator &i) const { WriteStr(i,k); WriteStr(i,v); }
uint32_t PennSearchMessage::PublishData::Deserialize(Buffer::Iterator &i) { k=ReadStr(i); v=ReadStr(i); return GetSize(); }

uint32_t PennSearchMessage::SearchReq::GetSize() const { 
    uint32_t sz = 8; // IP+Idx
    sz += 4; for(auto &s : terms) sz += 2+s.length();
    sz += 4; for(auto &s : docs) sz += 2+s.length();
    return sz; 
}
void PennSearchMessage::SearchReq::Serialize(Buffer::Iterator &i) const {
    i.WriteHtonU32(src.Get()); i.WriteHtonU32(idx);
    i.WriteU32(terms.size()); for(auto &s:terms) WriteStr(i,s);
    i.WriteU32(docs.size()); for(auto &s:docs) WriteStr(i,s);
}
uint32_t PennSearchMessage::SearchReq::Deserialize(Buffer::Iterator &i) {
    src.Set(i.ReadNtohU32()); idx = i.ReadNtohU32();
    uint32_t c = i.ReadU32(); terms.clear(); while(c--) terms.push_back(ReadStr(i));
    c = i.ReadU32(); docs.clear(); while(c--) docs.push_back(ReadStr(i));
    return GetSize();
}

uint32_t PennSearchMessage::SearchRsp::GetSize() const {
    uint32_t sz = 4; for(auto &s : results) sz += 2+s.length(); return sz;
}
void PennSearchMessage::SearchRsp::Serialize(Buffer::Iterator &i) const {
    i.WriteU32(results.size()); for(auto &s:results) WriteStr(i,s);
}
uint32_t PennSearchMessage::SearchRsp::Deserialize(Buffer::Iterator &i) {
    uint32_t c = i.ReadU32(); results.clear(); while(c--) results.push_back(ReadStr(i));
    return GetSize();
}

// Serialization
uint32_t PennSearchMessage::GetSerializedSize() const {
    uint32_t sz = 5;
    switch(m_type) {
        case PING_REQ: case PING_RSP: sz += m_ping.GetSize(); break;
        case PUBLISH_REQ: sz += m_pub.GetSize(); break;
        case SEARCH_REQ: sz += m_searchReq.GetSize(); break;
        case SEARCH_RSP: sz += m_searchRsp.GetSize(); break;
    }
    return sz;
}
void PennSearchMessage::Serialize(Buffer::Iterator start) const {
    start.WriteU8(m_type); start.WriteHtonU32(m_txId);
    switch(m_type) {
        case PING_REQ: case PING_RSP: m_ping.Serialize(start); break;
        case PUBLISH_REQ: m_pub.Serialize(start); break;
        case SEARCH_REQ: m_searchReq.Serialize(start); break;
        case SEARCH_RSP: m_searchRsp.Serialize(start); break;
    }
}
uint32_t PennSearchMessage::Deserialize(Buffer::Iterator start) {
    m_type = start.ReadU8(); m_txId = start.ReadNtohU32();
    uint32_t sz = 5;
    switch(m_type) {
        case PING_REQ: case PING_RSP: sz += m_ping.Deserialize(start); break;
        case PUBLISH_REQ: sz += m_pub.Deserialize(start); break;
        case SEARCH_REQ: sz += m_searchReq.Deserialize(start); break;
        case SEARCH_RSP: sz += m_searchRsp.Deserialize(start); break;
    }
    return sz;
}
void PennSearchMessage::Print(std::ostream &os) const { os << "Type " << (int)m_type; }

// Wrappers
void PennSearchMessage::SetPingReq(std::string s) { m_type=PING_REQ; m_ping.msg=s; }
PennSearchMessage::PingData PennSearchMessage::GetPingReq() { return m_ping; }
void PennSearchMessage::SetPingRsp(std::string s) { m_type=PING_RSP; m_ping.msg=s; }
PennSearchMessage::PingData PennSearchMessage::GetPingRsp() { return m_ping; }
void PennSearchMessage::SetPublishReq(std::string k, std::string v) { m_type=PUBLISH_REQ; m_pub.k=k; m_pub.v=v; }
PennSearchMessage::PublishData PennSearchMessage::GetPublishReq() { return m_pub; }
void PennSearchMessage::SetSearchReq(Ipv4Address ip, std::vector<std::string> t, uint32_t i, std::vector<std::string> d) {
    m_type=SEARCH_REQ; m_searchReq.src=ip; m_searchReq.terms=t; m_searchReq.idx=i; m_searchReq.docs=d;
}
PennSearchMessage::SearchReq PennSearchMessage::GetSearchReq() { return m_searchReq; }
void PennSearchMessage::SetSearchRsp(std::vector<std::string> r) { m_type=SEARCH_RSP; m_searchRsp.results=r; }
PennSearchMessage::SearchRsp PennSearchMessage::GetSearchRsp() { return m_searchRsp; }