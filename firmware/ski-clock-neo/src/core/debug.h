#ifndef DEBUG_H
#define DEBUG_H

// Debug logging control
// Define RELEASE_BUILD to disable all debug output (used by promote-prod.yml)
#ifndef RELEASE_BUILD
  #define DEBUG_LOGGING
#endif

#ifdef DEBUG_LOGGING
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
