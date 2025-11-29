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
// Added for Chord implementation
#include <unordered_map>

using namespace ns3;

class PennChord : public PennApplication
{
public:
  // =================================================================
  // SKELETON PUBLIC INTERFACE (DO NOT MODIFY)
  // =================================================================
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

  // From PennApplication
  virtual void ProcessCommand (std::vector<std::string> tokens);


  // =================================================================
  // EXTENDED CHORD PUBLIC INTERFACE
  // =================================================================
  
  // --- Core Ring Management ---
  void ChordCreate();
  void Join(Ipv4Address landmark);
  void Leave();
  
  // --- Lookup Logic ---
  void ChordLookup(uint32_t transactionId, uint32_t hashToFind);
  bool IsInBetween(uint32_t start, uint32_t target, uint32_t end) const;
  
  // --- Protocol Message Handlers ---
  // Successor Management
  void ProcessFindSuccessorReq (PennChordMessage message);
  void ProcessFindSuccessorRsp (PennChordMessage message);
  
  // Stabilization & Notification
  void Stabilize();
  void ProcessStabilizeReq(PennChordMessage message);
  void ProcessStabilizeRsp(PennChordMessage message);
  void ProcessNotifcationPkt(PennChordMessage message); 
  
  // Ring Maintenance
  void ProcessRingStatePtk(PennChordMessage message);
  void RingState();
  
  // Leave/Departure Handling
  void ProcessLeaveSuccessor(PennChordMessage message);
  void ProcessLeavePredecessor(PennChordMessage message);

  // --- Callback Registration ---
  // Lookup Events
  void SetLookUpCallback(Callback<void, Ipv4Address, uint32_t> lookupCb);
  void SetLookupSuccessCallback(Callback <void, uint32_t, Ipv4Address> lookupSuccessFn);
  void SetLookupFailureCallback(Callback <void, uint32_t> lookupFailureFn);
  
  // Churn Events
  void SetLeaveCallback(Callback<void, Ipv4Address> leaveCb);
  void SetRejoinCallback(Callback<void, Ipv4Address> rejoinCb);


protected:
  virtual void DoDispose ();
  
private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  // =================================================================
  // SKELETON PRIVATE MEMBERS
  // =================================================================
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


  // =================================================================
  // EXTENDED CHORD PRIVATE MEMBERS
  // =================================================================

  // --- Data Structures ---
  struct FingerTableEntry
  {
    uint32_t start;         // (nodeId + 2^i) % 2^32
    uint32_t finger_id;     // id of successor of start
    Ipv4Address finger_ip;  // ip of successor of start
  };

  // --- Routing State ---
  uint32_t m_nodeHash;
  Ipv4Address m_predecessor;
  Ipv4Address m_successor;
  
  bool m_leftChord = false;
  uint32_t m_joinTransactionId;

  // --- Finger Table Management ---
  std::vector<FingerTableEntry> m_fingerTable;
  std::map<uint32_t,uint32_t> m_pendingFingers; // Key: transId, Value: fingerIndex
  
  uint32_t m_fingerTableSize;       // Usually 32
  uint32_t m_nextFingerToFix;       // Iterator for stabilization
  bool m_fingerTableInitialized;

  void InitFingerTable();
  void FixFingerTable();
  int ClosestPrecedingFinger(uint32_t idToFind) const;

  // --- Lookup & Hop Statistics ---
  std::map<uint32_t, uint32_t> m_pendingLookups;
  std::unordered_map<uint32_t, uint32_t> m_hopsPerLookup; 
  uint16_t m_totalHops = 0;
  uint16_t m_numLookups = 0;

  // --- Application Timers ---
  Timer m_stabilizeTimer;
  Timer m_fixFingerTimer;

  // --- User Callbacks ---
  Callback <void, Ipv4Address, uint32_t> m_lookupCallback;
  Callback <void, uint32_t, Ipv4Address> m_lookupSuccessFn;
  Callback <void, uint32_t> m_lookupFailureFn;
  Callback <void, Ipv4Address> m_leaveCallback;
  Callback <void, Ipv4Address> m_rejoinCallback;

  // Internal helpers
  void TriggerRejoinCallback();
};

#endif