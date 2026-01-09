#ifndef IMAGE_DECODER_H
#define IMAGE_DECODER_H

#include <Arduino.h>
#include <SD.h>

#include <JPEGDEC.h>
#undef MOTOSHORT
#undef MOTOLONG
#undef INTELSHORT
#undef INTELLONG
#include <PNGdec.h>

class ImageDecoder {
public:
    struct DecodeContext {
        uint8_t* outBuffer;
        uint16_t targetWidth;
        uint16_t targetHeight;
        bool success;
    };
    /**
     * @brief Decodes a JPEG or PNG file from SD card into a 1-bit BW buffer.
     * 
     * @param path Path to the image file on SD card.
     * @param outBuffer Buffer to store the decoded 1-bit data (must be at least 800x480/8 bytes).
     * @param targetWidth Target width (800 for current display).
     * @param targetHeight Target height (480 for current display).
     * @return true if decoding was successful.
     */
    static bool decodeToBW(const char* path, uint8_t* outBuffer, uint16_t targetWidth, uint16_t targetHeight);

private:
    static PNG* currentPNG;
    static int JPEGDraw(JPEGDRAW *pDraw);
    
    // PNGdec callbacks
    static void PNGDraw(PNGDRAW *pDraw);
};

#endif // IMAGE_DECODER_H
