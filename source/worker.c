#include "worker.h"
#include "download.h"
#include <switch.h>
#include <pthread.h>

#define MAX_WORKERS 16

typedef struct {
    pthread_t      thread;
    bool           used;
    volatile bool  done;
    void        *(*fn)(void *);
    void          *arg;
} Slot;

static Slot s_slots[MAX_WORKERS];

static void *trampoline(void *p) {
    Slot *slot = p;
    slot->fn(slot->arg);
    slot->done = true;
    return NULL;
}

static void reap(bool wait_for_all) {
    for (int i = 0; i < MAX_WORKERS; i++) {
        Slot *slot = &s_slots[i];
        if (slot->used && (slot->done || wait_for_all)) {
            pthread_join(slot->thread, NULL);
            slot->used = false;
        }
    }
}

bool workerStart(void *(*fn)(void *), void *arg) {
    reap(false);

    for (int i = 0; i < MAX_WORKERS; i++) {
        Slot *slot = &s_slots[i];
        if (slot->used) continue;

        slot->fn = fn;
        slot->arg = arg;
        slot->done = false;
        if (pthread_create(&slot->thread, NULL, trampoline, slot) != 0)
            return false;
        slot->used = true;
        return true;
    }
    return false;
}

void workerShutdown(void) {
    downloadAbortAll();
    reap(true);
}

typedef struct { u64 addr, size; } Range;

#define MAX_RANGES 32

int workerReleaseLeakedStacks(void) {
    if (!envHasHeapOverride()) return 0;

    const u64 heap = (u64)envGetHeapOverrideAddr();
    const u64 heap_end = heap + envGetHeapOverrideSize();

    /* heap pages that are still lent out as somebody's stack */
    Range locked[MAX_RANGES];
    int nlocked = 0;
    for (u64 a = heap; a < heap_end; ) {
        MemoryInfo m;
        u32 page;
        if (R_FAILED(svcQueryMemory(&m, &page, a)) || m.size == 0) break;
        u64 start = m.addr < heap ? heap : m.addr;
        u64 end = m.addr + m.size > heap_end ? heap_end : m.addr + m.size;
        if ((m.type & 0xFF) == MemType_Heap && (m.attr & MemAttr_IsBorrowed) &&
            m.perm == Perm_None && nlocked < MAX_RANGES)
            locked[nlocked++] = (Range){ start, end - start };
        a = m.addr + m.size;
    }
    if (nlocked == 0) return 0;

    /* every stack mapping in the process; the dead threads' are among them */
    Range stacks[MAX_RANGES];
    int nstacks = 0;
    u64 a = 0;
    for (int guard = 0; guard < 8192; guard++) {
        MemoryInfo m;
        u32 page;
        if (R_FAILED(svcQueryMemory(&m, &page, a)) || m.size == 0) break;
        if ((m.type & 0xFF) == MemType_MappedMemory && nstacks < MAX_RANGES)
            stacks[nstacks++] = (Range){ m.addr, m.size };
        u64 next = m.addr + m.size;
        if (next <= a) break;   /* wrapped past the end of the address space */
        a = next;
    }

    /* Pair them up a page at a time. The kernel only unmaps when the stack
       page really is the alias of that heap page, so a wrong guess (or a live
       stack that has nothing to do with our heap) just returns an error. */
    int released = 0;
    for (int i = 0; i < nstacks; i++) {
        u64 next_src = 0;
        for (u64 dst = stacks[i].addr; dst < stacks[i].addr + stacks[i].size; dst += 0x1000) {
            if (next_src && R_SUCCEEDED(svcUnmapMemory((void *)dst, (void *)next_src, 0x1000))) {
                next_src += 0x1000;
                released++;
                continue;
            }
            next_src = 0;
            for (int j = 0; j < nlocked && !next_src; j++) {
                for (u64 src = locked[j].addr; src < locked[j].addr + locked[j].size; src += 0x1000) {
                    if (R_SUCCEEDED(svcUnmapMemory((void *)dst, (void *)src, 0x1000))) {
                        next_src = src + 0x1000;
                        released++;
                        break;
                    }
                }
            }
        }
    }
    return released;
}
