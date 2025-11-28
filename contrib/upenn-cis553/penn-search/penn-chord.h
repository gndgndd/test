/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef PENN_CHORD_H
#define PENN_CHORD_H

#include "ns3/penn-application.h"
#include "ns3/penn-chord-message.h"
#include "ns3/ping-request.h"
#include <openssl/sha.h>

#include "ns3/ipv4-address.h"
#include <map>
#include <vector>
#include <string>
#include "ns3/socket.h"
#include "ns3/nstime.h"
#include "ns3/timer.h"
#include "ns3/uinteger.h"

using namespace ns3;

class PennChord : public PennApplication
{
  public:
    static TypeId GetTypeId (void);
    PennChord ();
    virtual ~PennChord ();

    void SendPing (Ipv4Address destAddress, std::string pingMessage);
    void RecvMessage (Ptr<Socket> socket);
    
    // Message Handlers
    void ProcessPingReq (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void ProcessPingRsp (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    
    // MS2 Handlers
    void ProcessLookupReq (PennChordMessage message, Ipv4Address sourceAddress);
    void ProcessLookupForward (PennChordMessage message, Ipv4Address sourceAddress);
    void ProcessLookupRsp (PennChordMessage message, Ipv4Address sourceAddress);
    void ProcessGetPredecessor (PennChordMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
    void ProcessPredecessorRsp (PennChordMessage message, Ipv4Address sourceAddress);
    void ProcessNotify (PennChordMessage message, Ipv4Address sourceAddress);
    void ProcessRingStateReq (PennChordMessage message, Ipv4Address sourceAddress);

    void AuditPings ();
    uint32_t GetNextTransactionId ();
    void StopChord ();

    // Callback Setters
    void SetPingSuccessCallback (Callback <void, Ipv4Address, std::string> pingSuccessFn);
    void SetPingFailureCallback (Callback <void, Ipv4Address, std::string> pingFailureFn);
    void SetPingRecvCallback (Callback <void, Ipv4Address, std::string> pingRecvFn);

    // Lookup Callbacks
    void SetLookupResultCallback (Callback<void, uint32_t, Ipv4Address> lookupResultFn);
    void SetSearchLookupCallback (Callback<void, std::string, Ipv4Address> cb);
    void SetPublishLookupCallback (Callback<void, std::string, std::string, Ipv4Address> cb);
    
    // New callback for notifying app layer to transfer keys
    void SetKeyTransferCallback (Callback<void, Ipv4Address> cb);

    // Public API
    void IssueChordLookup (uint32_t keyHash, Ipv4Address originator);
    void StartSearchLookup (std::string contextKey, uint32_t keyHash);
    void StartPublishLookup (const std::string &keyword, const std::string &docId, uint32_t keyHash);

    virtual void ProcessCommand (std::vector<std::string> tokens);

  protected:
    virtual void DoDispose ();

  private:
    virtual void StartApplication (void);
    virtual void StopApplication (void);

    // Context types for lookups
    enum LookupContextType {
      CTX_SEARCH,
      CTX_PUBLISH,
      CTX_FINGER_FIX
    };

    struct LookupContext {
        LookupContextType type;
        std::string searchCtx;      // For Search
        std::string pubKeyword;     // For Publish
        std::string pubDocId;       // For Publish
        uint32_t fingerIndex;       // For Finger Fix
    };

    uint32_t m_currentTransactionId;
    Ptr<Socket> m_socket;
    Time m_pingTimeout;
    uint16_t m_appPort;

    // Ping tracking
    Timer m_auditPingsTimer;
    std::map<uint32_t, Ptr<PingRequest> > m_pingTracker;
    Callback <void, Ipv4Address, std::string> m_pingSuccessFn;
    Callback <void, Ipv4Address, std::string> m_pingFailureFn;
    Callback <void, Ipv4Address, std::string> m_pingRecvFn;

    // Lookup tracking
    Callback<void, uint32_t, Ipv4Address> m_lookupResultFn;
    std::map<uint32_t, uint32_t> m_lookupHopCounter;
    std::map<uint32_t, LookupContext> m_lookupContexts;
    
    Callback<void, std::string, Ipv4Address> m_searchLookupFn;
    Callback<void, std::string, std::string, Ipv4Address> m_publishLookupFn;
    Callback<void, Ipv4Address> m_keyTransferFn;

    // Chord State
    void CreateChord();
    void LeaveChord();
    void JoinChord(Ipv4Address referenceNode);
    void InitiateRingState();

    // Stabilization
    Timer m_stabilizeTimer;
    void Stabilize ();
    void Notify (Ipv4Address node);
    bool IsBetween (Ipv4Address target, Ipv4Address start, Ipv4Address end);
    bool IsBetweenHashSemiOpen(uint32_t target, uint32_t start, uint32_t end);
    
    // Finger Table
    struct FingerEntry {
      uint32_t start;
      Ipv4Address successor;
    };
    std::vector<FingerEntry> m_fingerTable;
    uint32_t m_fingerIndex;
    Timer m_fixFingersTimer;
    
    void InitFingerTable();
    void FixFingers();
    Ipv4Address FindSuccessor(uint32_t id);
    Ipv4Address ClosestPrecedingFinger(uint32_t id);

    // Node pointers
    Ipv4Address m_successor;
    Ipv4Address m_predecessor;
};

#endif