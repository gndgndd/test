/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef PENN_SEARCH_H
#define PENN_SEARCH_H

#include "ns3/penn-application.h"
#include "ns3/penn-chord.h"
#include "ns3/penn-search-message.h"
#include "ns3/ping-request.h"
#include "ns3/ipv4-address.h"
#include <map>
#include <vector>
#include <string>
#include "ns3/socket.h"
#include "ns3/timer.h"

using namespace ns3;

class PennSearch : public PennApplication
{
  public:
    static TypeId GetTypeId (void);
    PennSearch ();
    virtual ~PennSearch ();

    // Standard Skeleton Methods
    void SendPing (std::string nodeId, std::string pingMessage);
    void SendPennSearchPing (Ipv4Address destAddress, std::string pingMessage);
    void RecvMessage (Ptr<Socket> socket);
    void ProcessPingReq (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void ProcessPingRsp (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void AuditPings ();
    uint32_t GetNextTransactionId ();

    // Callbacks
    void HandleChordPingSuccess (Ipv4Address destAddress, std::string message);
    void HandleChordPingFailure (Ipv4Address destAddress, std::string message);
    void HandleChordPingRecv (Ipv4Address destAddress, std::string message);
    
    // Logic Callbacks
    void OnLookupComplete(uint32_t key, Ipv4Address result);

    virtual void ProcessCommand (std::vector<std::string> tokens);
    virtual void SetTrafficVerbose (bool on);
    virtual void SetErrorVerbose (bool on);
    virtual void SetDebugVerbose (bool on);
    virtual void SetStatusVerbose (bool on);
    virtual void SetChordVerbose (bool on);
    virtual void SetSearchVerbose (bool on);

  protected:
    virtual void DoDispose ();
    
  private:
    virtual void StartApplication (void);
    virtual void StopApplication (void);

    Ptr<PennChord> m_chord;
    uint32_t m_currentTransactionId;
    Ptr<Socket> m_socket;
    Time m_pingTimeout;
    uint16_t m_appPort, m_chordPort;
    Timer m_auditPingsTimer;
    std::map<uint32_t, Ptr<PingRequest> > m_pingTracker;
    
    // YOUR ORIGINAL DATA STRUCTURES RESTORED
    std::map<std::string, std::vector<std::string>> m_indices; // Replaces 'index'
    struct PendingPub { std::string key, val; };
    std::vector<PendingPub> m_pendingPubs; // Queue for lookups
    
    // Handlers
    void OnPublishReq(PennSearchMessage msg, Ipv4Address src);
    void OnSearchReq(PennSearchMessage msg, Ipv4Address src);
    void OnSearchRsp(PennSearchMessage msg, Ipv4Address src);
};
#endif