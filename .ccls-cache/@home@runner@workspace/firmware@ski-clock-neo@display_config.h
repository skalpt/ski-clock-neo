#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

// ==================== HARDWARE CONFIGURATION ====================
// This file defines the physical display hardware configuration.
// Change these values to match your actual hardware setup.
// This header is included by both the renderer (neopixel_render.h, hub75_render.h)
// and the display library (display.h, display.cpp) to ensure consistent sizing.

#define DISPLAY_ROWS    2       // Number of physical display rows
#define PANELS_PER_ROW  3       // Number of panels per row
#define PANEL_WIDTH     16      // Width of each panel in pixels
#define PANEL_HEIGHT    16      // Height of each panel in pixels

// Calculate total dimensions
#define ROW_WIDTH (PANELS_PER_ROW * PANEL_WIDTH)    // 3 * 16 = 48 pixels
#define ROW_HEIGHT PANEL_HEIGHT                      // 16 pixels

// Calculate buffer size needed for this hardware configuration
// Bit-packed: (2 rows * 48 width * 16 height) / 8 bits/byte = 192 bytes
#define DISPLAY_BUFFER_SIZE ((DISPLAY_ROWS * ROW_WIDTH * ROW_HEIGHT) / 8)

#endif
