/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010 University of Pennsylvania
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

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
#include <unordered_map>

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

    // Lookup callbacks
    void SetLookupSuccessCallback(Callback <void, uint32_t, Ipv4Address> lookupSuccessFn);
    void SetLookupFailureCallback(Callback <void, uint32_t> lookupFailureFn);

    // Leave Callback
    void SetLeaveCallback(Callback<void, Ipv4Address> leaveCb);
    void SetRejoinCallback(Callback<void, Ipv4Address> rejoinCb);

    // From PennApplication
    virtual void ProcessCommand (std::vector<std::string> tokens);
    
    
    // ---------------------------------------
    void ChordCreate();
    void Join(Ipv4Address landmark);
    void ProcessFindSuccessorReq (PennChordMessage message);
    void ProcessFindSuccessorRsp (PennChordMessage message);

    void Stabilize();
    void ProcessStabilizeReq(PennChordMessage message);
    void ProcessStabilizeRsp(PennChordMessage message);
    bool IsInBetween(uint32_t start, uint32_t target, uint32_t end) const;

    void ProcessNotifcationPkt(PennChordMessage message);

    void RingState();
    void ProcessRingStatePtk(PennChordMessage message);

    void Leave();
    void ProcessLeaveSuccessor(PennChordMessage message);
    void ProcessLeavePredecessor(PennChordMessage message);

    // lookup logic
    void ChordLookup(uint32_t transactionId, uint32_t hashToFind);
    void SetLookUpCallback(Callback<void, Ipv4Address, uint32_t> lookupCb);


  protected:
    virtual void DoDispose ();
    
  private:
    virtual void StartApplication (void);
    virtual void StopApplication (void);

    // hop count trackers
    std::unordered_map<uint32_t, uint32_t> m_hopsPerLookup; // key: transactionId, value: hops

    uint16_t m_totalHops = 0;
    uint16_t m_numLookups = 0;

    uint32_t m_currentTransactionId;
    Ptr<Socket> m_socket;
    Time m_pingTimeout;
    uint16_t m_appPort;
    // Timers
    Timer m_auditPingsTimer;
    // Ping tracker
    std::map<uint32_t, Ptr<PingRequest> > m_pingTracker;
    // Callbacks
    Callback <void, Ipv4Address, std::string> m_pingSuccessFn;
    Callback <void, Ipv4Address, std::string> m_pingFailureFn;
    Callback <void, Ipv4Address, std::string> m_pingRecvFn;

    Callback <void, Ipv4Address, uint32_t> m_lookupCallback;

    // successor, predecessor, and nodeHash
    Ipv4Address m_predecessor;
    Ipv4Address m_successor;
    uint32_t m_nodeHash;

    // leave callbacks
    Callback<void, Ipv4Address> m_leaveCallback;
    Callback<void, Ipv4Address> m_rejoinCallback;
    void TriggerRejoinCallback();
    bool m_leftChord = false;

    // Lookup callbacks
    Callback <void, uint32_t, Ipv4Address> m_lookupSuccessFn;
    Callback <void, uint32_t> m_lookupFailureFn;

    Timer m_stabilizeTimer;

    // finger table timer to call FixFingerTable
    Timer m_fixFingerTimer;

    // finger table entry struct
    struct FingerTableEntry
    {
      uint32_t start;         // (nodeId + 2^i) % 2^32
      uint32_t finger_id;     // id of successor of start
      Ipv4Address finger_ip;  // ip of successor of start
      // uint32_t finger_port;   // port of successor of start
    };

    // finger table initilization
    std::vector<FingerTableEntry> m_fingerTable;

    // tracks finger table entries that are in the process of being fixed
    std::map<uint32_t,uint32_t> m_pendingFingers;

    uint32_t m_fingerTableSize;   // 32
    uint32_t m_nextFingerToFix;   // cycles 0..31
    bool m_fingerTableInitialized; // true if finger table is initialized

    // finger table methods
    void InitFingerTable();
    void FixFingerTable();
    int ClosestPrecedingFinger(uint32_t idToFind) const;

    // transaction ID for a packet sent during a rejoin
    uint32_t m_joinTransactionId;

    std::map<uint32_t, uint32_t> m_pendingLookups;
};

#endif


