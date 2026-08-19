#ifndef APPLESCREEN_LOG_H
#define APPLESCREEN_LOG_H

// Injected code has no attached console - this is the only way to see what
// the dylib is doing once it's living inside the Minecraft/JVM process.
void applescreen_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* APPLESCREEN_LOG_H */
