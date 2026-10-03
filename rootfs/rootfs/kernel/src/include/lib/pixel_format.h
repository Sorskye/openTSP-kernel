#pragma once

#ifndef PIXEL_FORMAT_H
#define PIXEL_FORMAT_H

struct pixel_format {
    uint8_t bpp;
    uint8_t red_pos, red_bits;
    uint8_t green_pos, green_bits;
    uint8_t blue_pos, blue_bits;
};

#endif