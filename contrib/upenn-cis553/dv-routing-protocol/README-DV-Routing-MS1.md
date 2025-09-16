# DV Routing for MS1

## (0) Thank you

Firstly, a big thanks to Sal and Huseyin for your work so far, it made my work on this implemenation much, much easier!

## (1) General Points
- My overall philosophy was to implement the new message types `HELLO_REQ` and `HELLO_RSP` in `dv-message.cc/.h` to closely mirror those of `PING_REQ` and `PING_RSP`
- The same can be said with regard to the corresponding methods in `dv-routing-protocol.cc/.h`

## (2) Changes to `dv-message.cc/.h`

1. `HELLO_REQ`: Payload: `helloMessage`
        - I noticed that Huseyin left the payload empty in his parallel implementation for LS. 
        - I believe either implementation is fine but chose to include the message to make things easier by keeping the implementation close to `PING_REQ` where possible. There may also be potential advantages to including a packet message for debugging purposes.
2. `HELLO_RSP`: Payload: `sourceAddress, helloMessage`
    - Same as above
    - Huseyin also implemented an equivalent `senderAddress` in the payload. It seems to me that in the end this won't be necessary since we can extract the sending node's address using the `GetOriginatorAddress()` method on the message objects.

## (3) Changes to `dv-routing-protocol.cc/.h`
- There are two primary ways in which my implmentation for DV differs from Huseyin's respective implementation for LS:
    1. I separated the handling of hello messages into two methods: `ProcessHelloReq()` and `ProcessHelloRsp()`
    2. Instead of creating a separate method (like `SendHello()`), I included the logic for periodic sending of hello messages to neighbors in the `AuditHellos()` function
    - As is clear from points 1 and 2, I tried to follow the naming convention for the `Ping` methods as closely as possible.

### Methods

1. `ProcessHelloReq(DVMessage dvMessage)`
    - Used `BroadcastPacket()` method to send response upon receiving a hello (I think this is simpler than iterating through sockets manually as is currently used in `RecvHelloMessage()` to respond to hello requests).
    - I used `dvMessage.GetSequenceNumber()` as the second argument to the `DVMessage` constructor following the ping implementation. I am not exactly sure about the meaning of this variable but I noticed Huseyin's LS implementation used a `0` instead - maybe my implementation should be altered accordingly?

2. `ProcessHelloRsp(DVMessage dvMessage, Ipv4Address localInterfaceAddress)`
    - Implementation follows Huseyin's LS implementation except for minor choice of naming `neighborAddress` (rather than `originatorAddress`) in accordance with `ObserveHello()` function definition in `neighbor-table.h`

3. `AuditHellos()`
    - Auditing using `neighborManager` implementation was very simple, follows LS
    - As mentioned above, I included the periodic sending of hello messages to neighbors here.
        - As such, there was no need to set up a separate `HelloCallback` function for the `m_neighborTimers` object as functionality was already included in the `AuditCallback`.

4. `DumpNeighbors()`
    - Used the `PRINT_LOG` rather than `STATUS_LOG` to print neighbor table entries (I recommend the LS implementation be changed to follow this implementation)
    - The problem with using the `STATUS_LOG` is that always prints a pre-defined start to the message (`"*STATUS* Node 1,..."`) which disrupts the display of the table.
    - Otherwise, implementation followed that of Huseyin's implementation for LS.

## (4) Minor change to `ls-routing-protocol.cc` and `ls-message.cc`
- I made only 3 direct interventions in the code for the LS protocol:
    1. In `ls-routing-protocol.cc`, I deleted the extra `DumpNeigbors()` definition left over from the starter code and moved the proper implementation to that location in the code in its place.
    2. In `ls-message.cc`, I altered some problematic characters and switched them to regular space characters. I believe this may have been cause by a difference/mistranslations between environments (I am using VS Code).
    3. Added `#include "ns3/neighbor-table.h"` and `#include "ns3/neighbor-timers.h"` statements (if they weren't there already).

