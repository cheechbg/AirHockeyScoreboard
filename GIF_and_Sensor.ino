#include <Adafruit_Protomatter.h>
#include <Wire.h>
#include <Adafruit_VCNL4010.h>
#include <AnimatedGIF.h>
#include <Adafruit_SPIFlash.h>
#include <Adafruit_TinyUSB.h>
#include <SPI.h>
#include <SdFat.h>

//VCNL4010 sensor init
Adafruit_VCNL4010 red_prox;
Adafruit_VCNL4010 blue_prox;

// CONFIGURABLE SETTINGS ---------------------------------------------------

char GIFpath[] = "/gifs";     // Absolute path to GIFs on CIRCUITPY drive
uint16_t GIFminimumTime = 5; // Min. repeat time (seconds) until next GIF
#define WIDTH  64             // Matrix width in pixels
#define HEIGHT 32             // Matrix height in pixels
// Maximim matrix height is 32px on most boards, 64 on MatrixPortal if the
// 'E' jumper is set.

// Variable declarations for scoring -------------------------------------------------
int8_t red_score = 0;
int8_t blue_score = 0;
bool GIFisOpen = false;
char *filename = "blank";

// FLASH FILESYSTEM STUFF --------------------------------------------------

// External flash macros for QSPI or SPI are defined in board variant file.
#if defined(EXTERNAL_FLASH_USE_QSPI)
Adafruit_FlashTransport_QSPI flashTransport;
#elif defined(EXTERNAL_FLASH_USE_SPI)
Adafruit_FlashTransport_SPI flashTransport(EXTERNAL_FLASH_USE_CS,
    EXTERNAL_FLASH_USE_SPI);
#else
#error No QSPI/SPI flash are defined in your board variant.h!
#endif

Adafruit_SPIFlash flash(&flashTransport);
FatFileSystem filesys;     // Filesystem object from SdFat
Adafruit_USBD_MSC usb_msc; // USB mass storage object

// RGB MATRIX (PROTOMATTER) LIBRARY STUFF ----------------------------------

#if defined(_VARIANT_MATRIXPORTAL_M4_)
uint8_t rgbPins[] = {7, 8, 9, 10, 11, 12};
uint8_t addrPins[] = {17, 18, 19, 20, 21}; // 16/32/64 pixels tall
uint8_t clockPin = 14;
uint8_t latchPin = 15;
uint8_t oePin = 16;
#elif defined(_VARIANT_METRO_M4_)
uint8_t rgbPins[] = {2, 3, 4, 5, 6, 7};
uint8_t addrPins[] = {A0, A1, A2, A3}; // 16 or 32 pixels tall
uint8_t clockPin = A4;
uint8_t latchPin = 10;
uint8_t oePin = 9;
#elif defined(_VARIANT_FEATHER_M4_)
uint8_t rgbPins[] = {6, 5, 9, 11, 10, 12};
uint8_t addrPins[] = {A5, A4, A3, A2}; // 16 or 32 pixels tall
uint8_t clockPin = 13;
uint8_t latchPin = 0;
uint8_t oePin = 1;
#endif
#if HEIGHT == 16
#define NUM_ADDR_PINS 3
#elif HEIGHT == 32
#define NUM_ADDR_PINS 4
#elif HEIGHT == 64
#define NUM_ADDR_PINS 5
#endif

Adafruit_Protomatter matrix(WIDTH, 6, 1, rgbPins, NUM_ADDR_PINS, addrPins,
                            clockPin, latchPin, oePin, true);

// I2C MULTIPLEXER SWITCH FUNCTION  ----------------------------------------

void MuxSwitch(uint8_t bus)
{
  Wire.beginTransmission(0x70);  // TCA9548A address is 0x70
  Wire.write(1 << bus);          // send byte to select bus
  Wire.endTransmission();
}

// ANIMATEDGIF LIBRARY STUFF -----------------------------------------------

AnimatedGIF GIF;
File GIFfile;
int16_t xPos = 0, yPos = 0; // Top-left pixel coord of GIF in matrix space

// FILE ACCESS FUNCTIONS REQUIRED BY ANIMATED GIF LIB ----------------------

// Pass in ABSOLUTE PATH of GIF file to open
void *GIFOpenFile(const char *filename, int32_t *pSize) {
  GIFfile = filesys.open(filename);
  if (GIFfile) {
    *pSize = GIFfile.size();
    return (void *)&GIFfile;
  }
  return NULL;
}

void GIFCloseFile(void *pHandle) {
  File *f = static_cast<File *>(pHandle);
  if (f) f->close();
}

int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  int32_t iBytesRead = iLen;
  File *f = static_cast<File *>(pFile->fHandle);
  // If a file is read all the way to last byte, seek() stops working
  if ((pFile->iSize - pFile->iPos) < iLen)
    iBytesRead = pFile->iSize - pFile->iPos - 1; // ugly work-around
  if (iBytesRead <= 0) return 0;
  iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
  pFile->iPos = f->position();
  return iBytesRead;
}

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  return pFile->iPos;
}

// Draw one line of image to matrix back buffer
void GIFDraw(GIFDRAW *pDraw) {
  uint8_t *s;
  uint16_t *d, *usPalette, usTemp[320];
  int x, y;

  y = pDraw->iY + pDraw->y; // current line in image

  // Vertical clip
  int16_t screenY = yPos + y; // current row on matrix
  if ((screenY < 0) || (screenY >= matrix.height())) return;

  usPalette = pDraw->pPalette;

  s = pDraw->pPixels;
  // Apply the new pixels to the main image
  if (pDraw->ucHasTransparency) { // if transparency used
    uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
    int x, iCount;
    pEnd = s + pDraw->iWidth;
    x = 0;
    iCount = 0; // count non-transparent pixels
    while (x < pDraw->iWidth) {
      c = ucTransparent - 1;
      d = usTemp;
      while (c != ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent) { // done, stop
          s--;                    // back up to treat it like transparent
        } else {                  // opaque
          *d++ = usPalette[c];
          iCount++;
        }
      }             // while looking for opaque pixels
      if (iCount) { // any opaque pixels?
        span(usTemp, xPos + pDraw->iX + x, screenY, iCount);
        x += iCount;
        iCount = 0;
      }
      // no, look for a run of transparent pixels
      c = ucTransparent;
      while (c == ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent)
          iCount++;
        else
          s--;
      }
      if (iCount) {
        x += iCount; // skip these
        iCount = 0;
      }
    }
  } else {
    s = pDraw->pPixels;
    // Translate 8-bit pixels through RGB565 palette (already byte reversed)
    for (x = 0; x < pDraw->iWidth; x++)
      usTemp[x] = usPalette[*s++];
    span(usTemp, xPos + pDraw->iX, screenY, pDraw->iWidth);
  }
}

// Copy a horizontal span of pixels from a source buffer to an X,Y position
// in matrix back buffer, applying horizontal clipping. Vertical clipping is
// handled in GIFDraw() above -- y can safely be assumed valid here.
void span(uint16_t *src, int16_t x, int16_t y, int16_t width) {
  if (x >= matrix.width()) return; // Span entirely off right of matrix
  int16_t x2 = x + width - 1;      // Rightmost pixel
  if (x2 < 0) return;              // Span entirely off left of matrix
  if (x < 0) {                     // Span partially off left of matrix
    width += x;                    // Decrease span width
    src -= x;                      // Increment source pointer to new start
    x = 0;                         // Leftmost pixel is first column
  }
  if (x2 >= matrix.width()) {      // Span partially off right of matrix
    width -= (x2 - matrix.width() + 1);
  }
  if (matrix.getRotation() == 0) {
    memcpy(matrix.getBuffer() + y * matrix.width() + x, src, width * 2);
  } else {
    while (x <= x2) {
      matrix.drawPixel(x++, y, *src++);
    }
  }
}

// FUNCTIONS REQUIRED FOR USB MASS STORAGE ---------------------------------

static bool msc_changed = true; // Is set true on filesystem changes

// Callback on READ10 command.
int32_t msc_read_cb(uint32_t lba, void *buffer, uint32_t bufsize) {
  return flash.readBlocks(lba, (uint8_t *)buffer, bufsize / 512) ? bufsize : -1;
}

// Callback on WRITE10 command.
int32_t msc_write_cb(uint32_t lba, uint8_t *buffer, uint32_t bufsize) {
  digitalWrite(LED_BUILTIN, HIGH);
  return flash.writeBlocks(lba, buffer, bufsize / 512) ? bufsize : -1;
}

// Callback on WRITE10 completion.
void msc_flush_cb(void) {
  flash.syncBlocks();   // Sync with flash
  filesys.cacheClear(); // Clear filesystem cache to force refresh
  digitalWrite(LED_BUILTIN, LOW);
  msc_changed = true;
}

// Get number of files in a specified path that match extension ('filter').
// Pass in absolute path (e.g. "/" or "/gifs") and extension WITHOUT period
// (e.g. "gif", NOT ".gif").
int16_t numFiles(const char *path, const char *filter) {
  File dir = filesys.open(path);
  if (!dir) return -1;
  char filename[256];
  for (int16_t num_files = 0;;) {
    File entry = dir.openNextFile();
    if (!entry) return num_files; // No more files
    entry.getName(filename, sizeof(filename) - 1);
    entry.close();
    if (!entry.isDirectory() &&       // Skip directories
        strncmp(filename, "._", 2)) { // and Mac junk files
      char *extension = strrchr(filename, '.');
      if (extension && !strcasecmp(&extension[1], filter)) num_files++;
    }
  }
  return -1;
}

// Return name of file (matching extension) by index (0 to numFiles()-1)
char *filenameByIndex(const char *path, const char *filter, int16_t index) {
  static char filename[256]; // Must be static, we return a pointer to this!
  File entry, dir = filesys.open(path);
  if (!dir) return NULL;
  while (entry = dir.openNextFile()) {
    entry.getName(filename, sizeof(filename) - 1);
    entry.close();
    if (!entry.isDirectory() &&      // Skip directories
        strncmp(filename, "._", 2)) { // and Mac junk files
      char *extension = strrchr(filename, '.');
      if (extension  && !strcasecmp(&extension[1], filter)) {
        if (!index--) {
          return filename;
        }
      }
    }
  }
  return NULL;
}

//  GOAL SCORE WRITERS  -----------------------------------------------------------
void RedGoalWriter(void) {
  // Clear Screen
  matrix.fillScreen(0);                              // Fill background black
  //Borders
  matrix.drawRect(0, 0, 32, 32, matrix.color565(255, 0, 0));             // Red
  matrix.drawRect(1, 1, 30, 30, matrix.color565(255, 122, 0));             // Yellow
  matrix.drawRect(32, 0, 32, 32, matrix.color565(0, 0, 255));             // Blue
  matrix.drawRect(33, 1, 30, 30, matrix.color565(0, 122, 255));             // Light Blue
  //Score Text
  matrix.setCursor(9, 5);                           // start text at middle of Red window
  matrix.setTextSize(3);                            // size 3 == 21 pixels high
  matrix.setTextColor(0xF800);                      // Red Text
  matrix.println(red_score = red_score + 1);
  matrix.setCursor(41, 5);                          // start text at middle of Blue window
  matrix.setTextSize(3);                            // size 3 == 21 pixels high
  matrix.setTextColor(0x1F);                        // Blue text
  matrix.println(blue_score);
  matrix.show(); // Copy data to matrix buffers
  delay(3000);                                      //Delay to allow puck extraction
}

void BlueGoalWriter(void) {
  // Clear Screen
  matrix.fillScreen(0);                              // Fill background black
  //Borders
  matrix.drawRect(0, 0, 32, 32, matrix.color565(255, 0, 0));             // Red
  matrix.drawRect(1, 1, 30, 30, matrix.color565(255, 122, 0));             // Yellow
  matrix.drawRect(32, 0, 32, 32, matrix.color565(0, 0, 255));             // Blue
  matrix.drawRect(33, 1, 30, 30, matrix.color565(0, 122, 255));             // Light Blue
  //Score Text
  matrix.setCursor(9, 5);                           // start text at middle of Red window
  matrix.setTextSize(3);                            // size 3 == 21 pixels high
  matrix.setTextColor(0xF800);                      // Red Text
  matrix.println(red_score);
  matrix.setCursor(41, 5);                          // start text at middle of Blue window
  matrix.setTextSize(3);                            // size 3 == 21 pixels high
  matrix.setTextColor(0x1F);                        // Blue text
  matrix.println(blue_score = blue_score + 1);
  matrix.show(); // Copy data to matrix buffers
  delay(3000);                                      //Delay to allow puck extraction
}

void setup(void) {
  // USB mass storage / filesystem setup (do BEFORE Serial init)
  flash.begin();
  // Set disk vendor id, product id and revision
  usb_msc.setID("Adafruit", "External Flash", "1.0");
  // Set disk size, block size is 512 regardless of spi flash page size
  usb_msc.setCapacity(flash.pageSize() * flash.numPages() / 512, 512);
  usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);
  usb_msc.setUnitReady(true); // MSC is ready for read/write
  usb_msc.begin();
  filesys.begin(&flash); // Start filesystem on the flash

  Serial.begin(115200);
  Wire.begin();
  //VCNL4010 sensor starts via the multiplexor
  MuxSwitch(0);                                      // select I2C bus 0 for the Red proximity sensor
  red_prox.begin();
  MuxSwitch(1);                                      // select I2C bus 1 for the Blue proximity sensor
  blue_prox.begin();

  // Initialize matrix...
  ProtomatterStatus status = matrix.begin();
  Serial.print("Protomatter begin() status: ");
  Serial.println((int)status);
  matrix.fillScreen(0);
  matrix.show();

  // GIF setup
  GIF.begin(LITTLE_ENDIAN_PIXELS);

  //RED Initial Draw --------------------------------------------------------
  //Borders
  matrix.drawRect(0, 0, 32, 32, matrix.color565(255, 0, 0));             // Red
  matrix.drawRect(1, 1, 30, 30, matrix.color565(255, 122, 0));             // Yellow
  //Score
  matrix.setCursor(9, 5);   // middle of first block
  matrix.setTextSize(3);    // size 3 == 21 pixels high
  matrix.setTextColor(0xF800);
  matrix.println(red_score);

  //BLUE Initial Draw --------------------------------------------------------
  //Borders
  matrix.drawRect(32, 0, 32, 32, matrix.color565(0, 0, 255));             // Blue
  matrix.drawRect(33, 1, 30, 30, matrix.color565(0, 122, 255));             // Light Blue
  //Score
  matrix.setCursor(41, 5);   // middle of second block
  matrix.setTextSize(3);    // size 3 == 21 pixels high
  matrix.setTextColor(0x1F);
  matrix.println(blue_score);

  // Smaller font sizes
  //  matrix.setCursor(11, 9);   // start text at middle of Red window
  //  matrix.setTextSize(2);    // size 2 == 16 pixels high
  //  matrix.setTextColor(0xF800);
  //  matrix.println(red_score);
  //
  //  matrix.setCursor(43, 9);   // start text at middle of Red window
  //  matrix.setTextSize(2);    // size 2 == 16 pixels high
  //  matrix.setTextColor(0x1F);
  //  matrix.println(blue_score);
  matrix.show(); // Copy data to matrix buffers
}

uint32_t GIFstartTime = 0; // When current GIF started playing
int8_t GIFincrement = 0;   // +1 = next GIF, -1 = prev, 0 = same
int8_t GoalScored = 0;  // 1 = Red, 2 = Blue

void loop(void) {
  
  if (msc_changed) {     // If filesystem has changed...
    msc_changed = false; // Clear flag
    return;              // Prioritize USB, handled in calling func
  }

//  RED GOAL --------------------------------
  MuxSwitch(0);  
  if(red_prox.readProximity() > 15000) {
    GIFincrement = 1; // Set entry flag
    GoalScored = 1;
  }

//  BLUE GOAL  ------------------------------
  MuxSwitch(1);
  if(blue_prox.readProximity() > 15000) {
    GIFincrement = 1; // Set entry flag
    GoalScored = 2; 
  }

  if (GIFincrement) { // Change file?
    switch(GoalScored) {
      case 1:
        filename = "colbert2.gif";
        break;
      case 2:
        filename = "falcor.gif";
        break;
    }
    if (filename) {
      char fullname[sizeof GIFpath + 256];
      sprintf(fullname, "%s/%s", GIFpath, filename); // Absolute path to GIF
      Serial.printf("Opening file '%s'\n", fullname);
      if (GIF.open(fullname, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
        matrix.fillScreen(0);
        Serial.printf("GIF dimensions: %d x %d\n", GIF.getCanvasWidth(), GIF.getCanvasHeight());
        xPos = (matrix.width() - GIF.getCanvasWidth()) / 2; // Center on matrix
        yPos = (matrix.height() - GIF.getCanvasHeight()) / 2;
        GIFisOpen = true;
        GIFstartTime = millis();
        GIFincrement = 0; // Reset increment flag
      }
    }
  } else if(GIFisOpen) {
    if (GIF.playFrame(true, NULL) >= 0) { // Auto resets to start if needed
      matrix.show();
      if ((millis() - GIFstartTime) >= (GIFminimumTime * 1000)) {
        GIF.close();    // stop it
        Serial.println("GIF Stopped!");
        Serial.printf("GoalScored Flag: '%d'\n", GoalScored);
        GIFisOpen = false;
        GIFincrement = 0; // Reset increment flag
        switch(GoalScored) {
          case 1:
            GoalScored = 0;
            RedGoalWriter();
            break;
          case 2:
            GoalScored = 0;
            BlueGoalWriter();
            break;
        }
      }
    }
  }
}
