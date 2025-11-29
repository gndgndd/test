/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef PENN_CHORD_H
#define PENN_CHORD_H

#include "ns3/penn-application.h"
#include "ns3/penn-chord-message.h"
#include "ns3/ping-request.h"
#include <openssl/sha.h>

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

using namespace ns3;

class PennChord : public PennApplication
{
  public:
    static TypeId GetTypeId (void);
    PennChord ();
    virtual ~PennChord ();

    void SendPing (Ipv4Address destAddress, std::string pingMessage);
    void RecvMessage (Ptr<Socket> socket);
    void ProcessPingReq (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void ProcessPingRsp (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void AuditPings ();
    uint32_t GetNextTransactionId ();
    void StopChord ();

    // Callback with Application Layer (add more when required)
    void SetPingSuccessCallback (Callback <void, Ipv4Address, std::string> pingSuccessFn);
    void SetPingFailureCallback (Callback <void, Ipv4Address, std::string> pingFailureFn);
    void SetPingRecvCallback (Callback <void, Ipv4Address, std::string> pingRecvFn);

    // ==============================================================
    // Milestone 2A – Lookup callbacks and drivers
    // ==============================================================

    void SetLookupResultCallback (Callback<void, uint32_t, Ipv4Address> lookupResultFn);
    void IssueChordLookup (uint32_t keyHash, Ipv4Address originator);

    // ---- Search-specific lookup support ----
    void SetSearchLookupCallback (Callback<void, std::string, Ipv4Address> cb);
    void StartSearchLookup (std::string contextKey, uint32_t keyHash);

    // ---- Publish-specific lookup support ----
    void SetPublishLookupCallback (Callback<void, std::string, std::string, Ipv4Address> cb);
    void StartPublishLookup (const std::string &keyword,
                             const std::string &docId,
                             uint32_t keyHash);

    // Chord-internal handling of LOOKUP_* messages
    void ProcessLookupReq (PennChordMessage message, Ipv4Address sourceAddress);
    void ProcessLookupForward (PennChordMessage message, Ipv4Address sourceAddress);
    void ProcessLookupRsp (PennChordMessage message, Ipv4Address sourceAddress);

    // From PennApplication
    virtual void ProcessCommand (std::vector<std::string> tokens);

  protected:
    virtual void DoDispose ();

  private:
    virtual void StartApplication (void);
    virtual void StopApplication (void);

    uint32_t m_currentTransactionId;
    Ptr<Socket> m_socket;
    Time m_pingTimeout;
    uint16_t m_appPort;

    // ==============================================================
    // PING/AUDIT MEMBERS
    // ==============================================================
    Timer m_auditPingsTimer;
    std::map<uint32_t, Ptr<PingRequest> > m_pingTracker;
    Callback <void, Ipv4Address, std::string> m_pingSuccessFn;
    Callback <void, Ipv4Address, std::string> m_pingFailureFn;
    Callback <void, Ipv4Address, std::string> m_pingRecvFn;

    // ==============================================================
    // Milestone 2A – lookup state and callbacks
    // ==============================================================

    Callback<void, uint32_t, Ipv4Address> m_lookupResultFn;
    std::map<uint32_t, uint32_t> m_lookupHopCounter;
    std::map<uint32_t, std::string> m_searchContext;
    Callback<void, std::string, Ipv4Address> m_searchLookupFn;
    std::map<uint32_t, std::pair<std::string, std::string> > m_publishContext;
    Callback<void, std::string, std::string, Ipv4Address> m_publishLookupFn;

    // Join tracking
    uint32_t m_joinTransactionId; 

    // ==============================================================
    // Milestone 1 – Chord ring management
    // ==============================================================

    void CreateChord();
    void LeaveChord();
    void JoinChord(Ipv4Address referenceNode);
    void Ringstate();

    // ==============================================================
    // Milestone 1/2 – Stabilization + Notify
    // ==============================================================

    void Stabilize ();                 
    void Notify (Ipv4Address node);    
    bool IsBetween (Ipv4Address target, Ipv4Address start, Ipv4Address end); 
    void TransferKeys(Ipv4Address newOwner, Ipv4Address oldOwner, Ipv4Address predOfNewOwner);

    // ==============================================================
    // Milestone 2A – Finger Table (for O(log N) routing)
    // ==============================================================

    struct FingerEntry
    {
      uint32_t start;       
      Ipv4Address successor; 
    };

    std::vector<FingerEntry> m_fingerTable; 
    uint32_t m_fingerIndex;                 

    void InitFingerTable();
    void FixFingers();
    Ipv4Address FindSuccessor(uint32_t id); 
    Ipv4Address ClosestPrecedingFinger(uint32_t id); 

    Timer m_stabilizeTimer;
    Timer m_fixFingersTimer;
    void StartPeriodicStabilization();

    static std::string ToHexKey (uint32_t value);

    Ipv4Address m_successor;           
    Ipv4Address m_predecessor;         

    static std::set<Ipv4Address> s_joined;
    static std::map<Ipv4Address, Ipv4Address> m_successorPredecessor;
};

#endif