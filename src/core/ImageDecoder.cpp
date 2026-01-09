#include "ImageDecoder.h"
#include <vector>

static ImageDecoder::DecodeContext* g_ctx = nullptr;
PNG* ImageDecoder::currentPNG = nullptr;

bool ImageDecoder::decodeToDisplay(const char* path, BBEPAPER* bbep, uint16_t targetWidth, uint16_t targetHeight) {
    String p = String(path);
    p.toLowerCase();

    // Allocate everything on heap to keep stack usage minimal
    std::vector<int16_t> errorBuffer(targetWidth * 2, 0);
    DecodeContext* ctx = new DecodeContext();
    if (!ctx) return false;

    ctx->bbep = bbep;
    ctx->targetWidth = targetWidth;
    ctx->targetHeight = targetHeight;
    ctx->offsetX = 0;
    ctx->offsetY = 0;
    ctx->errorBuf = errorBuffer.data();
    ctx->success = false;
    g_ctx = ctx;

    if (p.endsWith(".jpg") || p.endsWith(".jpeg")) {
        JPEGDEC jpeg;
        File f = SD.open(path);
        if (!f) {
            delete ctx;
            g_ctx = nullptr;
            return false;
        }

        int rc = jpeg.open((void *)&f, (int)f.size(), [](void *p) { /* close */ }, 
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
            jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
            jpeg.setUserPointer(ctx);
            
            int iw = jpeg.getWidth();
            int ih = jpeg.getHeight();
            int scale = 0;

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

            ctx->offsetX = (targetWidth - iw) / 2;
            ctx->offsetY = (targetHeight - ih) / 2;
            
            if (jpeg.decode(ctx->offsetX, ctx->offsetY, scale)) {
                ctx->success = true;
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
            delete ctx;
            g_ctx = nullptr;
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
            ctx->offsetX = (targetWidth - png.getWidth()) / 2;
            ctx->offsetY = (targetHeight - png.getHeight()) / 2;
            
            rc = png.decode(ctx, 0);
            if (rc == PNG_SUCCESS) {
                ctx->success = true;
            }
            png.close();
        }
        f.close();
        currentPNG = nullptr;
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

    for (int y = 0; y < pDraw->iHeight; y++) {
        int targetY = pDraw->y + y;
        if (targetY < 0 || targetY >= ctx->targetHeight) continue;

        const uint16_t* pSrcRow = &pDraw->pPixels[y * pDraw->iWidth];

        for (int x = 0; x < pDraw->iWidth; x++) {
            int targetX = pDraw->x + x;
            if (targetX < 0 || targetX >= ctx->targetWidth) continue;

            uint16_t pixel = pSrcRow[x];
            uint8_t r = (pixel >> 11) & 0x1F; 
            uint8_t g = (pixel >> 5) & 0x3F;  
            uint8_t b = pixel & 0x1F;         
            
            // Integer-only luminance calculation to avoid FPU context issues in callbacks
            // Using coefficients scaled by 1024: R*306 + G*601 + B*117
            // r is 5 bits (0-31), g is 6 bits (0-63), b is 5 bits (0-31)
            // First scale them to 8 bits
            uint32_t r8 = (r * 255) / 31;
            uint32_t g8 = (g * 255) / 63;
            uint32_t b8 = (b * 255) / 31;
            uint32_t lum = (r8 * 306 + g8 * 601 + b8 * 117) >> 10;
            
            uint8_t color = (lum < 128) ? 0 : 1;
            ctx->bbep->drawPixel(targetX, targetY, color);
        }
    }
    return 1;
}

void ImageDecoder::PNGDraw(PNGDRAW *pDraw) {
    if (!pDraw || !g_ctx) return;
    DecodeContext *ctx = g_ctx; 
    
    if (!currentPNG || !ctx->bbep) return;
    
    uint16_t* usPixels = (uint16_t*)malloc(pDraw->iWidth * sizeof(uint16_t));
    if (!usPixels) return;
    
    currentPNG->getLineAsRGB565(pDraw, usPixels, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);

    int targetY = pDraw->y + ctx->offsetY;
    if (targetY < 0 || targetY >= ctx->targetHeight) {
        free(usPixels);
        return;
    }

    for (int x = 0; x < pDraw->iWidth; x++) {
        int targetX = x + ctx->offsetX;
        if (targetX < 0 || targetX >= ctx->targetWidth) continue;

        uint16_t pixel = usPixels[x];
        uint8_t r = (pixel >> 11) & 0x1F; 
        uint8_t g = (pixel >> 5) & 0x3F;  
        uint8_t b = pixel & 0x1F;         
        
        uint32_t r8 = (r * 255) / 31;
        uint32_t g8 = (g * 255) / 63;
        uint32_t b8 = (b * 255) / 31;
        uint32_t lum = (r8 * 306 + g8 * 601 + b8 * 117) >> 10;

        uint8_t color = (lum < 128) ? 0 : 1;
        ctx->bbep->drawPixel(targetX, targetY, color);
    }
    free(usPixels);
}
