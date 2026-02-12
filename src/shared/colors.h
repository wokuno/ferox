#ifndef FEROX_COLORS_H
#define FEROX_COLORS_H

#include "types.h"

// Convert HSV (h: 0-360, s: 0-1, v: 0-1) to RGB
Color hsv_to_rgb(float h, float s, float v);

// Generate a random vibrant color for colony body
Color generate_body_color(void);

// Generate a contrasting border color for a given body color
Color generate_border_color(Color body_color);

// Calculate color distance (Euclidean in RGB space)
float color_distance(Color c1, Color c2);

// Clamp a value to uint8_t range
uint8_t clamp_u8(int value);

// Blend two colors
static inline Color color_blend(Color a, Color b, float weight_a) {
    Color c;
    float weight_b = 1.0f - weight_a;
    c.r = (uint8_t)(a.r * weight_a + b.r * weight_b);
    c.g = (uint8_t)(a.g * weight_a + b.g * weight_b);
    c.b = (uint8_t)(a.b * weight_a + b.b * weight_b);
    return c;
}

#endif // FEROX_COLORS_H
