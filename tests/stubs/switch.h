/* Just enough of libnx for the download code to build on a PC (tests only). */
#ifndef TEST_SWITCH_STUB_H
#define TEST_SWITCH_STUB_H

#include <stdint.h>

typedef uint32_t Result;
typedef int64_t  s64;
typedef uint64_t u64;

#define R_FAILED(rc) ((rc) != 0)

enum { NcmStorageId_SdCard = 5 };

static inline Result fsdevCommitDevice(const char *name) { (void)name; return 0; }
static inline Result nsInitialize(void) { return 0; }
static inline void   nsExit(void) { }
static inline Result nsGetFreeSpaceSize(int id, s64 *out) { (void)id; *out = 0; return 0; }

#endif
