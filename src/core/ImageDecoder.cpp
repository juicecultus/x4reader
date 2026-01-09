#include "ImageDecoder.h"

static ImageDecoder::DecodeContext* g_ctx = nullptr;
PNG* ImageDecoder::currentPNG = nullptr;

bool ImageDecoder::decodeToDisplay(const char* path, BBEPAPER* bbep, uint16_t targetWidth, uint16_t targetHeight) {
    String p = String(path);
    p.toLowerCase();

    DecodeContext ctx;
    ctx.bbep = bbep;
    ctx.targetWidth = targetWidth;
    ctx.targetHeight = targetHeight;
    ctx.offsetX = 0;
    ctx.offsetY = 0;
    ctx.success = false;
    g_ctx = &ctx;

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
    
    for (int y = 0; y < pDraw->iHeight; y++) {
        int targetY = pDraw->y + y;
        if (targetY < 0 || targetY >= ctx->targetHeight) continue;

        for (int x = 0; x < pDraw->iWidth; x++) {
            int targetX = pDraw->x + x;
            if (targetX < 0 || targetX >= ctx->targetWidth) continue;

            uint16_t pixel = pDraw->pPixels[y * pDraw->iWidth + x];
            
            uint8_t r = (pixel >> 11) & 0x1F; 
            uint8_t g = (pixel >> 5) & 0x3F;  
            uint8_t b = pixel & 0x1F;         
            
            float lum = (r * 8.22f * 0.299f) + (g * 4.04f * 0.587f) + (b * 8.22f * 0.114f);
            
            // bb_epaper: 0 = black, 1 = white
            uint8_t color = (lum < 128) ? 0 : 1;
            ctx->bbep->drawPixel(targetX, targetY, color);
        }
    }
    return 1;
}

void ImageDecoder::PNGDraw(PNGDRAW *pDraw) {
    DecodeContext *ctx = (DecodeContext *)pDraw->pUser;
    uint16_t usPixels[800]; 
    
    if (!currentPNG) return;
    
    currentPNG->getLineAsRGB565(pDraw, usPixels, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);

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

        uint8_t color = (lum < 128) ? 0 : 1;
        ctx->bbep->drawPixel(targetX, targetY, color);
    }
}

