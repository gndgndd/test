/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef PENN_SEARCH_H
#define PENN_SEARCH_H

#include "ns3/penn-application.h"
#include "ns3/penn-chord.h"
#include "ns3/penn-search-message.h"
#include "ns3/ping-request.h"

#include "ns3/ipv4-address.h"
#include <map>
#include <set>
#include <vector>
#include <string>

using namespace ns3;

class PennSearch : public PennApplication
{
  public:
    static TypeId GetTypeId (void);
    PennSearch ();
    virtual ~PennSearch ();

    void SendPing (std::string nodeId, std::string pingMessage);
    void SendPennSearchPing (Ipv4Address destAddress, std::string pingMessage);
    void RecvMessage (Ptr<Socket> socket);
    void ProcessPingReq (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void ProcessPingRsp (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void AuditPings ();
    uint32_t GetNextTransactionId ();

    // Chord Callbacks
    void HandleChordPingSuccess (Ipv4Address destAddress, std::string message);
    void HandleChordPingFailure (Ipv4Address destAddress, std::string message);
    void HandleChordPingRecv (Ipv4Address destAddress, std::string message);
    void HandleSearchChordLookup (std::string contextKey, Ipv4Address owner);
    void HandlePublishChordLookup (std::string keyword, std::string docId, Ipv4Address owner);
    
    // New: Key Transfer Callback
    void HandleKeyTransfer (Ipv4Address newOwner);

    virtual void ProcessCommand (std::vector<std::string> tokens);

    // Search Logic
    void StartSearch (const std::vector<std::string> &terms);
    void ProcessSearchReq (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void ProcessSearchRsp (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void ProcessSearchEntry (PennSearchMessage message, Ipv4Address sourceAddress); // Via-node handler
    void ContinueSearch (const std::string &currentKeyword, const std::string &currentDocs, const std::string &remainingTerms, const std::string &originIp);

    // Publish/Store Logic
    void ProcessPublishReq (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void ProcessStoreReq (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);

    // Helpers
    std::string SetToString (const std::set<std::string> &s);
    std::string IntersectDocLists (const std::string &a, const std::string &b);
    std::string CombineSearchResults (const std::string &existingDocs, const std::string &newDocs);

    // From PennLog
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
    std::map<std::string, std::set<std::string> > m_invertedList;
};

#endif