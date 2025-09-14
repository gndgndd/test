#include "neighbor-table.h"
#include "ns3/simulator.h"

namespace ns3 {

/**
 * Constructor.
 * Initializes the neighbor table with a default timeout of 3 seconds.
 * Any neighbor not refreshed within this interval will be considered stale
 * and removed by the Audit() method.
 */
NeighborTable::NeighborTable()
  : m_timeout(Seconds(3.0))
{
}

/**
 * Update the timeout value for neighbor entries.
 * @param t  The new timeout duration.
 */
void
NeighborTable::SetTimeout(Time t)
{
  m_timeout = t;
}

/**
 * Record a HELLO message received from a neighbor.
 * If the neighbor already exists in the table, its lastSeen timestamp
 * and interface address are updated. Otherwise, a new entry is created.
 *
 * @param neighborAddress  The IP address of the neighbor node.
 * @param localInterface   The local interface address that received the HELLO.
 */
void
NeighborTable::ObserveHello(Ipv4Address neighborAddress, Ipv4Address localInterface)
{
  NeighborTableEntry &e = m_table[neighborAddress];
  e.neighborAddress  = neighborAddress;
  e.interfaceAddress = localInterface;
  e.lastSeen         = Simulator::Now();   // timestamp of most recent HELLO
}

/**
 * Remove stale neighbors from the table.
 * Iterates over all entries and deletes those whose lastSeen timestamp
 * exceeds the timeout interval.
 */
void
NeighborTable::Audit()
{
  const Time now = Simulator::Now();
  for (auto it = m_table.begin(); it != m_table.end(); )
    {
      if (now - it->second.lastSeen > m_timeout)
        it = m_table.erase(it);
      else
        ++it;
    }
}

/**
 * Clear all entries from the neighbor table.
 * Useful for resetting state during shutdown or reinitialization.
 */
void
NeighborTable::Clear()
{
  m_table.clear();
}

/**
 * @return The current number of entries in the neighbor table.
 */
uint32_t
NeighborTable::Size() const
{
  return static_cast<uint32_t>(m_table.size());
}

/**
 * Create a snapshot of the neighbor table.
 * Copies all entries into a vector to allow safe iteration without being
 * affected by concurrent modifications (e.g., from Audit()).
 *
 * @return Vector of NeighborTableEntry objects representing current neighbors.
 */
std::vector<NeighborTableEntry>
NeighborTable::Snapshot() const
{
  std::vector<NeighborTableEntry> v;
  v.reserve(m_table.size());
  for (const auto &kv : m_table)
    v.push_back(kv.second);
  return v;
}
}
