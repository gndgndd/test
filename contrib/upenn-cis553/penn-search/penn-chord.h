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
#include "ns3/timer.h"
#include "ns3/callback.h"

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

    // Callbacks
    void SetPingSuccessCallback (Callback <void, Ipv4Address, std::string> pingSuccessFn);
    void SetPingFailureCallback (Callback <void, Ipv4Address, std::string> pingFailureFn);
    void SetPingRecvCallback (Callback <void, Ipv4Address, std::string> pingRecvFn);

    // Added from your original logic to support Search interactions
    void SetLookupResultCallback(Callback<void, uint32_t, Ipv4Address> cb);
    void InitiateLookup(uint32_t key, bool isApp);

    // From PennApplication
    virtual void ProcessCommand (std::vector<std::string> tokens);

  protected:
    virtual void DoDispose ();
    
  private:
    virtual void StartApplication (void);
    virtual void StopApplication (void);

    // Skeleton variables
    uint32_t m_currentTransactionId;
    Ptr<Socket> m_socket;
    Time m_pingTimeout;
    uint16_t m_appPort;
    Timer m_auditPingsTimer;
    std::map<uint32_t, Ptr<PingRequest> > m_pingTracker;
    Callback <void, Ipv4Address, std::string> m_pingSuccessFn;
    Callback <void, Ipv4Address, std::string> m_pingFailureFn;
    Callback <void, Ipv4Address, std::string> m_pingRecvFn;

    // --- YOUR IMPLEMENTATION LOGIC (RESTORED) ---
    // Renamed slightly to differentiate from original
    struct NodeEntry {
        Ipv4Address ip;
        uint32_t id;
    };

    uint32_t m_myId;
    NodeEntry m_predecessor;
    std::vector<NodeEntry> m_fingers; // Size 32
    int m_nextFingerFix;

    Timer m_stabilizeTimer;
    Timer m_fingerTimer;
    
    // Logic Helpers
    void HandleStabilize();
    void HandleFixFingers();
    void HandleCheckPredecessor();
    
    // Message Handlers (Logic from your original code)
    void OnFindSuccessorReq(PennChordMessage msg, Ipv4Address src);
    void OnFindSuccessorRsp(PennChordMessage msg, Ipv4Address src);
    void OnNotify(PennChordMessage msg, Ipv4Address src);
    void OnGetPredecessorReq(PennChordMessage msg, Ipv4Address src);
    void OnGetPredecessorRsp(PennChordMessage msg, Ipv4Address src);
    void OnRingState(PennChordMessage msg, Ipv4Address src);

    // Math
    bool IsBetween(uint32_t k, uint32_t start, uint32_t end, bool inclusiveEnd);
    Ipv4Address GetClosestPrecedingNode(uint32_t key);
    
    // Stats
    uint32_t m_totalHops;
    uint32_t m_totalLookups;
    Callback<void, uint32_t, Ipv4Address> m_lookupResultCb;
};

#endif