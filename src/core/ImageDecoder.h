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
        uint8_t* frameBuffer;
        uint16_t targetWidth;
        uint16_t targetHeight;
        int16_t offsetX;
        int16_t offsetY;
        uint16_t decodedWidth;
        uint16_t decodedHeight;
        uint16_t renderWidth;
        uint16_t renderHeight;
        bool rotateSource90;
        bool scaleToWidth;
        int16_t* errorBuf;
        uint16_t* pngLineBuf;
        size_t pngLineBufPixels;
        bool success;
    };
    /**
     * @brief Decodes a JPEG or PNG file from SD card using BBEPAPER driver.
     * 
     * @param path Path to the image file on SD card.
     * @param bbep Pointer to BBEPAPER driver instance.
     * @param frameBuffer Pointer to raw 1-bit framebuffer (800x480).
     * @param targetWidth Target width (800 for current display).
     * @param targetHeight Target height (480 for current display).
     * @return true if decoding was successful.
     */
    static bool decodeToDisplay(const char* path, uint8_t* frameBuffer, uint16_t targetWidth, uint16_t targetHeight);

    static bool decodeToDisplayFitWidth(const char* path, uint8_t* frameBuffer, uint16_t targetWidth, uint16_t targetHeight);

private:
    static bool decodeBMPToDisplay(const char* path, DecodeContext* ctx);
    static PNG* currentPNG;
    static int JPEGDraw(JPEGDRAW *pDraw);
    
    // PNGdec callbacks
    static void PNGDraw(PNGDRAW *pDraw);
};

#endif // IMAGE_DECODER_H
