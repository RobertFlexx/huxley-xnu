# Security boundary of the initial changes

Inherited memory isolation, credential/MAC checks, code-signing enforcement,
kernel extension policy, allocator hardening and architecture mitigations are
unchanged. No release protection is disabled to obtain diagnostic output.
The firmware diagnostic is an isolated QEMU application, not a production
trust policy or loader. Unsigned test firmware execution says nothing about
HXNU secure boot or module trust.

| Threat/input | Current handling | Remaining work |
| --- | --- | --- |
| Corrupt boot-map stride/length/address arithmetic | Metadata rejected before mapping or descriptor reads | Trusted loader must guarantee the physical buffer is mapped/readable |
| Invalid range, overlap, page/accounting overflow | Whole map checked before PMAP intake; index/status panic | Reservation completeness, map lifetime and mutation remain loader contracts |
| EFI runtime page-count narrowing, shadow mapping overflow or physical-mask alias | Runtime descriptors checked against actual EFI mapping arithmetic and page-table mask | CPU-specific physical width, firmware runtime protections and real kernel mapping tests remain outstanding |
| Hibernation's NVS/PAL page-number narrowing | Bounds enforced alongside PMAP's usable classes | Actual hibernation execution remains untested |
| KASAN's earlier map reads and runtime memory allocation | Independent validation before shadow reservation; runtime ranges excluded; VM revalidates after mutation | Full KASAN build and execution blocked by SDK/generated headers |
| Bootstrap-page exposure or PMAP split overrun | Inclusive `first_avail` boundary corrected; two slots required before split writes | Source-reviewed consumer fixes require real VM compilation and runtime tests |
| Unterminated early boot command line | Bounded binder rejects missing NUL before serial parsing; unbound diagnostic parsing is safe | Readable pointer, full boot metadata and device-tree validation remain loader requirements |
| Invalid serial baud or absent UART | Safe baud fallback, scratch probe and failed initialization; explicit legacy avoids MMIO | Polled transmit still has inherited unbounded hardware wait; timeout design needs clock/console semantics review |
| Malicious userspace/syscall data | Existing copyin/copyout, credentials and Mach/BSD validation retained | Runtime syscall, IPC and exhaustion tests blocked by no userspace |
| Hostile devices/compromised drivers | Existing I/O Kit ownership/DMA/interrupt facilities retained | Generic drivers, IOMMU and teardown/failure tests not implemented |
| Malformed filesystems/executables | Existing VFS/Mach-O/trust checks retained | Root implementation and independent trust providers need real negative tests |
| Kernel memory corruption/concurrency | No scheduler, locks or object lifetimes changed; host validator/serial tests use ASan/UBSan | Full kernel sanitizer, SMP and stress tests remain blocked |

Validation is not authentication. A malicious loader already controls mappings
and kernel bytes; this parser cannot establish a trust root or safely dereference
an arbitrary unmapped address. The map remains loader-owned/readable during
validation and kernel-owned/reserved throughout later EFI/hibernate consumers.
No allocation, reference lifetime, lock or performance-critical indirect call
was added to Mach paths.
The checks cover initial map intake, not hibernation's independently supplied
replacement map or a general guarantee of safe firmware runtime services.
The wider hibernation/panic consumer audit is incomplete: bitmap sizing in
`hibernate_i386.c` still has uint32 rounding/accounting limits, and
`panic_hooks.c` uses an exclusive physical endpoint that can wrap for the
accepted final reserved page. Width validation does not establish complete
hibernation arithmetic safety or correct diagnostics at that extreme.

Generic platforms must replace unavailable AMFI/Image4/CoreTrust integration
with a reviewed policy/provider rather than successful stubs. They also need
real entropy, runtime memory protections and deliberate debug access policy.
Development diagnostics must remain explicit and local; no remote debug service
was enabled here.
