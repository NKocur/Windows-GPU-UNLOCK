/* RAM-only mocks; never linked into firmware. */
#include "pci-fixture.h"
static EFI_STATUS MixedMockLocate(UINTN *,EFI_HANDLE **);
static EFI_STATUS MixedMockHandle(EFI_HANDLE,EFI_GUID *,VOID **);
static VOID MixedMockReset(VOID);
static VOID MixedMockEvent(const char *);
static VOID MixedMockStopped(int);
static VOID MixedMockFree(VOID);
static VOID MixedMockBoundary(const char *);
static BOOLEAN MixedMockCompanionGop(VOID);
