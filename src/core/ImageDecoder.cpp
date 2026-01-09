#include "ImageDecoder.h"

static ImageDecoder::DecodeContext* g_ctx = nullptr;
PNG* ImageDecoder::currentPNG = nullptr;

bool ImageDecoder::decodeToBW(const char* path, uint8_t* outBuffer, uint16_t targetWidth, uint16_t targetHeight) {
    String p = String(path);
    p.toLowerCase();

    DecodeContext ctx;
    ctx.outBuffer = outBuffer;
    ctx.targetWidth = targetWidth;
    ctx.targetHeight = targetHeight;
    ctx.offsetX = 0;
    ctx.offsetY = 0;
    ctx.success = false;
    g_ctx = &ctx;

    // Initialize buffer to white (0xFF)
    memset(outBuffer, 0xFF, (targetWidth * targetHeight) / 8);

    if (p.endsWith(".jpg") || p.endsWith(".jpeg")) {
        JPEGDEC jpeg;
        File f = SD.open(path);
        if (!f) return false;

        // Use manual callback-based open to avoid linkage issues with File-based open
        int rc = jpeg.open((void *)&f, (int)f.size(), [](void *p) { /* close */ }, 
                       [](JPEGFILE *pfn, uint8_t *pBuf, int32_t iLen) -> int32_t {
                           if (!pfn->fHandle) return -1;
                           return ((File *)pfn->fHandle)->read(pBuf, (size_t)iLen);
                       },
                       [](JPEGFILE *pfn, int32_t iPos) -> int32_t {
                           if (!pfn->fHandle) return -1;
                           return ((File *)pfn->fHandle)->seek((uint32_t)iPos) ? iPos : -1;
                       }, JPEGDraw);

        if (rc) {
            jpeg.setUserPointer(&ctx);
            
            int scale = 0;
            int iw = jpeg.getWidth();
            int ih = jpeg.getHeight();

            // Determine scaling factor (JPEGDEC supports 1/2, 1/4, 1/8)
            if (iw > targetWidth * 4 || ih > targetHeight * 4) {
                scale = JPEG_SCALE_EIGHTH;
                iw >>= 3; ih >>= 3;
            } else if (iw > targetWidth * 2 || ih > targetHeight * 2) {
                scale = JPEG_SCALE_QUARTER;
                iw >>= 2; ih >>= 2;
            } else if (iw > targetWidth || ih > targetHeight) {
                scale = JPEG_SCALE_HALF;
                iw >>= 1; ih >>= 1;
            }

            // Calculate centering offsets
            ctx.offsetX = (targetWidth - iw) / 2;
            ctx.offsetY = (targetHeight - ih) / 2;
            
            if (jpeg.decode(ctx.offsetX, ctx.offsetY, scale)) {
                ctx.success = true;
            }
            jpeg.close();
        }
        f.close();
    } else if (p.endsWith(".png")) {
        PNG png;
        currentPNG = &png;
        File f = SD.open(path);
        if (!f) {
            currentPNG = nullptr;
            return false;
        }
        
        int rc = png.open(path, [](const char *szFilename, int32_t *pFileSize) -> void * {
            File *file = new File(SD.open(szFilename));
            if (file && *file) {
                *pFileSize = (int32_t)file->size();
                return (void *)file;
            }
            if (file) delete file;
            return NULL;
        }, [](void *pHandle) {
            File *file = (File *)pHandle;
            if (file) {
                file->close();
                delete file;
            }
        }, [](PNGFILE *pfn, uint8_t *pBuffer, int32_t iLength) -> int32_t {
            if (!pfn->fHandle) return -1;
            File *file = (File *)pfn->fHandle;
            return (int32_t)file->read(pBuffer, (size_t)iLength);
        }, [](PNGFILE *pfn, int32_t iPos) -> int32_t {
            if (!pfn->fHandle) return -1;
            File *file = (File *)pfn->fHandle;
            return file->seek((uint32_t)iPos) ? iPos : -1;
        }, [](PNGDRAW *pDraw) -> int {
            PNGDraw(pDraw);
            return 1;
        });
        
        if (rc == PNG_SUCCESS) {
            // PNGdec doesn't have built-in scaling, so it will be drawn top-left or cropped
            ctx.offsetX = (targetWidth - png.getWidth()) / 2;
            ctx.offsetY = (targetHeight - png.getHeight()) / 2;
            
            rc = png.decode(&ctx, 0);
            if (rc == PNG_SUCCESS) {
                ctx.success = true;
            }
            png.close();
        }
        f.close();
        currentPNG = nullptr;
    }


    g_ctx = nullptr;
    return ctx.success;
}

int ImageDecoder::JPEGDraw(JPEGDRAW *pDraw) {
    DecodeContext *ctx = (DecodeContext *)pDraw->pUser;
    
    const int destStride = (ctx->targetWidth + 7) / 8;
    // Serial.printf("JPEGDraw: x=%d, y=%d, w=%d, h=%d, destStride=%d\n", pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, destStride);

    for (int y = 0; y < pDraw->iHeight; y++) {
        int targetY = pDraw->y + y;
        if (targetY >= ctx->targetHeight) break;

        for (int x = 0; x < pDraw->iWidth; x++) {
            int targetX = pDraw->x + x;
            if (targetX >= ctx->targetWidth) break;

            uint16_t pixel = pDraw->pPixels[y * pDraw->iWidth + x];
            
            // Extract RGB565 components
            uint8_t r = (pixel >> 11) & 0x1F; 
            uint8_t g = (pixel >> 5) & 0x3F;  
            uint8_t b = pixel & 0x1F;         
            
            // Standard Luminance: 0.299R + 0.587G + 0.114B (scaled to 0-255)
            float lum = (r * 8.22f * 0.299f) + (g * 4.04f * 0.587f) + (b * 8.22f * 0.114f);
            
            int byteIdx = (targetY * destStride) + (targetX / 8);
            int bitIdx = 7 - (targetX % 8);
            
            if (lum < 128) {
                // Black pixel (0 in E-Ink/SSD1677 based on EInkDisplay.cpp)
                ctx->outBuffer[byteIdx] &= ~(1 << bitIdx);
            } else {
                // White pixel (1 in E-Ink/SSD1677 based on EInkDisplay.cpp)
                ctx->outBuffer[byteIdx] |= (1 << bitIdx);
            }
        }
    }
    return 1;
}

void ImageDecoder::PNGDraw(PNGDRAW *pDraw) {
    DecodeContext *ctx = (DecodeContext *)pDraw->pUser;
    uint16_t usPixels[800]; 
    
    if (!currentPNG) return;
    
    currentPNG->getLineAsRGB565(pDraw, usPixels, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);

    const int destStride = (ctx->targetWidth + 7) / 8;

    int targetY = pDraw->y + ctx->offsetY;
    if (targetY < 0 || targetY >= ctx->targetHeight) return;

    for (int x = 0; x < pDraw->iWidth; x++) {
        int targetX = x + ctx->offsetX;
        if (targetX < 0 || targetX >= ctx->targetWidth) continue;

        uint16_t pixel = usPixels[x];
        uint8_t r = (pixel >> 11) & 0x1F; 
        uint8_t g = (pixel >> 5) & 0x3F;  
        uint8_t b = pixel & 0x1F;         
        
        float lum = (r * 8.22f * 0.299f) + (g * 4.04f * 0.587f) + (b * 8.22f * 0.114f);

        int byteIdx = (targetY * destStride) + (targetX / 8);
        int bitIdx = 7 - (targetX % 8);

        if (lum < 128) {
            // Black pixel (0 in E-Ink/SSD1677)
            ctx->outBuffer[byteIdx] &= ~(1 << bitIdx);
        } else {
            // White pixel (1 in E-Ink/SSD1677)
            ctx->outBuffer[byteIdx] |= (1 << bitIdx);
        }
    }
}

