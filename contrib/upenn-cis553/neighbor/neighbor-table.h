/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef UPENN_CIS553_NEIGHBOR_TABLE_H
#define UPENN_CIS553_NEIGHBOR_TABLE_H

#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include <map>
#include <vector>

namespace ns3 {

/**
 * Lightweight record for one discovered neighbor.
 * - neighborAddress: the neighbor node's main IPv4 address
 * - interfaceAddress: our local interface address that received the HELLO
 * - lastSeen: simulation time when the last HELLO was observed
 */
struct NeighborTableEntry
{
  Ipv4Address neighborAddress;
  Ipv4Address interfaceAddress;
  Time        lastSeen;
};

/**
 * Per-node neighbor table used in MS1 for HELLO-based discovery.
 * Stores soft state and supports:
 *  - ObserveHello(): insert/update on incoming HELLO
 *  - Audit(): purge entries that have not been refreshed within timeout
 *  - Snapshot(): copy out entries for printing/reporting
 */
class NeighborTable
{
public:
  NeighborTable();

  // Set the inactivity timeout. Entries older than this are removed by Audit().
  void SetTimeout(Time t);

  // Insert or update a neighbor when a HELLO is observed on a local interface.
  void ObserveHello(Ipv4Address neighborAddress, Ipv4Address localInterface);

  // Remove stale neighbors (call periodically from a timer).
  void Audit();

  // Clear all entries from the table.
  void Clear();

  // Number of neighbors currently tracked.
  uint32_t Size() const;

  // Return a copy of the table for safe iteration and printing.
  std::vector<NeighborTableEntry> Snapshot() const;

  // Returns true if address has entry in neighbor table, false otherwise
  bool Contains(Ipv4Address address);

private:
  std::map<Ipv4Address, NeighborTableEntry> m_table;
  Time m_timeout;
};

} 
#endif
