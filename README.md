This plugin implemets Forward-Edge Code Pointer Hiding as proposed by [Crane et
al](https://www.ics.uci.edu/~perl/oakland15_readactor.pdf), aiming to thwart Indirect JIT-ROP attacks.

Given a binary protected with Function-Granular ASLR and Execute-Only-Memory,
an attacker can theoretically circumvent these protection mechanisms by
executing an Indirect JIT-ROP attack. This attack consists of leaking function
pointers during execution, thus revealing the specific parts in memory in
which those functions lie. The memory patches disclosed by this technique may
be sufficient to build an effective ROP.

In order to thwart this kind of attack, this plugin hides function addresses:
whenever a function address is written to a readable memory, the plugin
replaces it with an address of a trampoline function that contains nothing but
a call to the original function. This way, even if an attacker manages to leak
a function address, the only thing she can conclude is the location of the
trampoline function, which doesn't include enough instructions to build a ROP.

**NOTE:** In order to achieve full protection, backward-edge pointers (i.e.
return addresses in the stack) must be hidden as well, but this protection is
out of the scope of this plugin.

This plugin was written to support C binaries compiled for x86_64 architecture.

### KNOWN ISSUES: ###
 - This plugin was written as a POC and was never integrated with a major
   project.
 - Additional tests are required.
 - Trampoline functions are not very clean and contain unnecessary
   instructions. Further work is required to clean them up to contain the
   minimum instructions necessary.
