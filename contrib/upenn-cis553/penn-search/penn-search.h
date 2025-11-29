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
#include "ns3/socket.h"
#include "ns3/nstime.h"
#include "ns3/timer.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include <sstream>

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

    void HandleChordPingSuccess (Ipv4Address destAddress, std::string message);
    void HandleChordPingFailure (Ipv4Address destAddress, std::string message);
    void HandleChordPingRecv (Ipv4Address destAddress, std::string message);
    void HandleSearchChordLookup (std::string contextKey, Ipv4Address owner);

    virtual void ProcessCommand (std::vector<std::string> tokens);

    virtual void SetTrafficVerbose (bool on);
    virtual void SetErrorVerbose (bool on);
    virtual void SetDebugVerbose (bool on);
    virtual void SetStatusVerbose (bool on);
    virtual void SetChordVerbose (bool on);
    virtual void SetSearchVerbose (bool on);

    void StartSearch (const std::vector<std::string> &terms);
    void ProcessSearchReq (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void ProcessSearchRsp (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void ContinueSearch (const std::string &currentKeyword, const std::string &currentDocs, const std::string &remainingTerms, const std::string &originIp);

    void ProcessPublishReq (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void ProcessStoreReq (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void HandlePublishChordLookup (std::string keyword, std::string docId, Ipv4Address owner);
    
    // Handle request from Chord to transfer keys
    void HandleTransferKeys (Ipv4Address newOwner, uint32_t rangeStart, uint32_t rangeEnd);

    std::string SetToString (const std::set<std::string> &s);
    std::string IntersectDocLists (const std::string &a, const std::string &b);
    std::string CombineSearchResults (const std::string &existingDocs, const std::string &newDocs);
    void InitializeSearchLayer ();

    void ChordLookupForwardingStub (const std::string &keyword, const std::string &docs, const std::string &remaining, const std::string &originIp, Ipv4Address nextHop);
    void DistributedInvertedListMaintenanceStub ();

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

    void StartSearchFromOrigin (const std::vector<std::string> &terms, const std::string &originIp);
};

#endif