#include "ImageDecoder.h"
#include <vector>

static const char* pngErrName(int err) {
    switch (err) {
        case PNG_SUCCESS: return "PNG_SUCCESS";
        case PNG_INVALID_PARAMETER: return "PNG_INVALID_PARAMETER";
        case PNG_DECODE_ERROR: return "PNG_DECODE_ERROR";
        case PNG_MEM_ERROR: return "PNG_MEM_ERROR";
        case PNG_NO_BUFFER: return "PNG_NO_BUFFER";
        case PNG_UNSUPPORTED_FEATURE: return "PNG_UNSUPPORTED_FEATURE";
        case PNG_INVALID_FILE: return "PNG_INVALID_FILE";
        case PNG_TOO_BIG: return "PNG_TOO_BIG";
        case PNG_QUIT_EARLY: return "PNG_QUIT_EARLY";
        default: return "PNG_UNKNOWN";
    }
}

static void dumpHex(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    for (size_t i = 0; i < len; i++) {
        Serial.printf("%02X", data[i]);
        if (i + 1 < len) Serial.print(" ");
    }
}

static ImageDecoder::DecodeContext* g_ctx = nullptr;
PNG* ImageDecoder::currentPNG = nullptr;

static uint32_t g_pngReadCalls = 0;
static uint32_t g_pngSeekCalls = 0;
static uint32_t g_pngShortReads = 0;
static int32_t g_pngLastSeekPos = -1;
static int32_t g_pngLastReadRequested = 0;
static int32_t g_pngLastReadReturned = 0;
static uint32_t g_pngReadFillLoops = 0;

static uint16_t rd16le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t rd32sle(const uint8_t* p) {
    return (int32_t)rd32le(p);
}

bool ImageDecoder::decodeBMPToDisplay(const char* path, DecodeContext* ctx) {
    if (!path || !ctx || !ctx->bbep) return false;

    File f = SD.open(path);
    if (!f) {
        Serial.printf("ImageDecoder: Failed to open %s\n", path);
        return false;
    }

    uint8_t hdr[54];
    const int n = (int)f.read(hdr, sizeof(hdr));
    if (n != (int)sizeof(hdr)) {
        Serial.printf("ImageDecoder: BMP header too short (%d bytes)\n", n);
        f.close();
        return false;
    }

    if (hdr[0] != 'B' || hdr[1] != 'M') {
        Serial.println("ImageDecoder: BMP invalid signature");
        f.close();
        return false;
    }

    const uint32_t dataOffset = rd32le(&hdr[10]);
    const uint32_t dibSize = rd32le(&hdr[14]);
    if (dibSize < 40) {
        Serial.printf("ImageDecoder: BMP unsupported DIB header size %lu\n", (unsigned long)dibSize);
        f.close();
        return false;
    }

    const int32_t bmpW = rd32sle(&hdr[18]);
    const int32_t bmpH = rd32sle(&hdr[22]);
    const uint16_t planes = rd16le(&hdr[26]);
    const uint16_t bpp = rd16le(&hdr[28]);
    const uint32_t compression = rd32le(&hdr[30]);

    if (bmpW <= 0 || bmpH == 0 || planes != 1) {
        Serial.printf("ImageDecoder: BMP invalid dims w=%ld h=%ld planes=%u\n", (long)bmpW, (long)bmpH, (unsigned)planes);
        f.close();
        return false;
    }

    if (!(bpp == 24 || bpp == 32)) {
        Serial.printf("ImageDecoder: BMP unsupported bpp=%u\n", (unsigned)bpp);
        f.close();
        return false;
    }

    // Only support BI_RGB (no compression)
    if (compression != 0) {
        Serial.printf("ImageDecoder: BMP unsupported compression=%lu\n", (unsigned long)compression);
        f.close();
        return false;
    }

    const bool topDown = (bmpH < 0);
    const int32_t absH = topDown ? -bmpH : bmpH;

    ctx->rotateSource90 = false;
    ctx->decodedWidth = (uint16_t)bmpW;
    ctx->decodedHeight = (uint16_t)absH;
    ctx->renderWidth = (uint16_t)bmpW;
    ctx->renderHeight = (uint16_t)absH;
    ctx->offsetX = ((int)ctx->targetWidth - (int)bmpW) / 2;
    ctx->offsetY = ((int)ctx->targetHeight - (int)absH) / 2;

    if (ctx->scaleToWidth && bmpW > 0) {
        // Preserve aspect ratio, force full target width.
        const int outW = (int)ctx->targetWidth;
        const int outH = (int)((((int64_t)absH) * (int64_t)outW) / (int64_t)bmpW);
        ctx->renderWidth = (uint16_t)outW;
        ctx->renderHeight = (uint16_t)outH;
        ctx->offsetX = 0;
        ctx->offsetY = ((int)ctx->targetHeight - outH) / 2;
    }

    Serial.printf("ImageDecoder: BMP %ldx%ld bpp=%u topDown=%d dataOffset=%lu offset=%d,%d\n",
                  (long)bmpW, (long)absH, (unsigned)bpp, topDown ? 1 : 0,
                  (unsigned long)dataOffset, ctx->offsetX, ctx->offsetY);

    const uint32_t bytesPerPixel = (uint32_t)(bpp / 8);
    const uint32_t rowStride = (uint32_t)(((uint32_t)bmpW * bytesPerPixel + 3) & ~3U);

    std::vector<uint8_t> row;
    row.resize(rowStride);

    if (!ctx->scaleToWidth) {
        for (int32_t y = 0; y < absH; y++) {
            const int32_t srcRow = topDown ? y : (absH - 1 - y);
            const uint32_t rowPos = dataOffset + (uint32_t)srcRow * rowStride;
            if (!f.seek(rowPos)) {
                Serial.printf("ImageDecoder: BMP seek failed at row %ld pos=%lu\n", (long)y, (unsigned long)rowPos);
                f.close();
                return false;
            }

            const int32_t r = (int32_t)f.read(row.data(), row.size());
            if (r != (int32_t)row.size()) {
                Serial.printf("ImageDecoder: BMP short read row %ld got=%ld need=%lu\n", (long)y, (long)r, (unsigned long)row.size());
                f.close();
                return false;
            }

            const int py = ctx->offsetY + (int)y;
            if (py < 0 || py >= (int)ctx->targetHeight) continue;

            for (int32_t x = 0; x < bmpW; x++) {
                const int px = ctx->offsetX + (int)x;
                if (px < 0 || px >= (int)ctx->targetWidth) continue;

                const uint8_t b = row[(uint32_t)x * bytesPerPixel + 0];
                const uint8_t g = row[(uint32_t)x * bytesPerPixel + 1];
                const uint8_t rr = row[(uint32_t)x * bytesPerPixel + 2];

                const uint32_t lum = (rr * 306U + g * 601U + b * 117U) >> 10;
                const uint8_t color = (lum < 128U) ? 0 : 1;

                // portrait logical -> physical framebuffer mapping
                const int fx = py;
                const int fy = 479 - px;
                if (fx < 0 || fx >= 800 || fy < 0 || fy >= 480) continue;

                if (ctx->frameBuffer) {
                    const int byteIdx = (fy * 100) + (fx / 8);
                    const int bitIdx = 7 - (fx % 8);
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
    } else {
        const int outW = (int)ctx->renderWidth;
        const int outH = (int)ctx->renderHeight;

        for (int dy = 0; dy < outH; ++dy) {
            const int py = ctx->offsetY + dy;
            if (py < 0 || py >= (int)ctx->targetHeight) continue;

            int sy = (int)((((int64_t)dy) * (int64_t)absH) / (int64_t)outH);
            if (sy < 0) sy = 0;
            if (sy >= (int)absH) sy = (int)absH - 1;
            const int32_t srcRow = topDown ? sy : ((int32_t)absH - 1 - sy);
            const uint32_t rowPos = dataOffset + (uint32_t)srcRow * rowStride;
            if (!f.seek(rowPos)) {
                Serial.printf("ImageDecoder: BMP seek failed at scaled row %d pos=%lu\n", dy, (unsigned long)rowPos);
                f.close();
                return false;
            }

            const int32_t r = (int32_t)f.read(row.data(), row.size());
            if (r != (int32_t)row.size()) {
                Serial.printf("ImageDecoder: BMP short read scaled row %d got=%ld need=%lu\n", dy, (long)r, (unsigned long)row.size());
                f.close();
                return false;
            }

            for (int dx = 0; dx < outW; ++dx) {
                const int px = dx;
                if (px < 0 || px >= (int)ctx->targetWidth) continue;
                int sx = (int)((((int64_t)dx) * (int64_t)bmpW) / (int64_t)outW);
                if (sx < 0) sx = 0;
                if (sx >= (int)bmpW) sx = (int)bmpW - 1;

                const uint8_t b = row[(uint32_t)sx * bytesPerPixel + 0];
                const uint8_t g = row[(uint32_t)sx * bytesPerPixel + 1];
                const uint8_t rr = row[(uint32_t)sx * bytesPerPixel + 2];
                const uint32_t lum = (rr * 306U + g * 601U + b * 117U) >> 10;
                const uint8_t color = (lum < 128U) ? 0 : 1;

                const int fx = py;
                const int fy = 479 - px;
                if (fx < 0 || fx >= 800 || fy < 0 || fy >= 480) continue;

                if (ctx->frameBuffer) {
                    const int byteIdx = (fy * 100) + (fx / 8);
                    const int bitIdx = 7 - (fx % 8);
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
    }

    f.close();
    return true;
}

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
    ctx->decodedWidth = 0;
    ctx->decodedHeight = 0;
    ctx->renderWidth = 0;
    ctx->renderHeight = 0;
    ctx->rotateSource90 = false;
    ctx->scaleToWidth = false;
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

            // If targeting portrait but the JPEG reports landscape dimensions,
            // it is commonly an EXIF-rotated portrait photo. Rotate in draw callback.
            ctx->rotateSource90 = (targetHeight > targetWidth) && (srcW > srcH);

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

            // Prefer a full-screen result:
            // - If the image (as displayed) is larger than the target, decode at full size
            //   (scale=0) and center-crop using negative offsets.
            // - Otherwise, fall back to a "fit" downscale (letterbox).
            int scale = 0;
            int outW = srcW;
            int outH = srcH;

            const int visW0 = ctx->rotateSource90 ? srcH : srcW;
            const int visH0 = ctx->rotateSource90 ? srcW : srcH;
            const bool canCoverAtFullRes = (visW0 >= (int)targetWidth) && (visH0 >= (int)targetHeight);

            if (!canCoverAtFullRes) {
                // Find first option that fits. If none fit, fall back to the smallest.
                scale = JPEG_SCALE_EIGHTH;
                outW = srcW >> 3;
                outH = srcH >> 3;

                for (size_t i = 0; i < (sizeof(opts) / sizeof(opts[0])); i++) {
                    const int w = srcW >> opts[i].shift;
                    const int h = srcH >> opts[i].shift;
                    const int visW = ctx->rotateSource90 ? h : w;
                    const int visH = ctx->rotateSource90 ? w : h;
                    if (visW <= (int)targetWidth && visH <= (int)targetHeight) {
                        scale = opts[i].opt;
                        outW = w;
                        outH = h;
                        break;
                    }
                }
            }

            ctx->decodedWidth = (uint16_t)outW;
            ctx->decodedHeight = (uint16_t)outH;

            const int visW = ctx->rotateSource90 ? outH : outW;
            const int visH = ctx->rotateSource90 ? outW : outH;
            ctx->renderWidth = (uint16_t)visW;
            ctx->renderHeight = (uint16_t)visH;

            // Center. Offsets may be negative when center-cropping to fill the screen.
            ctx->offsetX = ((int)targetWidth - visW) / 2;
            ctx->offsetY = ((int)targetHeight - visH) / 2;
            
            jpeg->setMaxOutputSize(1); 

            Serial.printf(
                "ImageDecoder: JPEG src=%dx%d target=%dx%d rotate90=%d scale=%d out=%dx%d vis=%dx%d offset=%d,%d\n",
                srcW, srcH,
                (int)targetWidth, (int)targetHeight,
                ctx->rotateSource90 ? 1 : 0,
                scale,
                outW, outH,
                visW, visH,
                ctx->offsetX, ctx->offsetY);

            // Decode at origin; we apply offsets/rotation in the draw callback.
            if (jpeg->decode(0, 0, scale)) {
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

        g_pngReadCalls = 0;
        g_pngSeekCalls = 0;
        g_pngShortReads = 0;
        g_pngLastSeekPos = -1;
        g_pngLastReadRequested = 0;
        g_pngLastReadReturned = 0;
        g_pngReadFillLoops = 0;

        {
            File hf = SD.open(path);
            if (hf) {
                uint8_t header[32];
                int n = (int)hf.read(header, sizeof(header));
                Serial.printf("ImageDecoder: PNG header (%d bytes): ", n);
                dumpHex(header, (n > 0) ? (size_t)n : 0);
                Serial.println();
                hf.close();
            } else {
                Serial.printf("ImageDecoder: PNG header read open failed for %s\n", path);
            }
        }

        int rc = png->open(path, [](const char *szFilename, int32_t *pFileSize) -> void * {
            File *file = new File(SD.open(szFilename));
            if (file && *file) {
                file->seek(0);
                *pFileSize = (int32_t)file->size();
                Serial.printf("ImageDecoder: PNG open %s size=%ld\n", szFilename, (long)*pFileSize);
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
            g_pngReadCalls++;
            g_pngLastReadRequested = iLength;
            if (g_pngReadCalls == 1) {
                Serial.printf("ImageDecoder: PNG first read request=%ld fileSize=%ld\n", (long)iLength, (long)pfn->iSize);
            }
            int32_t total = 0;
            while (total < iLength) {
                g_pngReadFillLoops++;
                int32_t n = (int32_t)file->read(pBuffer + total, (size_t)(iLength - total));
                if (n <= 0) break;
                total += n;
            }
            g_pngLastReadReturned = total;
            if (g_pngReadCalls == 2 && total >= 8) {
                // After PNGParseInfo consumes 33 bytes, DecodePNG seeks to 8 and reads 2048.
                // Dump the first 8 bytes of that buffer which should be the first chunk header.
                Serial.print("ImageDecoder: PNG decode first 8 bytes after seek(8): ");
                dumpHex(pBuffer, 8);
                Serial.println();
            }
            if (total > 0 && total < iLength && file->available() > 0) {
                g_pngShortReads++;
            }
            return total;
        }, [](PNGFILE *pfn, int32_t iPos) -> int32_t {
            if (!pfn || !pfn->fHandle) return -1;
            File *file = (File *)pfn->fHandle;
            const bool ok = file->seek((uint32_t)iPos);
            g_pngSeekCalls++;
            g_pngLastSeekPos = iPos;
            if (g_pngSeekCalls == 1) {
                Serial.printf("ImageDecoder: PNG first seek(%ld) ok=%d\n", (long)iPos, ok ? 1 : 0);
            }
            return ok ? iPos : -1;
        }, [](PNGDRAW *pDraw) -> int {
            if (!pDraw || !g_ctx) return 0;
            DecodeContext *ctx = g_ctx; 
            
            if (!currentPNG || !ctx->bbep || !ctx->errorBuf) return 0;
            
            uint16_t* usPixels = (uint16_t*)malloc(pDraw->iWidth * sizeof(uint16_t));
            if (!usPixels) return 0;
            
            currentPNG->getLineAsRGB565(pDraw, usPixels, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);

            const int sy = pDraw->y;
            if (sy < 0) {
                free(usPixels);
                return 0;
            }

            // PNGs on SD are expected to already be stored in portrait (e.g. 480x800).
            // Do not attempt EXIF-style rotation handling for PNG.

            const int w = (int)ctx->targetWidth;
            const int pyRow = ctx->offsetY + sy;
            if (pyRow < 0 || pyRow >= (int)ctx->targetHeight) {
                free(usPixels);
                return 0;
            }

            int16_t* curErr = &ctx->errorBuf[(pyRow % 2) * w];
            int16_t* nxtErr = &ctx->errorBuf[((pyRow + 1) % 2) * w];
            if (pyRow + 1 < (int)ctx->targetHeight) {
                memset(nxtErr, 0, (size_t)w * sizeof(int16_t));
            }

            for (int x = 0; x < pDraw->iWidth; x++) {
                const int sx = x;
                const int px = ctx->offsetX + sx;
                const int py = ctx->offsetY + sy;
                if (px < 0 || px >= (int)ctx->targetWidth || py < 0 || py >= (int)ctx->targetHeight) continue;

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
            return 1;
        });
        
        if (rc == PNG_SUCCESS) {
            int iw = png->getWidth();
            int ih = png->getHeight();

            ctx->rotateSource90 = false;
            ctx->decodedWidth = (uint16_t)iw;
            ctx->decodedHeight = (uint16_t)ih;
            ctx->renderWidth = (uint16_t)iw;
            ctx->renderHeight = (uint16_t)ih;

            ctx->offsetX = ((int)targetWidth - iw) / 2;
            ctx->offsetY = ((int)targetHeight - ih) / 2;
            // Offsets may be negative when center-cropping to fill the screen.

            Serial.printf("ImageDecoder: Decoding PNG %dx%d at offset %d,%d\n",
                          iw, ih, ctx->offsetX, ctx->offsetY);

            rc = png->decode(ctx, 0);
            if (rc == PNG_SUCCESS) {
                ctx->success = true;
                Serial.println("ImageDecoder: PNG decode successful");
            } else {
                const int lastErr = png->getLastError();
                Serial.printf("ImageDecoder: PNG decode failed, rc=%d (%s) lastErr=%d (%s)\n",
                              rc, pngErrName(rc), lastErr, pngErrName(lastErr));
                Serial.printf("ImageDecoder: PNG cb stats: readCalls=%u seekCalls=%u shortReads=%u lastSeek=%d lastReadReq=%d lastReadRet=%d fillLoops=%u\n",
                              (unsigned)g_pngReadCalls,
                              (unsigned)g_pngSeekCalls,
                              (unsigned)g_pngShortReads,
                              (int)g_pngLastSeekPos,
                              (int)g_pngLastReadRequested,
                              (int)g_pngLastReadReturned,
                              (unsigned)g_pngReadFillLoops);
            }
            png->close();
        } else {
            const int lastErr = png->getLastError();
            Serial.printf("ImageDecoder: PNG open failed, rc=%d (%s) lastErr=%d (%s)\n",
                          rc, pngErrName(rc), lastErr, pngErrName(lastErr));
        }
        currentPNG = nullptr;
        delete png;
    } else if (p.endsWith(".bmp")) {
        bool result = decodeBMPToDisplay(path, ctx);
        ctx->success = result;
        if (result) {
            Serial.println("ImageDecoder: BMP decode successful");
        } else {
            Serial.println("ImageDecoder: BMP decode failed");
        }
    }

    bool result = ctx->success;
    delete ctx;
    g_ctx = nullptr;
    return result;
}

bool ImageDecoder::decodeToDisplayFitWidth(const char* path, BBEPAPER* bbep, uint8_t* frameBuffer, uint16_t targetWidth, uint16_t targetHeight) {
    String p = String(path);
    p.toLowerCase();

    std::vector<int16_t> errorBuffer(targetWidth * 2, 0);
    DecodeContext* ctx = new DecodeContext();
    if (!ctx) return false;

    ctx->bbep = bbep;
    ctx->frameBuffer = frameBuffer;
    ctx->targetWidth = targetWidth;
    ctx->targetHeight = targetHeight;
    ctx->offsetX = 0;
    ctx->offsetY = 0;
    ctx->decodedWidth = 0;
    ctx->decodedHeight = 0;
    ctx->renderWidth = 0;
    ctx->renderHeight = 0;
    ctx->rotateSource90 = false;
    ctx->scaleToWidth = true;
    ctx->errorBuf = errorBuffer.data();
    ctx->success = false;
    g_ctx = ctx;

    if (p.endsWith(".bmp")) {
        ctx->success = decodeBMPToDisplay(path, ctx);
    } else if (p.endsWith(".jpg") || p.endsWith(".jpeg")) {
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
            ctx->rotateSource90 = (targetHeight > targetWidth) && (srcW > srcH);

            // Choose the largest downscale that still covers target width
            struct ScaleOpt { int opt; int shift; };
            const ScaleOpt opts[] = {
                {JPEG_SCALE_EIGHTH, 3},
                {JPEG_SCALE_QUARTER, 2},
                {JPEG_SCALE_HALF, 1},
                {0, 0},
            };
            int scale = 0;
            int outW = srcW;
            int outH = srcH;

            // Default to full res if nothing qualifies
            for (size_t i = 0; i < (sizeof(opts) / sizeof(opts[0])); i++) {
                const int w = srcW >> opts[i].shift;
                const int h = srcH >> opts[i].shift;
                const int visW = ctx->rotateSource90 ? h : w;
                if (visW >= (int)targetWidth) {
                    scale = opts[i].opt;
                    outW = w;
                    outH = h;
                    break;
                }
            }

            ctx->decodedWidth = (uint16_t)outW;
            ctx->decodedHeight = (uint16_t)outH;

            const int visW = ctx->rotateSource90 ? outH : outW;
            const int visH = ctx->rotateSource90 ? outW : outH;
            ctx->renderWidth = (uint16_t)visW;
            ctx->renderHeight = (uint16_t)visH;

            ctx->offsetX = ((int)targetWidth - visW) / 2;
            ctx->offsetY = ((int)targetHeight - visH) / 2;

            jpeg->setMaxOutputSize(1);

            if (jpeg->decode(0, 0, scale)) {
                ctx->success = true;
            }
            jpeg->close();
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

        int rc = png->open(path, [](const char *szFilename, int32_t *pFileSize) -> void * {
            File *file = new File(SD.open(szFilename));
            if (file && *file) {
                file->seek(0);
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
            int32_t total = 0;
            while (total < iLength) {
                int32_t n = (int32_t)file->read(pBuffer + total, (size_t)(iLength - total));
                if (n <= 0) break;
                total += n;
            }
            return total;
        }, [](PNGFILE *pfn, int32_t iPos) -> int32_t {
            if (!pfn || !pfn->fHandle) return -1;
            File *file = (File *)pfn->fHandle;
            return file->seek((uint32_t)iPos) ? iPos : -1;
        }, [](PNGDRAW *pDraw) -> int {
            if (!pDraw || !g_ctx) return 0;
            DecodeContext *ctx = g_ctx;
            if (!currentPNG || !ctx->bbep || !ctx->errorBuf) return 0;

            uint16_t* usPixels = (uint16_t*)malloc(pDraw->iWidth * sizeof(uint16_t));
            if (!usPixels) return 0;
            currentPNG->getLineAsRGB565(pDraw, usPixels, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);

            const int srcW = (int)ctx->decodedWidth;
            const int srcH = (int)ctx->decodedHeight;
            const int outW = (int)ctx->targetWidth;
            const int outH = (int)ctx->renderHeight;
            const int sy = pDraw->y;

            int dy0 = 0;
            int dy1 = -1;
            if (srcH > 0 && outH > 0) {
                dy0 = (int)((((int64_t)sy) * (int64_t)outH) / (int64_t)srcH);
                dy1 = (int)((((int64_t)(sy + 1)) * (int64_t)outH) / (int64_t)srcH) - 1;
                if (dy1 < dy0) dy1 = dy0;
            }

            for (int dy = dy0; dy <= dy1; ++dy) {
                const int py = ctx->offsetY + dy;
                if (py < 0 || py >= (int)ctx->targetHeight) continue;

                const int w = (int)ctx->targetWidth;
                int16_t* curErr = &ctx->errorBuf[(py % 2) * w];
                int16_t* nxtErr = &ctx->errorBuf[((py + 1) % 2) * w];
                if (py + 1 < (int)ctx->targetHeight) {
                    memset(nxtErr, 0, (size_t)w * sizeof(int16_t));
                }

                for (int dx = 0; dx < outW; ++dx) {
                    int sx = 0;
                    if (srcW > 0) {
                        sx = (int)((((int64_t)dx) * (int64_t)srcW) / (int64_t)outW);
                        if (sx < 0) sx = 0;
                        if (sx >= srcW) sx = srcW - 1;
                    }

                    const int px = dx;
                    const int fx = py;
                    const int fy = 479 - px;
                    if (fx < 0 || fx >= 800 || fy < 0 || fy >= 480) continue;

                    uint16_t pixel = usPixels[sx];
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
            }

            free(usPixels);
            return 1;
        });

        if (rc == PNG_SUCCESS) {
            int iw = png->getWidth();
            int ih = png->getHeight();

            ctx->rotateSource90 = false;
            ctx->decodedWidth = (uint16_t)iw;
            ctx->decodedHeight = (uint16_t)ih;

            const int outW = (int)targetWidth;
            const int outH = (iw > 0) ? (int)((((int64_t)ih) * (int64_t)outW) / (int64_t)iw) : 0;
            ctx->renderWidth = (uint16_t)outW;
            ctx->renderHeight = (uint16_t)outH;

            ctx->offsetX = 0;
            ctx->offsetY = ((int)targetHeight - outH) / 2;

            rc = png->decode(ctx, 0);
            if (rc == PNG_SUCCESS) {
                ctx->success = true;
            }
            png->close();
        }

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
    // If ctx->rotateSource90 is set, rotate source pixels to portrait first.
    for (int y = 0; y < pDraw->iHeight; y++) {
        const int sy = pDraw->y + y;
        if (sy < 0) continue;

        const uint16_t* pSrcRow = pDraw->pPixels + (y * pDraw->iWidth);

        for (int x = 0; x < pDraw->iWidth; x++) {
            const int sx = pDraw->x + x;
            if (sx < 0) continue;

            int px;
            int py;
            if (ctx->rotateSource90) {
                // Rotate 90deg CCW: (sx, sy) -> (sy, decodedW-1-sx)
                px = ctx->offsetX + sy;
                py = ctx->offsetY + ((int)ctx->decodedWidth - 1 - sx);
            } else {
                px = ctx->offsetX + sx;
                py = ctx->offsetY + sy;
            }

            if (px < 0 || px >= (int)ctx->targetWidth || py < 0 || py >= (int)ctx->targetHeight) continue;

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

    const int sy = pDraw->y;
    if (sy < 0) {
        free(usPixels);
        return;
    }

    // If we ever rotate PNG source, the callback order won't be scanline-ordered
    // in the destination. Dithering relies on scanline order; fall back to a
    // simple threshold in that case.
    const bool useDither = !ctx->rotateSource90;

    const int w = (int)ctx->targetWidth;
    int pyRow = ctx->rotateSource90 ? (ctx->offsetY + ((int)ctx->decodedWidth - 1 - 0)) : (ctx->offsetY + sy);
    if (pyRow < 0 || pyRow >= (int)ctx->targetHeight) {
        free(usPixels);
        return;
    }

    int16_t* curErr = &ctx->errorBuf[(pyRow % 2) * w];
    int16_t* nxtErr = &ctx->errorBuf[((pyRow + 1) % 2) * w];
    if (pyRow + 1 < (int)ctx->targetHeight) {
        memset(nxtErr, 0, (size_t)w * sizeof(int16_t));
    }

    for (int x = 0; x < pDraw->iWidth; x++) {
        const int sx = x;
        int px;
        int py;
        if (ctx->rotateSource90) {
            px = ctx->offsetX + sy;
            py = ctx->offsetY + ((int)ctx->decodedWidth - 1 - sx);
        } else {
            px = ctx->offsetX + sx;
            py = ctx->offsetY + sy;
        }
        if (px < 0 || px >= (int)ctx->targetWidth || py < 0 || py >= (int)ctx->targetHeight) continue;

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

        int16_t gray = (int16_t)lum;
        if (useDither) {
            gray = (int16_t)lum + curErr[px];
        }
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

        if (useDither) {
            int16_t err = gray - (color ? 255 : 0);
            if (px + 1 < w) curErr[px + 1] += (err * 7) / 16;
            if (py + 1 < (int)ctx->targetHeight) {
                if (px > 0) nxtErr[px - 1] += (err * 3) / 16;
                nxtErr[px] += (err * 5) / 16;
                if (px + 1 < w) nxtErr[px + 1] += (err * 1) / 16;
            }
        }
    }
    free(usPixels);
}
