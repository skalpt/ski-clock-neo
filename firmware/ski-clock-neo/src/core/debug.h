#ifndef DEBUG_H
#define DEBUG_H

// Debug logging control
// - Enabled by default for development builds
// - Disabled when RELEASE_BUILD is defined (used by promote-prod.yml)
// - Can be overridden with -DDEBUG_LOGGING=1 for troubleshooting release builds
#ifndef DEBUG_LOGGING
  #ifdef RELEASE_BUILD
    #define DEBUG_LOGGING 0
  #else
    #define DEBUG_LOGGING 1
  #endif
#endif

#if DEBUG_LOGGING
  #define DEBUG_BEGIN(baud) Serial.begin(baud)
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_BEGIN(baud)
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

#endif
