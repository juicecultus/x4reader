#include "EpubReader.h"

#include "../xml/SimpleXmlParser.h"

// Helper function for case-insensitive string comparison
static bool strcasecmp_helper(const String& str1, const char* str2) {
  if (str1.length() != strlen(str2))
    return false;
  for (size_t i = 0; i < str1.length(); i++) {
    char c1 = str1.charAt(i);
    char c2 = str2[i];
    if (c1 >= 'A' && c1 <= 'Z')
      c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z')
      c2 += 32;
    if (c1 != c2)
      return false;
  }
  return true;
}

// Helper function to find next element with given name
static bool findNextElement(SimpleXmlParser* parser, const char* elementName) {
  while (parser->read()) {
    if (parser->getNodeType() == SimpleXmlParser::Element && strcasecmp_helper(parser->getName(), elementName)) {
      return true;
    }
  }
  return false;
}

// File handle for extraction callback
static File g_extract_file;

// Callback to write extracted data to SD card file
static int extract_to_file_callback(const void* data, size_t size, void* user_data) {
  if (!g_extract_file) {
    return 0;  // File not open
  }

  size_t written = g_extract_file.write((const uint8_t*)data, size);
  return (written == size) ? 1 : 0;  // Return 1 for success, 0 for failure
}

EpubReader::EpubReader(const char* epubPath)
    : epubPath_(epubPath),
      valid_(false),
      reader_(nullptr),
      spine_(nullptr),
      spineCount_(0),
      toc_(nullptr),
      tocCount_(0) {
  Serial.printf("\n=== EpubReader: Opening %s ===\n", epubPath);

  // Verify file exists
  File testFile = SD.open(epubPath);
  if (!testFile) {
    Serial.println("ERROR: Cannot open EPUB file");
    return;
  }
  size_t fileSize = testFile.size();
  testFile.close();
  Serial.printf("EPUB file verified, size: %u bytes\n", fileSize);

  // Create extraction directory based on EPUB filename
  String epubFilename = String(epubPath);
  int lastSlash = epubFilename.lastIndexOf('/');
  if (lastSlash < 0) {
    lastSlash = epubFilename.lastIndexOf('\\');
  }
  if (lastSlash >= 0) {
    epubFilename = epubFilename.substring(lastSlash + 1);
  }
  int lastDot = epubFilename.lastIndexOf('.');
  if (lastDot >= 0) {
    epubFilename = epubFilename.substring(0, lastDot);
  }

#ifdef TEST_BUILD
  extractDir_ = "test/output/epub_" + epubFilename;
#else
  extractDir_ = "/epub_" + epubFilename;
#endif
  Serial.printf("Extract directory: %s\n", extractDir_.c_str());

  if (!ensureExtractDirExists()) {
    return;
  }

  // Parse container.xml to get content.opf path
  if (!parseContainer()) {
    Serial.println("ERROR: Failed to parse container.xml");
    return;
  }

  // Parse content.opf to get spine items
  if (!parseContentOpf()) {
    Serial.println("ERROR: Failed to parse content.opf");
    return;
  }

  // Parse toc.ncx to get table of contents (optional - don't fail if missing)
  if (!tocNcxPath_.isEmpty()) {
    if (!parseTocNcx()) {
      Serial.println("WARNING: Failed to parse toc.ncx - TOC will be unavailable");
    }
  } else {
    Serial.println("INFO: No toc.ncx found in this EPUB");
  }

  valid_ = true;
  Serial.println("EpubReader initialized successfully\n");
}

EpubReader::~EpubReader() {
  closeEpub();
  if (spine_) {
    delete[] spine_;
    spine_ = nullptr;
  }
  if (spineSizes_) {
    delete[] spineSizes_;
    spineSizes_ = nullptr;
  }
  if (spineOffsets_) {
    delete[] spineOffsets_;
    spineOffsets_ = nullptr;
  }
  if (toc_) {
    delete[] toc_;
    toc_ = nullptr;
  }
  Serial.println("EpubReader destroyed");
}

bool EpubReader::openEpub() {
  if (reader_) {
    return true;  // Already open
  }

  epub_error err = epub_open(epubPath_.c_str(), &reader_);
  if (err != EPUB_OK) {
    Serial.printf("ERROR: Failed to open EPUB: %s\n", epub_get_error_string(err));
    reader_ = nullptr;
    return false;
  }

  Serial.println("EPUB opened for reading");
  return true;
}

void EpubReader::closeEpub() {
  if (reader_) {
    epub_close(reader_);
    reader_ = nullptr;
    Serial.println("EPUB closed");
  }
}

bool EpubReader::ensureExtractDirExists() {
  if (!SD.exists(extractDir_.c_str())) {
    if (!SD.mkdir(extractDir_.c_str())) {
      Serial.printf("ERROR: Failed to create directory %s\n", extractDir_.c_str());
      return false;
    }
    Serial.printf("Created directory: %s\n", extractDir_.c_str());
  }
  return true;
}

String EpubReader::getExtractedPath(const char* filename) {
  String path = extractDir_ + "/" + String(filename);
  return path;
}

bool EpubReader::isFileExtracted(const char* filename) {
  String path = getExtractedPath(filename);
  bool exists = SD.exists(path.c_str());
  if (exists) {
    Serial.printf("File already extracted: %s\n", filename);
  }
  return exists;
}

bool EpubReader::extractFile(const char* filename) {
  Serial.printf("\n=== Extracting %s ===\n", filename);

  // Open EPUB if not already open
  if (!openEpub()) {
    return false;
  }

  // Find the file in the EPUB
  uint32_t fileIndex;
  epub_error err = epub_locate_file(reader_, filename, &fileIndex);
  if (err != EPUB_OK) {
    Serial.printf("ERROR: File not found in EPUB: %s\n", filename);
    return false;
  }

  // Get file info
  epub_file_info info;
  err = epub_get_file_info(reader_, fileIndex, &info);
  if (err != EPUB_OK) {
    Serial.printf("ERROR: Failed to get file info: %s\n", epub_get_error_string(err));
    return false;
  }

  Serial.printf("Found file at index %d (size: %u bytes)\n", fileIndex, info.uncompressed_size);

  // Create subdirectories if needed
  String extractPath = getExtractedPath(filename);
  int lastSlash = extractPath.lastIndexOf('/');
  if (lastSlash > 0) {
    String dirPath = extractPath.substring(0, lastSlash);

    // Create all parent directories
    int pos = 0;
    while (pos < dirPath.length()) {
      int nextSlash = dirPath.indexOf('/', pos + 1);
      if (nextSlash == -1) {
        nextSlash = dirPath.length();
      }

      String subDir = dirPath.substring(0, nextSlash);
      if (!SD.exists(subDir.c_str())) {
        if (!SD.mkdir(subDir.c_str())) {
          Serial.printf("ERROR: Failed to create directory %s\n", subDir.c_str());
          return false;
        }
      }

      pos = nextSlash;
    }
  }

  // Extract to file
  Serial.printf("Extracting to: %s\n", extractPath.c_str());

  g_extract_file = SD.open(extractPath.c_str(), FILE_WRITE);
  if (!g_extract_file) {
    Serial.printf("ERROR: Failed to open file for writing: %s\n", extractPath.c_str());
    return false;
  }

  err = epub_extract_streaming(reader_, fileIndex, extract_to_file_callback, nullptr, 4096);
  g_extract_file.close();

  if (err != EPUB_OK) {
    Serial.printf("ERROR: Extraction failed: %s\n", epub_get_error_string(err));
    return false;
  }

  Serial.printf("Successfully extracted %s\n", filename);
  return true;
}

String EpubReader::getFile(const char* filename) {
  if (!valid_) {
    Serial.println("ERROR: EpubReader not valid");
    return String("");
  }

  // Check if file is already extracted
  if (isFileExtracted(filename)) {
    return getExtractedPath(filename);
  }

  // Need to extract it
  if (!extractFile(filename)) {
    return String("");
  }

  return getExtractedPath(filename);
}

String EpubReader::getChapterNameForSpine(int spineIndex) const {
  // Get the spine item
  const SpineItem* spineItem = getSpineItem(spineIndex);
  if (spineItem == nullptr) {
    return String("");
  }

  // Search TOC for matching href
  // The spine href and TOC href should match (both are relative to content.opf)
  for (int i = 0; i < tocCount_; i++) {
    if (toc_[i].href == spineItem->href) {
      return toc_[i].title;
    }
  }

  // No exact match found - return empty string
  return String("");
}

bool EpubReader::parseContainer() {
  // Get container.xml (will extract if needed)
  // Check if file is already extracted
  const char* filename = "META-INF/container.xml";
  String containerPath;

  if (isFileExtracted(filename)) {
    containerPath = getExtractedPath(filename);
  } else {
    // Need to extract it
    if (!extractFile(filename)) {
      Serial.println("ERROR: Failed to extract container.xml");
      return false;
    }
    containerPath = getExtractedPath(filename);
  }

  if (containerPath.isEmpty()) {
    Serial.println("ERROR: Failed to get container.xml path");
    return false;
  }

  Serial.printf("Parsing container: %s\n", containerPath.c_str());

  // Parse container.xml to get content.opf path
  // Allocate parser on heap to avoid stack overflow (parser has 8KB buffer)
  SimpleXmlParser* parser = new SimpleXmlParser();
  if (!parser->open(containerPath.c_str())) {
    Serial.println("ERROR: Failed to open container.xml for parsing");
    delete parser;
    return false;
  }

  // Find <rootfile> element and get full-path attribute
  if (findNextElement(parser, "rootfile")) {
    contentOpfPath_ = parser->getAttribute("full-path");
  }

  parser->close();
  delete parser;

  if (contentOpfPath_.isEmpty()) {
    Serial.println("ERROR: Could not find content.opf path in container.xml");
    return false;
  }

  Serial.printf("Found content.opf: %s\n", contentOpfPath_.c_str());
  return true;
}

bool EpubReader::parseContentOpf() {
  // Get content.opf file
  const char* filename = contentOpfPath_.c_str();
  String opfPath;

  if (isFileExtracted(filename)) {
    opfPath = getExtractedPath(filename);
  } else {
    // Need to extract it
    if (!extractFile(filename)) {
      Serial.println("ERROR: Failed to extract content.opf");
      return false;
    }
    opfPath = getExtractedPath(filename);
  }

  if (opfPath.isEmpty()) {
    Serial.println("ERROR: Failed to get content.opf path");
    return false;
  }

  Serial.printf("Parsing content.opf: %s\n", opfPath.c_str());

  // Memory-efficient parsing: only store XHTML manifest items (not images/fonts/css)
  // Spine only references XHTML files, so we don't need the rest

  SimpleXmlParser* parser = new SimpleXmlParser();
  if (!parser->open(opfPath.c_str())) {
    Serial.println("ERROR: Failed to open content.opf for parsing");
    delete parser;
    return false;
  }

  // Step 1: Find <spine> element and get toc attribute
  String tocId = "";
  if (findNextElement(parser, "spine")) {
    tocId = parser->getAttribute("toc");
  }
  parser->close();

  // Step 2: If we have a toc id, find its href in manifest
  if (!tocId.isEmpty()) {
    if (!parser->open(opfPath.c_str())) {
      delete parser;
      return false;
    }
    while (findNextElement(parser, "item")) {
      String id = parser->getAttribute("id");
      if (id == tocId) {
        tocNcxPath_ = parser->getAttribute("href");
        Serial.printf("Found toc.ncx reference: %s\n", tocNcxPath_.c_str());
        break;
      }
    }
    parser->close();
  }

  // Step 3: Build manifest lookup - only for XHTML items (what spine references)
  // This is much smaller than the full manifest
  struct ManifestItem {
    String id;
    String href;
  };

  // Count XHTML items first
  if (!parser->open(opfPath.c_str())) {
    delete parser;
    return false;
  }

  int xhtmlCount = 0;
  while (findNextElement(parser, "item")) {
    String mediaType = parser->getAttribute("media-type");
    if (mediaType.indexOf("xhtml") >= 0 || mediaType.indexOf("html") >= 0) {
      xhtmlCount++;
    }
  }
  parser->close();

  Serial.printf("Found %d XHTML manifest items\n", xhtmlCount);

  // Allocate and collect XHTML manifest items
  ManifestItem* manifest = new ManifestItem[xhtmlCount];
  int manifestCount = 0;

  if (!parser->open(opfPath.c_str())) {
    delete parser;
    delete[] manifest;
    return false;
  }

  while (findNextElement(parser, "item") && manifestCount < xhtmlCount) {
    String mediaType = parser->getAttribute("media-type");
    if (mediaType.indexOf("xhtml") >= 0 || mediaType.indexOf("html") >= 0) {
      manifest[manifestCount].id = parser->getAttribute("id");
      manifest[manifestCount].href = parser->getAttribute("href");
      manifestCount++;
    }
  }
  parser->close();

  // Step 4: Count spine items
  if (!parser->open(opfPath.c_str())) {
    delete parser;
    delete[] manifest;
    return false;
  }

  int spineItemCount = 0;
  while (findNextElement(parser, "itemref")) {
    String idref = parser->getAttribute("idref");
    if (!idref.isEmpty()) {
      spineItemCount++;
    }
  }
  parser->close();

  Serial.printf("Found %d spine itemrefs\n", spineItemCount);

  // Allocate spine array
  spine_ = new SpineItem[spineItemCount];
  spineCount_ = 0;

  // Step 5: Collect spine items and resolve hrefs using manifest lookup
  if (!parser->open(opfPath.c_str())) {
    delete parser;
    delete[] manifest;
    delete[] spine_;
    spine_ = nullptr;
    return false;
  }

  while (findNextElement(parser, "itemref") && spineCount_ < spineItemCount) {
    String idref = parser->getAttribute("idref");
    if (!idref.isEmpty()) {
      spine_[spineCount_].idref = idref;
      spine_[spineCount_].href = "";

      // Look up href in manifest
      for (int j = 0; j < manifestCount; j++) {
        if (manifest[j].id == idref) {
          spine_[spineCount_].href = manifest[j].href;
          break;
        }
      }

      if (spine_[spineCount_].href.isEmpty()) {
        Serial.printf("WARNING: No manifest entry for idref: %s\n", idref.c_str());
      }
      spineCount_++;
    }
  }
  parser->close();

  // Done with manifest lookup
  delete[] manifest;
  delete parser;

  // Calculate spine item sizes for book-wide percentage calculation
  spineSizes_ = new size_t[spineCount_];
  spineOffsets_ = new size_t[spineCount_];
  totalBookSize_ = 0;

  // We need the EPUB open to get file sizes
  if (openEpub()) {
    // Get the base directory for content.opf (hrefs are relative to this)
    String baseDir = "";
    int lastSlash = contentOpfPath_.lastIndexOf('/');
    if (lastSlash >= 0) {
      baseDir = contentOpfPath_.substring(0, lastSlash + 1);
    }

    for (int i = 0; i < spineCount_; i++) {
      spineOffsets_[i] = totalBookSize_;

      // Build full path for this spine item
      String fullPath = baseDir + spine_[i].href;

      // Find the file in the EPUB and get its uncompressed size
      uint32_t fileIndex;
      epub_error err = epub_locate_file(reader_, fullPath.c_str(), &fileIndex);
      if (err == EPUB_OK) {
        epub_file_info info;
        err = epub_get_file_info(reader_, fileIndex, &info);
        if (err == EPUB_OK) {
          spineSizes_[i] = info.uncompressed_size;
          totalBookSize_ += info.uncompressed_size;
        } else {
          spineSizes_[i] = 0;
          Serial.printf("WARNING: Could not get file info for %s\n", fullPath.c_str());
        }
      } else {
        spineSizes_[i] = 0;
        Serial.printf("WARNING: Could not locate %s in EPUB\n", fullPath.c_str());
      }
    }
    closeEpub();
  }

  Serial.printf("\nSpine parsed successfully: %d items, total size: %u bytes\n", spineCount_, totalBookSize_);
  return true;
}

bool EpubReader::parseTocNcx() {
  // The toc.ncx path is relative to the content.opf location
  // We need to combine the content.opf directory with the toc.ncx path
  String tocPath = tocNcxPath_;

  // If content.opf is in a subdirectory, toc.ncx is likely there too
  int lastSlash = contentOpfPath_.lastIndexOf('/');
  if (lastSlash > 0) {
    String opfDir = contentOpfPath_.substring(0, lastSlash + 1);
    tocPath = opfDir + tocNcxPath_;
  }

  String extractedTocPath;
  if (isFileExtracted(tocPath.c_str())) {
    extractedTocPath = getExtractedPath(tocPath.c_str());
  } else {
    if (!extractFile(tocPath.c_str())) {
      Serial.printf("ERROR: Failed to extract toc.ncx: %s\n", tocPath.c_str());
      return false;
    }
    extractedTocPath = getExtractedPath(tocPath.c_str());
  }

  if (extractedTocPath.isEmpty()) {
    Serial.println("ERROR: Failed to get toc.ncx path");
    return false;
  }

  Serial.printf("Parsing toc.ncx: %s\n", extractedTocPath.c_str());

  SimpleXmlParser* parser = new SimpleXmlParser();
  if (!parser->open(extractedTocPath.c_str())) {
    Serial.println("ERROR: Failed to open toc.ncx for parsing");
    delete parser;
    return false;
  }

  // Use a temporary list to collect TOC items
  const int INITIAL_CAPACITY = 50;
  int capacity = INITIAL_CAPACITY;
  TocItem* tempToc = new TocItem[capacity];
  tocCount_ = 0;

  // Parse <navPoint> elements using a simpler approach:
  // For each navPoint, find its direct navLabel/text and content elements
  // Structure: <navPoint><navLabel><text>Title</text></navLabel><content src="file.xhtml#anchor"/></navPoint>

  String currentTitle = "";
  String currentSrc = "";
  bool inNavPoint = false;
  bool inNavLabel = false;
  bool expectingText = false;

  while (parser->read()) {
    SimpleXmlParser::NodeType nodeType = parser->getNodeType();
    String name = parser->getName();

    if (nodeType == SimpleXmlParser::Element) {
      if (strcasecmp_helper(name, "navPoint")) {
        // Starting a new navPoint - save previous one if we have data
        if (!currentTitle.isEmpty() && !currentSrc.isEmpty()) {
          // Grow array if needed
          if (tocCount_ >= capacity) {
            capacity *= 2;
            TocItem* newToc = new TocItem[capacity];
            for (int i = 0; i < tocCount_; i++) {
              newToc[i].title = tempToc[i].title;
              newToc[i].href = tempToc[i].href;
              newToc[i].anchor = tempToc[i].anchor;
            }
            delete[] tempToc;
            tempToc = newToc;
          }

          // Parse src into href and anchor
          int hashPos = currentSrc.indexOf('#');
          if (hashPos >= 0) {
            tempToc[tocCount_].href = currentSrc.substring(0, hashPos);
            tempToc[tocCount_].anchor = currentSrc.substring(hashPos + 1);
          } else {
            tempToc[tocCount_].href = currentSrc;
            tempToc[tocCount_].anchor = "";
          }
          tempToc[tocCount_].title = currentTitle;
          tocCount_++;
        }

        // Reset for new navPoint
        currentTitle = "";
        currentSrc = "";
        inNavPoint = true;
      } else if (strcasecmp_helper(name, "navLabel")) {
        inNavLabel = true;
      } else if (strcasecmp_helper(name, "text") && inNavLabel) {
        expectingText = true;
      } else if (strcasecmp_helper(name, "content") && inNavPoint) {
        // Only capture content if we don't have one yet for this navPoint
        if (currentSrc.isEmpty()) {
          currentSrc = parser->getAttribute("src");
        }
      }
    } else if (nodeType == SimpleXmlParser::Text && expectingText) {
      // Read the title text - only if we don't have one yet
      if (currentTitle.isEmpty()) {
        currentTitle = "";
        while (parser->hasMoreTextChars()) {
          char c = parser->readTextNodeCharForward();
          if (c != '\0') {
            currentTitle += c;
          }
        }
      }
      expectingText = false;
    } else if (nodeType == SimpleXmlParser::EndElement) {
      if (strcasecmp_helper(name, "navLabel")) {
        inNavLabel = false;
      } else if (strcasecmp_helper(name, "text")) {
        expectingText = false;
      }
    }
  }

  // Don't forget the last navPoint
  if (!currentTitle.isEmpty() && !currentSrc.isEmpty()) {
    if (tocCount_ >= capacity) {
      capacity *= 2;
      TocItem* newToc = new TocItem[capacity];
      for (int i = 0; i < tocCount_; i++) {
        newToc[i].title = tempToc[i].title;
        newToc[i].href = tempToc[i].href;
        newToc[i].anchor = tempToc[i].anchor;
      }
      delete[] tempToc;
      tempToc = newToc;
    }

    int hashPos = currentSrc.indexOf('#');
    if (hashPos >= 0) {
      tempToc[tocCount_].href = currentSrc.substring(0, hashPos);
      tempToc[tocCount_].anchor = currentSrc.substring(hashPos + 1);
    } else {
      tempToc[tocCount_].href = currentSrc;
      tempToc[tocCount_].anchor = "";
    }
    tempToc[tocCount_].title = currentTitle;
    tocCount_++;
  }

  // Allocate final TOC array with exact size
  if (tocCount_ > 0) {
    toc_ = new TocItem[tocCount_];
    for (int i = 0; i < tocCount_; i++) {
      toc_[i].title = tempToc[i].title;
      toc_[i].href = tempToc[i].href;
      toc_[i].anchor = tempToc[i].anchor;
    }
  }
  delete[] tempToc;

  parser->close();
  delete parser;

  Serial.printf("TOC parsed successfully: %d chapters/sections\n", tocCount_);

  // Print TOC summary
  for (int i = 0; i < tocCount_ && i < 10; i++) {
    Serial.printf("  [%d] %s -> %s", i, toc_[i].title.c_str(), toc_[i].href.c_str());
    if (!toc_[i].anchor.isEmpty()) {
      Serial.printf("#%s", toc_[i].anchor.c_str());
    }
    Serial.println();
  }
  if (tocCount_ > 10) {
    Serial.printf("  ... and %d more entries\n", tocCount_ - 10);
  }

  return true;
}
