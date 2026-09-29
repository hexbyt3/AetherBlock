#ifndef ERPT_CLEANUP_H
#define ERPT_CLEANUP_H

/* Delete the error reports Atmosphère saved to sd:/atmosphere/erpt_reports,
   so the next boot doesn't stall at the Nintendo logo while Atmosphère
   clears them itself. Returns how many were removed. */
int erptCleanupReports(void);

#endif
