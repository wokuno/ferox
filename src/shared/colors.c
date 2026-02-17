#include "colors.h"
#include "utils.h"
#include <math.h>

uint8_t clamp_u8(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

Color hsv_to_rgb(float h, float s, float v) {
    Color result = {0, 0, 0};
    
    // Normalize h to [0, 360)
    while (h < 0) h += 360.0f;
    while (h >= 360.0f) h -= 360.0f;
    
    // Clamp s and v to [0, 1]
    if (s < 0) s = 0;
    if (s > 1) s = 1;
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    
    float r_prime, g_prime, b_prime;
    
    if (h < 60) {
        r_prime = c; g_prime = x; b_prime = 0;
    } else if (h < 120) {
        r_prime = x; g_prime = c; b_prime = 0;
    } else if (h < 180) {
        r_prime = 0; g_prime = c; b_prime = x;
    } else if (h < 240) {
        r_prime = 0; g_prime = x; b_prime = c;
    } else if (h < 300) {
        r_prime = x; g_prime = 0; b_prime = c;
    } else {
        r_prime = c; g_prime = 0; b_prime = x;
    }
    
    result.r = clamp_u8((int)((r_prime + m) * 255.0f));
    result.g = clamp_u8((int)((g_prime + m) * 255.0f));
    result.b = clamp_u8((int)((b_prime + m) * 255.0f));
    
    return result;
}

Color generate_body_color(void) {
    // Generate vibrant colors using HSV
    // Random hue, high saturation, medium-high value
    float h = rand_float() * 360.0f;
    float s = 0.6f + rand_float() * 0.4f;  // 0.6 - 1.0
    float v = 0.5f + rand_float() * 0.4f;  // 0.5 - 0.9
    
    return hsv_to_rgb(h, s, v);
}

Color generate_border_color(Color body_color) {
    // Create a contrasting border by adjusting brightness
    // Lighter body -> darker border, darker body -> lighter border
    float brightness = (body_color.r + body_color.g + body_color.b) / 3.0f;
    
    Color border;
    if (brightness > 127) {
        // Darken for light bodies
        border.r = clamp_u8((int)(body_color.r * 0.4f));
        border.g = clamp_u8((int)(body_color.g * 0.4f));
        border.b = clamp_u8((int)(body_color.b * 0.4f));
    } else {
        // Lighten for dark bodies
        border.r = clamp_u8((int)(body_color.r * 1.6f + 60));
        border.g = clamp_u8((int)(body_color.g * 1.6f + 60));
        border.b = clamp_u8((int)(body_color.b * 1.6f + 60));
    }
    
    return border;
}

float color_distance(Color c1, Color c2) {
    // Euclidean distance in RGB space
    float dr = (float)c1.r - (float)c2.r;
    float dg = (float)c1.g - (float)c2.g;
    float db = (float)c1.b - (float)c2.b;
    
    return sqrtf(dr * dr + dg * dg + db * db);
}
