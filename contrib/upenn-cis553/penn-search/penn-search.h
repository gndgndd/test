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
// Added for complex types
#include <tuple>

using namespace ns3;

class PennSearch : public PennApplication
{
public:
  // =================================================================
  // SKELETON PUBLIC INTERFACE
  // =================================================================
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

  // From PennApplication
  virtual void ProcessCommand (std::vector<std::string> tokens);
  // From PennLog
  virtual void SetTrafficVerbose (bool on);
  virtual void SetErrorVerbose (bool on);
  virtual void SetDebugVerbose (bool on);
  virtual void SetStatusVerbose (bool on);
  virtual void SetChordVerbose (bool on);
  virtual void SetSearchVerbose (bool on);


  // =================================================================
  // EXTENDED SEARCH INTERFACE
  // =================================================================

  // --- Publishing Logic ---
  void PublishMetadataFile(std::string filename);
  void ProcessPublishReq (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
  void ProcessPublishRsp (PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);

  // --- Searching Logic ---
  void ProcessSearchReq(PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);
  void ProcessSearchRsp(PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);

  // --- Lookup & Routing ---
  void Lookup(uint32_t hashToFind);
  void HandleLookupResult(Ipv4Address owner, uint32_t hashToFind);
  void ProcessLookupResult(Ipv4Address owner, uint32_t hashToFind);
  
  // Chord Integration Callbacks
  void HandleChordLookupSuccess (uint32_t tid, Ipv4Address owner);
  void HandleChordLookupFailure (uint32_t tid);

  // --- Churn Handling ---
  void HandleLeave(Ipv4Address successorIp);
  void HandleRejoin(Ipv4Address successorIp);
  void ProcessRejoin(PennSearchMessage message, Ipv4Address sourceAddress, uint16_t sourcePort);

  // --- Public State (Compatibility) ---
  std::map<uint32_t, std::pair<std::string, std::vector<std::string>>> m_pendingPublishes;
  std::map<uint32_t, std::pair<std::string, std::vector<std::string>>> m_pendingRejoin;
  std::map<uint32_t, uint32_t> m_lookupTracker; //transactionId -> hashToFind


protected:
  virtual void DoDispose ();
  
private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  // =================================================================
  // SKELETON PRIVATE MEMBERS
  // =================================================================
  Ptr<PennChord> m_chord;
  uint32_t m_currentTransactionId;
  Ptr<Socket> m_socket;
  Time m_pingTimeout;
  uint16_t m_appPort, m_chordPort;
  // Timers
  Timer m_auditPingsTimer;
  // Ping tracker
  std::map<uint32_t, Ptr<PingRequest> > m_pingTracker;


  // =================================================================
  // EXTENDED PRIVATE MEMBERS
  // =================================================================
  
  // Inverted index <keyword, docIDs>
  std::map<std::string, std::vector<std::string>> m_invertedIndex;

  // Search tracker: tid -> (remaining keywords [to be passed on], currentDocs, requester, keywordIndex)
  std::map<uint32_t, std::tuple<
      std::vector<std::string>, // keywords
      std::vector<std::string>, // current docs
      Ipv4Address,              // requester
      uint32_t                  // current keyword index
    >> m_pendingSearches;

};

#endif