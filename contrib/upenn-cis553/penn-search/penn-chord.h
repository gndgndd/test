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
#include <algorithm>
#include <vector>
#include <set>
#include <sstream>
#include <iomanip>


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

    // From PennApplication
    virtual void ProcessCommand (std::vector<std::string> tokens);
    // MS2A ADDITIONS
    // PennSearch must register a callback so Chord can notify when lookup finishes
    void SetLookupResultCallback(Callback<void, uint32_t, Ipv4Address> lookupResultFn);
    // PennSearch triggers a lookup by calling this method
    void IssueChordLookup(uint32_t keyHash, Ipv4Address originator);
    // MS2A: Search lookup callback (Chord returns contextKey and owner)
    void SetSearchLookupCallback(Callback<void, std::string, Ipv4Address> cb);
    // MS2A: Entry point for PennSearch to start a search lookup
    void StartSearchLookup(std::string contextKey, uint32_t keyHash);
    // Internal handlers for new lookup message types
    void ProcessLookupReq(PennChordMessage message, Ipv4Address sourceAddress);
    void ProcessLookupForward(PennChordMessage message, Ipv4Address sourceAddress);
    void ProcessLookupRsp(PennChordMessage message, Ipv4Address sourceAddress);
    // Publish lookup
    void SetPublishLookupCallback(Callback<void, std::string, std::string, Ipv4Address> cb);
    void StartPublishLookup(const std::string &keyword,const std::string &docId, uint32_t keyHash);

    // ---------------------------------------

  protected:
    virtual void DoDispose ();

  private:
    virtual void StartApplication (void);
    virtual void StopApplication (void);

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

    // MS2A: lookup completion callback (Chord → PennSearch)
    Callback<void, uint32_t, Ipv4Address> m_lookupResultFn;
    // MS2A: track hop count / forwarding count per lookup
    std::map<uint32_t, uint32_t> m_lookupHopCounter;
    // MS2A: Map transaction id to saved context string
    std::map<uint32_t, std::string> m_searchContext;
    // MS2A: Callback to return lookup results to PennSearch
    Callback<void, std::string, Ipv4Address> m_searchLookupFn;
    // MS2A publish lookup support
    std::map<uint32_t, std::pair<std::string, std::string>> m_publishContext;
    Callback<void, std::string, std::string, Ipv4Address> m_publishLookupFn;

    // Milestone 1 - Create and Join - Sam
     void CreateChord();
     void LeaveChord();
     void JoinChord(Ipv4Address referenceNode);
     void Ringstate();

     // ==============================================================
     // Milestone 1 (Sal) – Part 2: Stabilization + Notify
     // --------------------------------------------------------------
     // Added fields and helper functions to maintain the Chord ring:
     // successor, predecessor, and consistency logic (no finger tables yet).
     // ==============================================================

     void Stabilize ();                 // runs ring stabilization logic
     void Notify (Ipv4Address node);    // updates predecessor if needed
     bool IsBetween (Ipv4Address target, Ipv4Address start, Ipv4Address end); // helper for hash-space checks

     // Helpers
     static std::string ToHexKey (uint32_t value);
     Ipv4Address FindSuccessor (uint32_t id);
     void SendRingstate (Ipv4Address target);

     Ipv4Address m_successor;           // node's immediate successor
     Ipv4Address m_predecessor;         // node's immediate predecessor

     // Global ring tracking (simulated for M1)
      static std::set<Ipv4Address> s_joined;
      static std::map<Ipv4Address, Ipv4Address> m_successorPredecessor;
 };

 #endif

