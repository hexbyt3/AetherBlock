#ifndef APPLOG_H
#define APPLOG_H

/* Best-effort line logger. Appends to APP_LOG_PATH so a failed update
   leaves behind a human-readable reason instead of only an on-screen
   message. Any failure to write the log is silently ignored — logging
   must never affect the operation it is recording. */
void appLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Start a new titled section. Rotates (truncates) the log first if it
   has grown past APP_LOG_MAX_SIZE so it can't grow without bound. */
void appLogSection(const char *title);

#endif
