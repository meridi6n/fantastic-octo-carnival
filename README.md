Crappy code I wrote a long time ago for an driver injector/mapper, code is heavily incomplete since I was not given authorization to provide the entire project, but most of the flow/logic is there and should be easy to recreate.

Note this doesn't work on Windows 11 if VBS is enabled. It was only really tested on 10 and even then it was quite unstable - worked sometimes, others not, lots of BSODs. Here be dragons, etc.

The initial goal was to craft bypasses for the specific detection vectors listed down below.

### 1. `MmGetPhysicalMemoryRanges`

Used to scan system memory map and use `3` in search of unbacked memory.
Bypassed with `MiRemovePhysicalMemory`. Frames are removed from the memory map but you end up with holes and fragmented ranges clearly visible in tools like RAMmap.

### 2. `MmIsNonPagedSystemAddressValid`

Before Windows 11 `MmIsNonPagedSystemAddressValid` would return `false` for session VAs.
Bypassed by allocating the payload VA with `MiObtainSessionVa` (which gives you a VA of type `MiVaSessionSpace`) rather than the other usual methods. Scanners usually skipped paged memory.

### 3. `MmGetVirtualForPhysical`

`MmGetVirtualForPhysical` relies on the getting the VA from the PFN database for a given frame. 
Bypasseds by using `MiInitializePfn` to point the payload frames into a legitimate driver's VA; one could think about it as a poor man's shadow walker/TLB split, except worse and easier to detect. 

### 4. Thread runtime scanning

Payload code execution happened inside DPCs via timers (`KeSetTimerEx`). This prevented many detection heuristics from triggering, but had an obvious problem of IRQL: since the payload only ever executed at `DISPATCH_LEVEL` everything had to be designed to be extremely fast (so as to not trigger the watchdog) plus only kernel functions callable at that IRQL (or greater) could be used; very limiting but possible to make it work.

---

Overall, thinking back on it I think there were some good ideas with respect to evasion, but there was a lot of overengineered garbage which coupled with my inexperience at the time did more harm than good.
