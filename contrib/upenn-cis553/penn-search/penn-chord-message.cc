/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/penn-chord-message.h"
#include "ns3/log.h"
#include "ns3/address-utils.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("PennChordMessage");
NS_OBJECT_ENSURE_REGISTERED (PennChordMessage);

PennChordMessage::PennChordMessage() : m_type(0), m_txId(0) {}
PennChordMessage::PennChordMessage(uint8_t type, uint32_t txId) : m_type(type), m_txId(txId) {}
PennChordMessage::~PennChordMessage() {}

TypeId PennChordMessage::GetTypeId(void) {
    static TypeId tid = TypeId("PennChordMessage")
        .SetParent<Header>()
        .AddConstructor<PennChordMessage>();
    return tid;
}

TypeId PennChordMessage::GetInstanceTypeId(void) const { return GetTypeId(); }
void PennChordMessage::SetMessageType(uint8_t type) { m_type = type; }
uint8_t PennChordMessage::GetMessageType() const { return m_type; }
void PennChordMessage::SetTransactionId(uint32_t txId) { m_txId = txId; }
uint32_t PennChordMessage::GetTransactionId() const { return m_txId; }

uint32_t PennChordMessage::GetSerializedSize(void) const {
    uint32_t sz = 5; 
    switch(m_type) {
        case PING_REQ: case PING_RSP: sz += m_ping.GetSize(); break;
        case FIND_SUCCESSOR_REQ: sz += m_findSuccReq.GetSize(); break;
        case FIND_SUCCESSOR_RSP: sz += m_findSuccRsp.GetSize(); break;
        case NOTIFY_REQ: case GET_PREDECESSOR_RSP: sz += m_addrPayload.GetSize(); break;
        default: break;
    }
    return sz;
}

void PennChordMessage::Serialize(Buffer::Iterator start) const {
    start.WriteU8(m_type);
    start.WriteHtonU32(m_txId);
    switch(m_type) {
        case PING_REQ: case PING_RSP: m_ping.Serialize(start); break;
        case FIND_SUCCESSOR_REQ: m_findSuccReq.Serialize(start); break;
        case FIND_SUCCESSOR_RSP: m_findSuccRsp.Serialize(start); break;
        case NOTIFY_REQ: case GET_PREDECESSOR_RSP: m_addrPayload.Serialize(start); break;
        default: break;
    }
}

uint32_t PennChordMessage::Deserialize(Buffer::Iterator start) {
    m_type = start.ReadU8();
    m_txId = start.ReadNtohU32();
    uint32_t sz = 5;
    switch(m_type) {
        case PING_REQ: case PING_RSP: sz += m_ping.Deserialize(start); break;
        case FIND_SUCCESSOR_REQ: sz += m_findSuccReq.Deserialize(start); break;
        case FIND_SUCCESSOR_RSP: sz += m_findSuccRsp.Deserialize(start); break;
        case NOTIFY_REQ: case GET_PREDECESSOR_RSP: sz += m_addrPayload.Deserialize(start); break;
        default: break;
    }
    return sz;
}

void PennChordMessage::Print(std::ostream &os) const {
    os << "MsgType=" << (int)m_type << " Tx=" << m_txId;
}

// --- Payload Implementation ---

uint32_t PennChordMessage::PingData::GetSize() const { return 2 + msg.length(); }
void PennChordMessage::PingData::Serialize(Buffer::Iterator &i) const {
    i.WriteU16(msg.length());
    i.Write((const uint8_t*)msg.c_str(), msg.length());
}
uint32_t PennChordMessage::PingData::Deserialize(Buffer::Iterator &i) {
    uint16_t len = i.ReadU16();
    char* buf = new char[len];
    i.Read((uint8_t*)buf, len);
    msg.assign(buf, len);
    delete[] buf;
    return GetSize();
}

uint32_t PennChordMessage::FindSuccReq::GetSize() const { return 5; }
void PennChordMessage::FindSuccReq::Serialize(Buffer::Iterator &i) const { i.WriteHtonU32(key); i.WriteU8(isApp); }
uint32_t PennChordMessage::FindSuccReq::Deserialize(Buffer::Iterator &i) { key = i.ReadNtohU32(); isApp = i.ReadU8(); return 5; }

uint32_t PennChordMessage::FindSuccRsp::GetSize() const { return 5; }
void PennChordMessage::FindSuccRsp::Serialize(Buffer::Iterator &i) const { i.WriteHtonU32(addr.Get()); i.WriteU8(isApp); }
uint32_t PennChordMessage::FindSuccRsp::Deserialize(Buffer::Iterator &i) { addr.Set(i.ReadNtohU32()); isApp = i.ReadU8(); return 5; }

uint32_t PennChordMessage::AddressPayload::GetSize() const { return 4; }
void PennChordMessage::AddressPayload::Serialize(Buffer::Iterator &i) const { i.WriteHtonU32(addr.Get()); }
uint32_t PennChordMessage::AddressPayload::Deserialize(Buffer::Iterator &i) { addr.Set(i.ReadNtohU32()); return 4; }

// Wrappers
void PennChordMessage::SetPingReq(std::string s) { m_type=PING_REQ; m_ping.msg=s; }
PennChordMessage::PingData PennChordMessage::GetPingReq() { return m_ping; }

void PennChordMessage::SetPingRsp(std::string s) { m_type=PING_RSP; m_ping.msg=s; }
PennChordMessage::PingData PennChordMessage::GetPingRsp() { return m_ping; }

void PennChordMessage::SetFindSuccReq(uint32_t k, bool a) { m_type=FIND_SUCCESSOR_REQ; m_findSuccReq.key=k; m_findSuccReq.isApp=a; }
PennChordMessage::FindSuccReq PennChordMessage::GetFindSuccReq() { return m_findSuccReq; }

void PennChordMessage::SetFindSuccRsp(Ipv4Address a, bool b) { m_type=FIND_SUCCESSOR_RSP; m_findSuccRsp.addr=a; m_findSuccRsp.isApp=b; }
PennChordMessage::FindSuccRsp PennChordMessage::GetFindSuccRsp() { return m_findSuccRsp; }

void PennChordMessage::SetNotifyReq(Ipv4Address a) { m_type=NOTIFY_REQ; m_addrPayload.addr=a; }
PennChordMessage::AddressPayload PennChordMessage::GetNotifyReq() { return m_addrPayload; }

void PennChordMessage::SetGetPredRsp(Ipv4Address a) { m_type=GET_PREDECESSOR_RSP; m_addrPayload.addr=a; }
PennChordMessage::AddressPayload PennChordMessage::GetGetPredRsp() { return m_addrPayload; }