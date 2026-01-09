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
                           return ((File *)pfn->fHandle)->read(pBuf, (size_t)iLen);
                       },
                       [](JPEGFILE *pfn, int32_t iPos) -> int32_t {
                           if (!pfn || !pfn->fHandle) return -1;
                           return ((File *)pfn->fHandle)->seek((uint32_t)iPos) ? iPos : -1;
                       }, JPEGDraw);

        if (rc) {
            jpeg.setUserPointer(ctx);
            
            int scale = 0;
            int iw = jpeg.getWidth();
            int ih = jpeg.getHeight();

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
    if (!pDraw || !pDraw->pUser) return 0;
    DecodeContext *ctx = (DecodeContext *)pDraw->pUser;
    
    if (!ctx->bbep || !ctx->errorBuf) return 0;

    // Boundary check for tile coordinates
    if (pDraw->x < 0 || pDraw->y < 0) return 1;

    for (int y = 0; y < pDraw->iHeight; y++) {
        int targetY = pDraw->y + y;
        if (targetY >= ctx->targetHeight) break;

        // Current and next error row pointers
        int16_t* curErr = &ctx->errorBuf[(targetY % 2) * ctx->targetWidth];
        int16_t* nxtErr = &ctx->errorBuf[((targetY + 1) % 2) * ctx->targetWidth];
        
        // Clear next error row at start of its row
        if (pDraw->x == 0 && y == 0 && targetY < ctx->targetHeight - 1) {
            memset(nxtErr, 0, ctx->targetWidth * sizeof(int16_t));
        }

        for (int x = 0; x < pDraw->iWidth; x++) {
            int targetX = pDraw->x + x;
            if (targetX >= ctx->targetWidth) break;

            if (!pDraw->pPixels) return 0; // Extreme safety
            uint16_t pixel = pDraw->pPixels[y * pDraw->iWidth + x];
            uint8_t r = (pixel >> 11) & 0x1F; 
            uint8_t g = (pixel >> 5) & 0x3F;  
            uint8_t b = pixel & 0x1F;         
            
            float lum = (r * 8.22f * 0.299f) + (g * 4.04f * 0.587f) + (b * 8.22f * 0.114f);
            
            // Apply error from previous pixels
            int16_t gray = (int16_t)lum + curErr[targetX];
            if (gray < 0) gray = 0;
            if (gray > 255) gray = 255;

            uint8_t color = (gray < 128) ? 0 : 1;
            ctx->bbep->drawPixel(targetX, targetY, color);

            // Calculate error
            int16_t err = gray - (color ? 255 : 0);

            // Distribute error (Floyd-Steinberg)
            if (targetX + 1 < ctx->targetWidth) curErr[targetX + 1] += (err * 7) / 16;
            if (targetY + 1 < ctx->targetHeight) {
                if (targetX > 0) nxtErr[targetX - 1] += (err * 3) / 16;
                nxtErr[targetX] += (err * 5) / 16;
                if (targetX + 1 < ctx->targetWidth) nxtErr[targetX + 1] += (err * 1) / 16;
            }
        }
    }
    return 1;
}

void ImageDecoder::PNGDraw(PNGDRAW *pDraw) {
    if (!pDraw || !pDraw->pUser) return;
    DecodeContext *ctx = (DecodeContext *)pDraw->pUser;
    
    if (!currentPNG || !ctx->bbep || !ctx->errorBuf) return;
    
    // Allocate line buffer on heap to avoid stack overflow
    uint16_t* usPixels = (uint16_t*)malloc(pDraw->iWidth * sizeof(uint16_t));
    if (!usPixels) return;
    
    currentPNG->getLineAsRGB565(pDraw, usPixels, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);

    int targetY = pDraw->y + ctx->offsetY;
    if (targetY < 0 || targetY >= ctx->targetHeight) {
        free(usPixels);
        return;
    }

    int16_t* curErr = &ctx->errorBuf[(targetY % 2) * ctx->targetWidth];
    int16_t* nxtErr = &ctx->errorBuf[((targetY + 1) % 2) * ctx->targetWidth];
    
    // PNGdec processes row by row, so we can clear the next error row safely here
    if (targetY < ctx->targetHeight - 1) {
        memset(nxtErr, 0, ctx->targetWidth * sizeof(int16_t));
    }

    for (int x = 0; x < pDraw->iWidth; x++) {
        int targetX = x + ctx->offsetX;
        if (targetX < 0 || targetX >= ctx->targetWidth) continue;

        uint16_t pixel = usPixels[x];
        uint8_t r = (pixel >> 11) & 0x1F; 
        uint8_t g = (pixel >> 5) & 0x3F;  
        uint8_t b = pixel & 0x1F;         
        
        float lum = (r * 8.22f * 0.299f) + (g * 4.04f * 0.587f) + (b * 8.22f * 0.114f);

        int16_t gray = (int16_t)lum + curErr[targetX];
        if (gray < 0) gray = 0;
        if (gray > 255) gray = 255;

        uint8_t color = (gray < 128) ? 0 : 1;
        ctx->bbep->drawPixel(targetX, targetY, color);

        int16_t err = gray - (color ? 255 : 0);
        if (targetX + 1 < ctx->targetWidth) curErr[targetX + 1] += (err * 7) / 16;
        if (targetY + 1 < ctx->targetHeight) {
            if (targetX > 0) nxtErr[targetX - 1] += (err * 3) / 16;
            nxtErr[targetX] += (err * 5) / 16;
            if (targetX + 1 < ctx->targetWidth) nxtErr[targetX + 1] += (err * 1) / 16;
        }
    }
    free(usPixels);
}
