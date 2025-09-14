# MS1 Neighbor Manager — How to include & build

This module adds HELLO-based neighbor discovery shared by LS and DV.

## Files
```
contrib/upenn-cis553/neighbor/
  neighbor-table.h / .cc   # soft state for neighbors
  neighbor-timers.h / .cc  # two periodic timers (HELLO + Audit)
```

## Include paths (important)

These headers are exported by waf under the **flattened** `ns3/` tree.  
**Use these forms from headers and .cc files:**

```cpp
#include "ns3/neighbor-table.h"
#include "ns3/neighbor-timers.h"
```

## Where to include them

Add the headers in any protocol implementation that needs neighbor discovery:

- **LSRoutingProtocol** (`ls-routing-protocol.h/.cc`)  
  ```cpp
  #include "ns3/neighbor-table.h"
  #include "ns3/neighbor-timers.h"
  ```

- **DVRoutingProtocol** (`dv-routing-protocol.h/.cc`)  
  ```cpp
  #include "ns3/neighbor-table.h"
  #include "ns3/neighbor-timers.h"
  ```

## How Neighbor Manager works

1. **NeighborTable**  
   - Stores per-neighbor entries (`neighborAddress`, `interfaceAddress`, `lastSeen`).  
   - Updated whenever a HELLO message is observed.  
   - Periodically audited to remove stale entries (default timeout = 3s).  

2. **NeighborTimers**  
   - Runs two periodic tasks for each node:  
     - **HELLO Tick** → sends HELLO messages every ~1s.  
     - **Audit Tick** → calls `NeighborTable::Audit()` every ~1s.  
     - Uses ns-3 `Timer` and `Callback` to bind into LS/DV protocols.

3. **Protocols (LS/DV)**  
   - Call `m_neighbors.ObserveHello()` when HELLO or HELLO_RSP is received.  
   - Use `m_neighbors.Snapshot()` when dumping neighbor tables.  
   - Configure timers at init:  
     ```cpp
     m_neighborTimers = CreateObject<NeighborTimers>();
     m_neighborTimers->Configure(Seconds(1.0), Seconds(1.0));
     m_neighborTimers->SetHelloCallback(...);
     m_neighborTimers->SetAuditCallback(...);
     m_neighborTimers->Start();
     ```

With this setup, both LS and DV can reuse the same **Neighbor Manager** for MS1 neighbor discovery.
