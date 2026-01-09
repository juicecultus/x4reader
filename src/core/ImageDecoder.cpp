#include "ImageDecoder.h"
#include <vector>

static ImageDecoder::DecodeContext* g_ctx = nullptr;
PNG* ImageDecoder::currentPNG = nullptr;

bool ImageDecoder::decodeToDisplay(const char* path, BBEPAPER* bbep, uint8_t* frameBuffer, uint16_t targetWidth, uint16_t targetHeight) {
    String p = String(path);
    p.toLowerCase();

    // Allocate everything on heap to keep stack usage minimal
    std::vector<int16_t> errorBuffer(targetWidth * 2, 0);
    DecodeContext* ctx = new DecodeContext();
    if (!ctx) return false;

    ctx->bbep = bbep;
    ctx->frameBuffer = frameBuffer;
    ctx->targetWidth = targetWidth;
    ctx->targetHeight = targetHeight;
    ctx->offsetX = 0;
    ctx->offsetY = 0;
    ctx->errorBuf = errorBuffer.data();
    ctx->success = false;
    g_ctx = ctx;

    if (p.endsWith(".jpg") || p.endsWith(".jpeg")) {
        JPEGDEC* jpeg = new JPEGDEC();
        if (!jpeg) {
            delete ctx;
            g_ctx = nullptr;
            return false;
        }
        File f = SD.open(path);
        if (!f) {
            Serial.printf("ImageDecoder: Failed to open %s\n", path);
            delete jpeg;
            delete ctx;
            g_ctx = nullptr;
            return false;
        }

        // Use manual callback-based open
        int rc = jpeg->open((void *)&f, (int)f.size(), [](void *p) { /* close */ }, 
                       [](JPEGFILE *pfn, uint8_t *pBuf, int32_t iLen) -> int32_t {
                           if (!pfn || !pfn->fHandle) return -1;
                           File *file = (File *)pfn->fHandle;
                           return (int32_t)file->read(pBuf, (size_t)iLen);
                       },
                       [](JPEGFILE *pfn, int32_t iPos) -> int32_t {
                           if (!pfn || !pfn->fHandle) return -1;
                           File *file = (File *)pfn->fHandle;
                           return file->seek((uint32_t)iPos) ? 1 : 0;
                       }, JPEGDraw);

        if (rc) {
            jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
            jpeg->setUserPointer(ctx);
            
            const int srcW = jpeg->getWidth();
            const int srcH = jpeg->getHeight();

            // "Fit" strategy: choose the least downscale that makes the decoded
            // image fit within the target dimensions, then center it (letterbox).
            // NOTE: JPEGDEC can only downscale by powers of two.
            struct ScaleOpt { int opt; int shift; };
            const ScaleOpt opts[] = {
                {0, 0},
                {JPEG_SCALE_HALF, 1},
                {JPEG_SCALE_QUARTER, 2},
                {JPEG_SCALE_EIGHTH, 3},
            };

            // Find first option that fits. If none fit, fall back to the smallest.
            int scale = JPEG_SCALE_EIGHTH;
            int outW = srcW >> 3;
            int outH = srcH >> 3;

            for (size_t i = 0; i < (sizeof(opts) / sizeof(opts[0])); i++) {
                const int w = srcW >> opts[i].shift;
                const int h = srcH >> opts[i].shift;
                if (w <= (int)targetWidth && h <= (int)targetHeight) {
                    scale = opts[i].opt;
                    outW = w;
                    outH = h;
                    break;
                }
            }

            // Center (letterbox/pillarbox). Clamp to >= 0 to avoid accidental cropping.
            ctx->offsetX = ((int)targetWidth - outW) / 2;
            ctx->offsetY = ((int)targetHeight - outH) / 2;
            if (ctx->offsetX < 0) ctx->offsetX = 0;
            if (ctx->offsetY < 0) ctx->offsetY = 0;
            
            jpeg->setMaxOutputSize(1); 

            Serial.printf("ImageDecoder: Decoding JPEG %dx%d to display at offset %d,%d scale %d\n", 
                          jpeg->getWidth(), jpeg->getHeight(), ctx->offsetX, ctx->offsetY, scale);

            if (jpeg->decode(ctx->offsetX, ctx->offsetY, scale)) {
                ctx->success = true;
                Serial.println("ImageDecoder: JPEG decode successful");
            } else {
                Serial.printf("ImageDecoder: JPEG decode failed, error %d\n", jpeg->getLastError());
            }
            jpeg->close();
        } else {
            Serial.printf("ImageDecoder: JPEG open failed, error %d\n", jpeg->getLastError());
        }
        f.close();
        delete jpeg;
    } else if (p.endsWith(".png")) {
        PNG* png = new PNG();
        if (!png) {
            delete ctx;
            g_ctx = nullptr;
            return false;
        }
        currentPNG = png;
        File f = SD.open(path);
        if (!f) {
            Serial.printf("ImageDecoder: Failed to open %s\n", path);
            currentPNG = nullptr;
            delete png;
            delete ctx;
            g_ctx = nullptr;
            return false;
        }
        
        int rc = png->open(path, [](const char *szFilename, int32_t *pFileSize) -> void * {
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
            if (!pfn || !pfn->fHandle) return -1;
            File *file = (File *)pfn->fHandle;
            return (int32_t)file->read(pBuffer, (size_t)iLength);
        }, [](PNGFILE *pfn, int32_t iPos) -> int32_t {
            if (!pfn || !pfn->fHandle) return -1;
            File *file = (File *)pfn->fHandle;
            return file->seek((uint32_t)iPos) ? iPos : -1;
        }, [](PNGDRAW *pDraw) -> int {
            PNGDraw(pDraw);
            return 1;
        });
        
        if (rc == PNG_SUCCESS) {
            ctx->offsetX = (targetWidth - png->getWidth()) / 2;
            ctx->offsetY = (targetHeight - png->getHeight()) / 2;
            
            Serial.printf("ImageDecoder: Decoding PNG %dx%d to display at offset %d,%d\n", 
                          png->getWidth(), png->getHeight(), ctx->offsetX, ctx->offsetY);

            rc = png->decode(ctx, 0);
            if (rc == PNG_SUCCESS) {
                ctx->success = true;
                Serial.println("ImageDecoder: PNG decode successful");
            } else {
                Serial.printf("ImageDecoder: PNG decode failed, error %d\n", png->getLastError());
            }
            png->close();
        } else {
            Serial.printf("ImageDecoder: PNG open failed, error %d\n", png->getLastError());
        }
        f.close();
        currentPNG = nullptr;
        delete png;
    }

    bool result = ctx->success;
    delete ctx;
    g_ctx = nullptr;
    return result;
}

int ImageDecoder::JPEGDraw(JPEGDRAW *pDraw) {
    if (!pDraw || !g_ctx || !pDraw->pPixels) return 0;
    DecodeContext *ctx = g_ctx; 
    
    if (!ctx->bbep) return 0;

    // NOTE: JPEGDEC invokes this callback in MCU blocks, not strict scanlines.
    // Error-diffusion dithering assumes left-to-right row order and causes heavy
    // streaking/corruption when applied to block callbacks. Use simple thresholding.
    // Framebuffer is 800x480 (landscape). UI is portrait logical 480x800.
    // Map portrait (px, py) -> framebuffer (fx, fy) as:
    //   fx = py
    //   fy = 479 - px
    // This matches EInkDisplay::saveFrameBufferAsPBM() rotation.
    for (int y = 0; y < pDraw->iHeight; y++) {
        int py = pDraw->y + y;
        if (py < 0 || py >= (int)ctx->targetHeight) continue;

        const uint16_t* pSrcRow = pDraw->pPixels + (y * pDraw->iWidth);

        for (int x = 0; x < pDraw->iWidth; x++) {
            int px = pDraw->x + x;
            if (px < 0 || px >= (int)ctx->targetWidth) continue;

            const int fx = py;
            const int fy = 479 - px;
            if (fx < 0 || fx >= 800 || fy < 0 || fy >= 480) continue;

            uint16_t pixel = pSrcRow[x];
            uint8_t r = (pixel >> 11) & 0x1F;
            uint8_t g = (pixel >> 5) & 0x3F;
            uint8_t b = pixel & 0x1F;

            uint32_t r8 = (r * 255) / 31;
            uint32_t g8 = (g * 255) / 63;
            uint32_t b8 = (b * 255) / 31;
            uint32_t lum = (r8 * 306 + g8 * 601 + b8 * 117) >> 10;

            uint8_t color = (lum < 128) ? 0 : 1;

            if (ctx->frameBuffer) {
                int byteIdx = (fy * 100) + (fx / 8);
                int bitIdx = 7 - (fx % 8);
                if (color == 0) {
                    ctx->frameBuffer[byteIdx] &= ~(1 << bitIdx);
                } else {
                    ctx->frameBuffer[byteIdx] |= (1 << bitIdx);
                }
            } else {
                ctx->bbep->drawPixel(fx, fy, color);
            }
        }
    }
    return 1;
}

void ImageDecoder::PNGDraw(PNGDRAW *pDraw) {
    if (!pDraw || !g_ctx) return;
    DecodeContext *ctx = g_ctx; 
    
    if (!currentPNG || !ctx->bbep || !ctx->errorBuf) return;
    
    uint16_t* usPixels = (uint16_t*)malloc(pDraw->iWidth * sizeof(uint16_t));
    if (!usPixels) return;
    
    currentPNG->getLineAsRGB565(pDraw, usPixels, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);

    int py = pDraw->y + ctx->offsetY;
    if (py < 0 || py >= (int)ctx->targetHeight) {
        free(usPixels);
        return;
    }

    const int w = (int)ctx->targetWidth;
    int16_t* curErr = &ctx->errorBuf[(py % 2) * w];
    int16_t* nxtErr = &ctx->errorBuf[((py + 1) % 2) * w];
    if (py + 1 < (int)ctx->targetHeight) {
        memset(nxtErr, 0, (size_t)w * sizeof(int16_t));
    }

    for (int x = 0; x < pDraw->iWidth; x++) {
        int px = x + ctx->offsetX;
        if (px < 0 || px >= (int)ctx->targetWidth) continue;

        const int fx = py;
        const int fy = 479 - px;
        if (fx < 0 || fx >= 800 || fy < 0 || fy >= 480) continue;

        uint16_t pixel = usPixels[x];
        uint8_t r = (pixel >> 11) & 0x1F; 
        uint8_t g = (pixel >> 5) & 0x3F;  
        uint8_t b = pixel & 0x1F;         
        
        uint32_t r8 = (r * 255) / 31;
        uint32_t g8 = (g * 255) / 63;
        uint32_t b8 = (b * 255) / 31;
        uint32_t lum = (r8 * 306 + g8 * 601 + b8 * 117) >> 10;

        int16_t gray = (int16_t)lum + curErr[px];
        if (gray < 0) gray = 0;
        else if (gray > 255) gray = 255;

        uint8_t color = (gray < 128) ? 0 : 1;
        
        if (ctx->frameBuffer) {
            int byteIdx = (fy * 100) + (fx / 8);
            int bitIdx = 7 - (fx % 8);
            if (color == 0) {
                ctx->frameBuffer[byteIdx] &= ~(1 << bitIdx);
            } else {
                ctx->frameBuffer[byteIdx] |= (1 << bitIdx);
            }
        } else {
            ctx->bbep->drawPixel(fx, fy, color);
        }

        int16_t err = gray - (color ? 255 : 0);
        if (px + 1 < w) curErr[px + 1] += (err * 7) / 16;
        if (py + 1 < (int)ctx->targetHeight) {
            if (px > 0) nxtErr[px - 1] += (err * 3) / 16;
            nxtErr[px] += (err * 5) / 16;
            if (px + 1 < w) nxtErr[px + 1] += (err * 1) / 16;
        }
    }
    free(usPixels);
}
