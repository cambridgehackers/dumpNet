/* Copyright (c) 2018 The Connectal Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <string>
#include <map>
#include <list>

#define BUFFER_SIZE 16384000
#define MAX_MESSAGE 40000
#define STRING -2
typedef struct {
    int offY; // row
    int offX; // column
    const char *tile;
} TileCoord;
typedef struct {
    const char *site;
    const char *tile;
} SiteTile;
#include "coord.temp"

typedef struct {
    const char *prefix;
    int         length;
    const char **map;
} CacheIndex;
typedef struct {
    int belNumber;
    const char *name;
} PlaceInfo;
typedef struct {
    int sitePin;
    int net;
    int exist;
    const char *name;
} SiteNetMap;
typedef struct {
     const char *name;
     int   mandatory;
     int   sameA6LUT_O6;
} SitePinInfo;
typedef struct {
     const char *name;
     SitePinInfo *info;
     int   size;
} SitePinInfoMap;
#include "cacheData.h"

typedef struct {
    int net;
    std::string name;
} SiteNetInfo;
typedef struct {
     int         valr;
     std::map<int, std::string> cacheMap;
} CacheType;
typedef struct {
     int   mandatory;
     int   sameA6LUT_O6;
} SitePinMandatory;

std::map<int, SiteNetInfo> siteNet;
std::map<int, int> netExist;
std::map<std::string, std::string> site2Tile;
std::map<int, std::string> coord2Tile;
std::map<int, std::string> placeName;
std::map<std::string, std::list<std::string>> sitePinTemplate;
std::map<std::string, std::map<std::string, int>> sitePinCount;
std::map<std::string, std::map<std::string, SitePinMandatory>> sitePinMandatory;
std::map<std::string, int> sitePinA6LUT_O6;
void initCoord()
{
    int maxX = 0;
    for (int i = 0; i < sizeof(tileCoord)/sizeof(tileCoord[0]); i++) {
       int j = tileCoord[i].offX;
       if (j > maxX)
           maxX = j;
    }
    maxX++;
    for (int i = 0; i < sizeof(tileCoord)/sizeof(tileCoord[0]); i++)
       coord2Tile[tileCoord[i].offY * maxX + tileCoord[i].offX] = tileCoord[i].tile;
    for (int i = 0; i < sizeof(siteTile)/sizeof(siteTile[0]); i++)
       site2Tile[siteTile[i].site] = siteTile[i].tile;
#ifdef HAS_CACHE_DATA
    for (int i = 0; i < sizeof(sitePinMap)/sizeof(sitePinMap[0]); i++)
        for (int j = 0; j < sitePinMap[i].size; j++) {
            sitePinMandatory[sitePinMap[i].name][sitePinMap[i].info[j].name].mandatory = sitePinMap[i].info[j].mandatory;
            sitePinMandatory[sitePinMap[i].name][sitePinMap[i].info[j].name].sameA6LUT_O6 = sitePinMap[i].info[j].sameA6LUT_O6;
            sitePinA6LUT_O6[sitePinMap[i].name] += sitePinMap[i].info[j].sameA6LUT_O6;
        }
#endif
}
std::map<std::string, CacheType> cache;
std::map<std::string, std::list<std::string>> tilePin;

struct {
    int val, tag;
    std::string str;
} messageData[MAX_MESSAGE];
int messageDataIndex;
uint8_t inbuf[BUFFER_SIZE];
const char header[] = "Xilinx_Design_Exchange_Format";
const char placerHeader[] = "Xilinx Placer Database, Copyright 2014 All rights reserved.";
const char deviceCacheHeader[] = "Xilinx Device Cache, Copyright 2014 All rights reserved.";
const char xnHeader[] = "XLNX:NTLP";
int ttrace = 0;
int trace = 0;
int mtrace = 0;
int messageLen;
std::string tagStringValue;
int tagLength;
int tagLine;
int tagValue;
const char *stripPrefix;
void traceString(std::string str);

inline std::string autostr(uint64_t X, bool isNeg = false)
{
  char Buffer[21];
  char *BufPtr = std::end(Buffer);

  if (X == 0) *--BufPtr = '0';  // Handle special case...

  while (X) {
    *--BufPtr = '0' + char(X % 10);
    X /= 10;
  }

  if (isNeg) *--BufPtr = '-';   // Add negative sign...
  return std::string(BufPtr, std::end(Buffer));
}

inline std::string autostrH(uint64_t X, bool isNeg = false)
{
  char Buffer[21];
  char *BufPtr = std::end(Buffer);

  if (X == 0) *--BufPtr = '0';  // Handle special case...

  while (X) {
    *--BufPtr = "0123456789abcdef"[X % 16];
    X /= 16;
  }

  if (isNeg) *--BufPtr = '-';   // Add negative sign...
  return std::string(BufPtr, std::end(Buffer));
}

bool inline endswith(std::string str, std::string suffix)
{
    int skipl = str.length() - suffix.length();
    return skipl >= 0 && str.substr(skipl) == suffix;
}

bool inline startswith(std::string str, std::string suffix)
{
    return str.substr(0, suffix.length()) == suffix;
}

std::string translateName(const char *prefix, int index)
{
    char buffer[1000];
    int lookupIndex = 0;

    sprintf(buffer, "%s_%03x", prefix, index);
#ifdef HAS_CACHE_DATA
    while (lookupMap[lookupIndex].prefix)
        if (strcmp(lookupMap[lookupIndex].prefix, prefix))
            lookupIndex++;
        else if (index && index <= lookupMap[lookupIndex].length)
            return lookupMap[lookupIndex].map[index-1];
        else
            break;
#endif
    return buffer;
}

std::string translatePlace(int index)
{
    char buffer[1000];
    int lookupIndex = 0;

    sprintf(buffer, "BEL_%03x", index);
#ifdef HAS_CACHE_DATA
    while (placeMap[lookupIndex].name)
        if (placeMap[lookupIndex].belNumber != index)
            lookupIndex++;
        else
            return placeMap[lookupIndex].name;
#endif
    return buffer;
}

void checkNetName(int netId, std::string name)
{
    int lookupIndex = 0;

    netExist[netId] = 1;
#ifdef HAS_CACHE_DATA
    while (siteNetMap[lookupIndex].name)
        if (siteNetMap[lookupIndex].net != netId)
            lookupIndex++;
        else {
            if (name == siteNetMap[lookupIndex].name)
                return;
printf("[%s:%d] Net name doesn't match! %x = '%s' req '%s'\n", __FUNCTION__, __LINE__, netId, siteNetMap[lookupIndex].name, name.c_str());
            exit(-1);
        }
printf("[%s:%d] Net not found! %x = '%s'\n", __FUNCTION__, __LINE__, netId, name.c_str());
    exit(-1);
#endif
}

std::string lookupSiteNet(int siteNet)
{
    char buffer[1000];
    int lookupIndex = 0;

    sprintf(buffer, "SW_%03x", siteNet);
#ifdef HAS_CACHE_DATA
    while (siteNetMap[lookupIndex].name)
        if (siteNetMap[lookupIndex].sitePin != siteNet)
            lookupIndex++;
        else {
            sprintf(buffer, "%s%s", siteNetMap[lookupIndex].exist ? "" : "*", siteNetMap[lookupIndex].name);
            break;
        }
#endif
    if (siteNet == 1)
        return "NONE1"; // VCC
    if (siteNet == 2)
        return "NONE2"; // GND
    if (siteNet == 3)
        return "NONE3"; // Open
    return buffer;
}

std::string getPinName(std::string tile, int pin)
{
    int lookupIndex = 0;
    std::string ret = autostrH(pin);
    int ind = tile.find("_X");
    tile = tile.substr(0, ind);
#ifdef HAS_CACHE_DATA
    while (tilePinMap[lookupIndex].prefix) {
        if (tile == tilePinMap[lookupIndex].prefix && pin < tilePinMap[lookupIndex].length)
            return tilePinMap[lookupIndex].map[pin];
        lookupIndex++;
    }
#endif
    return ret;
}

void memdump(const unsigned char *p, int len, const char *title)
{
int i;

    i = 0;
    while (len > 0) {
        if (!(i & 0xf)) {
            if (i > 0)
                fprintf(stdout, "\n");
            fprintf(stdout, "%s: ",title);
        }
        fprintf(stdout, "%02x ", *p++);
        i++;
        len--;
    }
    fprintf(stdout, "\n");
}

uint8_t const *bufp = inbuf;

void skipBuf(int len)
{
    if (trace || len > 1)
        memdump(bufp, len, "fill");
    bufp += len;
}

void checkByte(int byte)
{
    assert(*bufp == byte);
    bufp++;
}

void checkId(int val, int expected)
{
    if (val != expected)
        printf("[%s:%d] value was %x, expected %x\n", __FUNCTION__, __LINE__, val, expected);
assert (val == expected);
}

void checkStr(std::string val, std::string expected)
{
assert (val == expected);
}

int readInteger()
{
    int ret = 0;
    if (*bufp == ' ')
        bufp++;
    uint8_t const *start = bufp;
    bool isNeg = false;
    while(1) {
        if (*bufp == '-')
            isNeg = true;
        else if (isdigit(*bufp))
            ret = ret * 10 + *bufp - '0';
        else
            break;
        bufp++;
    }
if (start == bufp)
     memdump(bufp-16, 64, "BADREADINT");
    assert(start != bufp);
    std::string temp(start, bufp);
//printf("[%s:%d] readint %s start %p bufp %p isneg %d\n", __FUNCTION__, __LINE__, temp.c_str(), start, bufp, isNeg);
    if (isNeg)
        ret = -ret;
if (trace)
printf("[%s:%d] readint %d=%x\n", __FUNCTION__, __LINE__, ret, ret);
    return ret;
}

std::string readString()
{
    int len = readInteger();
    assert(*bufp == ' ');
    bufp++;
    assert(len < 1000);
    std::string temp(bufp, bufp + len);
    bufp += len;
if (trace)
printf("[%s:%d] readstring '%s'\n", __FUNCTION__, __LINE__, temp.c_str());
    return temp;
}

std::string readStrn()
{
    int len = *bufp++;
    assert(len < 50);
    std::string temp(bufp, bufp + len);
    bufp += len;
if (trace)
printf("[%s:%d] len %d '%s'\n", __FUNCTION__, __LINE__, len, temp.c_str());
    return temp;
}

long readVarint(uint8_t const **ptr = &bufp, bool internal = false)
{
    uint8_t const *start = (*ptr);
    while (*(*ptr) & 0x80)
        (*ptr)++;
    uint8_t const *p = (*ptr)++;
    long ret = 0;
    while (p >= start)
        ret = ret << 7 | (*p-- & 0x7f);
    if ((trace || mtrace) && !internal)
        printf("%s: word %lx\n", __FUNCTION__, ret);
    return ret;
}

int tagError;
int readTag(uint8_t const **ptr = &bufp, bool internal = false)
{
    tagStringValue = "";
    tagLength = 0;
    tagError = 0;
    uint8_t const *start = (*ptr); (void) start;
    long ch = readVarint(ptr, true);
    int line = ch & 0x7;
    int tag = ch >> 3;
    //printf("[%s:%d] buf %x line %x tag %d=0x%x\n", __FUNCTION__, __LINE__, ch, line, tag, tag);
    tagLine = line;
    tagValue = tag;
    switch (line) {
    case 0: { // varint
        long ret = readVarint(ptr, true);
        if (trace)
            printf("[%s:%d] tag %d varint %lx\n", __FUNCTION__, __LINE__, tag, ret);
        return ret;
        }
    case 1: {// 64 bit
        uint64_t ret = 0;
        memcpy(&ret, (*ptr), 8);
        (*ptr) += 8;
        if (trace)
            printf("[%s:%d] tag %d 64bit %llx\n", __FUNCTION__, __LINE__, tag, (unsigned long long)ret);
        return ret;
        }
    case 2: { // length delim
        long len = readVarint(ptr, true);
        tagLength = len;
        tagStringValue = std::string((*ptr), (*ptr) + len);
        if (trace) {
            printf("%s: tag %d len %ld", __FUNCTION__, tag, len);
            traceString(tagStringValue);
        }
        (*ptr) += tagLength;
        return STRING;
        break;
        }
    case 3: { // start group -- deprecated
        tagError = 1;
        if (!internal)
        if (1 || trace) {
            printf("[%s:%d] STARTGROUP ", __FUNCTION__, __LINE__);
            memdump(start, 16, "");
        }
        return -1;
        }
    case 4: { // end group -- deprecated
        tagError = 1;
        if (!internal)
        if (1 || trace) {
            printf("[%s:%d] ENDGROUP ", __FUNCTION__, __LINE__);
            memdump(start, 16, "");
        }
        return -1;
        }
    case 5: { // 32 bit
        uint32_t ret = 0;
        memcpy(&ret, (*ptr), 4);
        (*ptr) += 4;
        if (trace)
            printf("[%s:%d] tag %d 32bit %x\n", __FUNCTION__, __LINE__, tag, ret);
        return ret;
        }
    default:
        tagError = 1;
        if (!internal) {
        printf("[%s:%d] ERRORTAG: line %d tag 0x%x offset %lx\n", __FUNCTION__, __LINE__, line, tag, start - inbuf);
memdump(start - 32, 128, "BUF16");
exit(-1);
        }
        return -1;
    }
    return 0;
}

void traceString(std::string str)
{
    int len = str.length();
    uint8_t const *strp = (uint8_t const *)str.c_str();
    uint8_t const *strend = strp + len;
    bool allPrintable = true;
    for (auto ch : str)
        if (!isprint(ch))
            allPrintable = false;
    if (allPrintable)
        printf(" '%s'", strp);
    else {
        const char *sep = "";
        printf("(");
        while (strp < strend) {
            printf("%s", sep);
            uint8_t const *start = strp;
            int tagVal = readTag(&strp, true);
            if (tagError) {
                strp = start;
                break;
            }
            if (tagStringValue != "") {
                std::string lstr = tagStringValue;
                if (stripPrefix && lstr.substr(0, strlen(stripPrefix)) == stripPrefix)
                    lstr = lstr.substr(strlen(stripPrefix));
                traceString(lstr);
            }
            else
                printf("%x", tagVal);
            sep = ", ";
        }
        len = strend - strp;
        if (len > 0)
            memdump(strp, len > 64 ? 64 : len, "");
        printf(")");
    }
}

static struct {
    const char *prev;
    const char *cur;
} matchTemplate[] = {
    {"L_AQ", "LOGIC_OUTS0"}, {"L_BQ", "LOGIC_OUTS1"}, {"L_CQ", "LOGIC_OUTS2"}, {"L_DQ", "LOGIC_OUTS3"},
    {"LL_AQ", "LOGIC_OUTS4"}, {"LL_BQ", "LOGIC_OUTS5"}, {"LL_CQ", "LOGIC_OUTS6"}, {"LL_DQ", "LOGIC_OUTS7"},
    {"M_AQ", "LOGIC_OUTS4"}, {"M_BQ", "LOGIC_OUTS5"}, {"M_CQ", "LOGIC_OUTS6"}, {"M_DQ", "LOGIC_OUTS7"},
    {"L_A", "LOGIC_OUTS8"}, {"L_B", "LOGIC_OUTS9"}, {"L_C", "LOGIC_OUTS10"}, {"L_D", "LOGIC_OUTS11"},
    {"M_A", "LOGIC_OUTS12"}, {"M_B", "LOGIC_OUTS13"}, {"M_C", "LOGIC_OUTS14"}, {"M_D", "LOGIC_OUTS15"},
    {"LL_A", "LOGIC_OUTS12"}, {"LL_B", "LOGIC_OUTS13"}, {"LL_C", "LOGIC_OUTS14"}, {"LL_D", "LOGIC_OUTS15"},
    {"L_AMUX", "LOGIC_OUTS16"}, {"L_BMUX", "LOGIC_OUTS17"}, {"L_CMUX", "LOGIC_OUTS18"}, {"L_DMUX", "LOGIC_OUTS19"},
    {"LL_AMUX", "LOGIC_OUTS20"}, {"LL_BMUX", "LOGIC_OUTS21"}, {"LL_CMUX", "LOGIC_OUTS22"}, {"LL_DMUX", "LOGIC_OUTS23"},
    {"M_AMUX", "LOGIC_OUTS20"}, {"M_BMUX", "LOGIC_OUTS21"}, {"M_CMUX", "LOGIC_OUTS22"}, {"M_DMUX", "LOGIC_OUTS23"},
    {nullptr, nullptr}};
static struct {
    const char *prefix;
    int deltaX;
    int deltaY;
    } dirNew[] = {
    {"EE2",  2,  0}, {"EE4",  4,  0}, {"EL1",  1,  0}, {"ER1",  1,  0},
    {"WW2", -2,  0}, {"WW4", -4,  0}, {"WL1", -1,  0}, {"WR1", -1,  0},
    {"NE2",  1,  1}, {"NE6",  2,  4}, {"NN2",  0,  2}, {"NN6",  0,  6}, {"NW2", -1,  1}, {"NW6", -2,  4},
    {"SE2",  1, -1}, {"SE6",  2, -4}, {"SS2",  0, -2}, {"SS6",  0, -6}, {"SW2", -1, -1}, {"SW6", -2, -4},
    {"NL1",  0,  1}, {"NR1",  0,  1},
    {"SL1",  0, -1}, {"SR1",  0, -1},
    {nullptr, 0, 0}};

static struct {
    const char *cur;
    const char *prev;
} prefixPin[] = {
    {"LL_A1", "IMUX7"}, {"LL_A2", "IMUX2"}, {"LL_A3", "IMUX1"},
    {"LL_A4", "IMUX11"}, {"LL_A5", "IMUX8"}, {"LL_A6", "IMUX4"},
    {"LL_AX", "BYP_ALT1"},
    {"LL_B1", "IMUX15"}, {"LL_B2", "IMUX18"}, {"LL_B3", "IMUX17"},
    {"LL_B4", "IMUX27"}, {"LL_B5", "IMUX24"}, {"LL_B6", "IMUX12"},
    {"LL_BX", "BYP_ALT4"},
    {"LL_C1", "IMUX32"}, {"LL_C2", "IMUX29"}, {"LL_C3", "IMUX22"},
    {"LL_C4", "IMUX28"}, {"LL_C5", "IMUX31"}, {"LL_C6", "IMUX35"},
    {"LL_CE", "FAN_ALT7"}, {"LL_CX", "BYP_ALT3"},
    {"LL_D1", "IMUX40"}, {"LL_D2", "IMUX45"}, {"LL_D3", "IMUX38"},
    {"LL_D4", "IMUX44"}, {"LL_D5", "IMUX47"}, {"LL_D6", "IMUX43"},
    {"LL_DX", "BYP_ALT6"},
    {"L_A1", "IMUX6"}, {"L_A2", "IMUX3"}, {"L_A3", "IMUX0"},
    {"L_A4", "IMUX10"}, {"L_A5", "IMUX9"}, {"L_A6", "IMUX5"},
    {"L_AX", "BYP_ALT0"},
    {"L_B1", "IMUX14"}, {"L_B2", "IMUX19"}, {"L_B3", "IMUX16"},
    {"L_B4", "IMUX26"}, {"L_B5", "IMUX25"}, {"L_B6", "IMUX13"},
    {"L_BX", "BYP_ALT5"},
    {"L_C1", "IMUX33"}, {"L_C2", "IMUX20"}, {"L_C3", "IMUX23"},
    {"L_C4", "IMUX21"}, {"L_C5", "IMUX30"}, {"L_C6", "IMUX34"},
    {"L_CE", "FAN_ALT6"},
    {"L_CX", "BYP_ALT2"},
    {"L_D1", "IMUX41"}, {"L_D2", "IMUX36"}, {"L_D3", "IMUX39"},
    {"L_D4", "IMUX37"}, {"L_D5", "IMUX46"}, {"L_D6", "IMUX42"},
    {"L_DX", "BYP_ALT7"},
    {"M_A1", "IMUX7"}, {"M_A2", "IMUX2"}, {"M_A3", "IMUX1"},
    {"M_A4", "IMUX11"}, {"M_A5", "IMUX8"}, {"M_A6", "IMUX4"},
    {"M_AX", "BYP_ALT1"},
    {"M_B1", "IMUX15"}, {"M_B2", "IMUX18"}, {"M_B3", "IMUX17"},
    {"M_B4", "IMUX27"}, {"M_B5", "IMUX24"}, {"M_B6", "IMUX12"},
    {"M_BX", "BYP_ALT4"},
    {"M_C1", "IMUX32"}, {"M_C2", "IMUX29"}, {"M_C3", "IMUX22"},
    {"M_C4", "IMUX28"}, {"M_C5", "IMUX31"}, {"M_C6", "IMUX35"},
    {"M_CE", "FAN_ALT7"}, {"M_CX", "BYP_ALT3"},
    {"M_D1", "IMUX40"}, {"M_D2", "IMUX45"}, {"M_D3", "IMUX38"},
    {"M_D4", "IMUX44"}, {"M_D5", "IMUX47"}, {"M_D6", "IMUX43"},
    {"M_DX", "BYP_ALT6"},
    {"L_SR", "CTRL0"}, {"LL_SR", "CTRL1"}, {"M_SR", "CTRL1"},
    {}};

struct {
    int value;
    const char *name;
} muxName[] = {
    {0x2, "EE2"}, //{0x2, "ER1"},
    {0x3, "EL1"}, {0x4, "ER1"},
    {0x8, "NR1"},
    {0xb, "NE2"}, //{0xb, "SR1"},
    {0xc, "NL1"}, {0xd, "NN2"}, {0xe, "NR1"}, {0xf, "NW2"},
    {0x10, "SE2"}, {0x11, "SL1"}, {0x12, "SR1"}, {0x13, "SS2"}, {0x14, "SW2"},
    {0x15, "VCC"},
    {0x16, "WL1"}, {0x17, "WR1"}, {0x18, "WW2"},
    {-1, nullptr}};

typedef struct {
    int         tile;
    int         siteX;
    int         siteY;
    int         mux;
    std::string site;
    std::string pin;
} PinInfo;

void readNodeList(bool &first)
{
    int count = readInteger();
    int param = readInteger();
    if (param)
        printf("param %d: ", param);
    if (first) {
        int extra = readInteger();
        checkId(extra, 0);
        extra = readInteger();
        checkId(extra, 0);
        first = false;
    }
    std::list<PinInfo> pinList;
    // First, parse file into 'pinList'
    for (int i = 0; i < count; i++) {
        int tile = readInteger();
        int mux = readInteger();
        int elementPin = mux>>16;
        mux &= 0xffff;

        int t1 = (tile >> 29) & 1;
        int t2 = (tile >> 28) & 1;
        checkId(t1, 0);
        checkId(t2, 0);
        //tile &= 0x3fffff;
        tile &= 0xffffff;
        std::string tname = coord2Tile[tile];
        if (tname == "") {
            printf("[%s:%d]missing tile def %x\n", __FUNCTION__, __LINE__, tile);
            exit(-1);
        }
        std::string pname = getPinName(tname, elementPin);
        if (startswith(pname, "CLBLL_") || startswith(pname, "CLBLM_"))
            pname = pname.substr(6);
        std::string temp = tname.substr(tname.find("_X")+2);
        int siteX = atoi(temp.c_str());
        int siteY = atoi(temp.substr(temp.find("Y")+1).c_str());
        pinList.push_back(PinInfo{tile, siteX, siteY, mux, tname, pname});
    }

    // Now, perform processing on the assembled list
    auto item = pinList.begin(), prevItem = item;
    if (item->mux != 0xffff) {
        printf("[%s:%d] default mux only on first element %p mux %x\n", __FUNCTION__, __LINE__, item->pin.c_str(), item->mux);
        exit(-1);
    }
    item++;
    int matchIndex = 0;
    // skip printing pins when they automatically follow from previous pin type
    while(matchTemplate[matchIndex].prev) {
        if (prevItem->pin == matchTemplate[matchIndex].prev) {
            if (item->pin == matchTemplate[matchIndex].cur && item->mux == 0) {
                item = pinList.erase(item);
                break;
            }
            else if ((prevItem->pin == "M_D" && item->pin == "M_DMUX")
             || (prevItem->pin == "L_C" && item->pin == "L_CMUX")) {
                // ok
            }
            else {
                printf("[%s:%d] prev %s pname %s\n", __FUNCTION__, __LINE__, prevItem->pin.c_str(), item->pin.c_str());
                exit(-1);
            }
        }
        matchIndex++;
    }
    bool skipFill = false;
    for (auto item = pinList.begin(), itemE = pinList.end(), prevItem = item; item != itemE;) {
        if (item != pinList.begin() && item->mux == 0xffff) {
            printf("[%s:%d] default mux only on first element %p mux %x\n", __FUNCTION__, __LINE__, item->pin.c_str(), item->mux);
            exit(-1);
        }
        if (item != pinList.begin()) {
        int matchIndex = 0;
        // validate that mandatory previous pin matches
        while(prefixPin[matchIndex].prev) {
            if (item->pin == prefixPin[matchIndex].cur) {
                auto pitem = item;
                pitem--;
                if (item->mux != 0 || !startswith(pitem->pin, prefixPin[matchIndex].prev)) {
                    printf("[%s:%d] prefix mux %x prev %s pname %s\n", __FUNCTION__, __LINE__, item->mux, pitem->pin.c_str(), item->pin.c_str());
                    exit(-1);
                }
                item->mux = pitem->mux; // copy mux value from prefix
                pinList.erase(pitem);
                break;
            }
            matchIndex++;
        }
        if (startswith(item->pin, "BYP") && item->pin.length() > 3 && isdigit(item->pin[3])) {
                auto pitem = item;
                pitem--;
            if (!startswith(pitem->pin, "BYP_ALT")) {
                printf("[%s:%d] bypalt prev %s pname %s\n", __FUNCTION__, __LINE__, pitem->pin.c_str(), item->pin.c_str());
                exit(-1);
            }
            int bval = atoi(item->pin.c_str() + 3);
            int balt = atoi(pitem->pin.c_str() + 7);
            if (item->mux != 0 || bval != balt) {
                printf("[%s:%d] bypaltval prev %s pname %s\n", __FUNCTION__, __LINE__, pitem->pin.c_str(), item->pin.c_str());
                exit(-1);
            }
            item = pinList.erase(item);
            //skipFill = true;
            if (0)
            if (item == pinList.end() || !startswith(item->pin, "BYP_BOUNCE") || item->mux != 0) {
                printf("[%s:%d] bypbounce prev %s pname %s\n", __FUNCTION__, __LINE__, pitem->pin.c_str(), item->pin.c_str());
                exit(-1);
            }
            goto endl;
        }
        if (!skipFill && startswith(prevItem->pin, "FAN_ALT")) {
            int balt = atoi(prevItem->pin.c_str() + 7);
            int bval;
            if (startswith(item->pin, "FAN") && item->pin.length() > 3 && isdigit(item->pin[3])) {
                bval = atoi(item->pin.c_str() + 3);
            }
            else if (startswith(item->pin, "FAN_BOUNCE")) {
                bval = atoi(item->pin.c_str() + 10);
            }
            else {
                printf("[%s:%d] fanalt prev %s pname %s\n", __FUNCTION__, __LINE__, prevItem->pin.c_str(), item->pin.c_str());
                exit(-1);
            }
            if (item->mux != 0 || bval != balt) {
                printf("[%s:%d] fanaltval prev %s pname %s\n", __FUNCTION__, __LINE__, prevItem->pin.c_str(), item->pin.c_str());
                exit(-1);
            }
            skipFill = true;
            item = pinList.erase(item);
            goto endl;
        }
        }
        prevItem = item;
        item++;
        skipFill = false;
endl:;
    }
    for (auto item = pinList.begin(), itemE = pinList.end(), prevItem = item; item != itemE; prevItem = item, item++) {
        if (prevItem->tile != item->tile) {
int bind = prevItem->pin.find("BEG");
if (bind > 0) {
    std::string fs = coord2Tile[prevItem->tile], cs = coord2Tile[item->tile].c_str();
    std::string ftemp = fs.substr(fs.find("_X")+2);
    std::string ctemp = cs.substr(cs.find("_X")+2);
    int targetX = atoi(ctemp.c_str());
    int sourceX = atoi(ftemp.c_str());
    int diffX = targetX - sourceX;
    int targetY = atoi(ctemp.substr(ctemp.find("Y") + 1).c_str());
    int sourceY = atoi(ftemp.substr(ftemp.find("Y") + 1).c_str());
    int diffY = targetY - sourceY;
    int diffInd = 0;
    int ldiff = (sourceY & 1) * 2 + (targetY & 1);
    while (dirNew[diffInd].prefix) {
        if (!endswith(prevItem->pin, "_N3") && !endswith(prevItem->pin, "_S0"))
        if (startswith(prevItem->pin, dirNew[diffInd].prefix)) {
        if (startswith(fs, "INT_") && startswith(cs, "INT_")) {
int fL = fs[4] == 'L';
int cL = cs[4] == 'L';
        if ((dirNew[diffInd].deltaX != diffX && targetX != 0 && (sourceX != 0 || dirNew[diffInd].deltaX >= 0)
)
         || dirNew[diffInd].deltaY != diffY)
            printf("\nZZZ tilediff X:%d Y:%d pin %s ldiff %d odd %d col:%d row:%d %s -> %s\n",
                diffX - dirNew[diffInd].deltaX, diffY - dirNew[diffInd].deltaY,
 prevItem->pin.c_str(),
                fL  * 2 + cL,
                ldiff,
                item->tile%128 - prevItem->tile%128, item->tile/128 - prevItem->tile/128,
                fs.c_str(), cs.c_str());
        }
        break;
        }
        diffInd++;
    }
}
        }

        printf(" ");
        if (item->site != "")
            printf("%s/", item->site.c_str());
        printf("%s", item->pin.c_str());
        if (item->mux != 0xffff) {
            int ind = 0;
            while(muxName[ind].name) {
                if (item->mux == muxName[ind].value) {
                    printf(":%s", muxName[ind].name);
                    goto muxEnd;
                }
                ind++;
            }
            if (item->mux < 2)
                printf(":%x", item->mux);
            else
                printf(":MUX_%x", item->mux);
muxEnd:;
        }
        int matchIndex = 0;
        while(prefixPin[matchIndex].prev) {
            if (item->pin == prefixPin[matchIndex].cur) {
                auto pitem = item;
                pitem++;
                if (pitem != itemE && pitem->tile != item->tile)
                    printf("\n");
                break;
            }
            matchIndex++;
        }
    }
}

int readMessage()
{
    messageDataIndex = 0;
    messageLen = readVarint(&bufp, true);
    const char *sep = "";
    uint8_t const *endp = bufp + messageLen;
    if (mtrace)
        printf("message[%d]: ", messageLen);
    while (bufp < endp) {
        int val = readTag();
        messageData[messageDataIndex].val = val;
        messageData[messageDataIndex].str = tagStringValue;
        messageData[messageDataIndex].tag = tagValue;
        if (mtrace) {
            printf("%s %x:", sep, tagValue);
            if (tagStringValue != "")
                traceString(tagStringValue);
            else
                printf("%x", val);
        }
        messageDataIndex++;
        sep = ", ";
    }
    if (mtrace)
        printf("\n");
    int excessRead = bufp - endp;
    if (excessRead) {
        printf("[%s:%d] excessreadofmessage %d\n", __FUNCTION__, __LINE__, excessRead);
        //assert(!excessRead);
    }
    return messageDataIndex;
}

void dumpQuad(const char *sep, const char *end)
{
    checkId(messageDataIndex, 4);
    for (int msgIndex = 0; msgIndex < 4; msgIndex++) {
        if (messageData[msgIndex].val == STRING)
            printf("%s'%s'", sep, messageData[msgIndex].str.c_str());
        else
            printf("%s%x", sep, messageData[msgIndex].val);
        sep = ", ";
    }
    printf("%s", end);
}

bool lineValid;
void belStart(int elementNumber)
{
    if (!lineValid) {
        lineValid = true;
        printf("    %s", translateName("Element", elementNumber).c_str());
    }
}

std::map<std::string, int> site2tile;

void dumpXdef(void)
{
//jca
    initCoord();
    printf("Parse header\n");
    checkStr(std::string(bufp, bufp + strlen(header)), header);
    bufp += strlen(header);
    std::string fileType = readString();
    int head1 = readInteger();
    int head2 = readInteger();
    int head3 = readInteger();
    int version = readInteger();

    std::string build = readString();
    int flag1 = readInteger();
printf("[%s:%d] filetype %s: %x, %x, %x, version %d. zero/non-zero %d build %s\n", __FUNCTION__, __LINE__, fileType.c_str(), head1, head2, head3, version, flag1, build.c_str());
    int head5 = readInteger();
    std::string str1 = readString();
    std::string device = readString();
    std::string package = readString();
    int head6 = readInteger();
    int head7 = readInteger();
    int nLimit = readInteger();
printf("[%s:%d] head5 %d device '%s' package '%s' limit %d str1 '%s': %x %x\n", __FUNCTION__, __LINE__, head5, device.c_str(), package.c_str(), nLimit, str1.c_str(), head6, head7);
    for (int i = 0; i < 3; i++) {
        checkId(readInteger(), 0);
    }
    for (int i = 0; i < nLimit; i++) {
        int val1 = readInteger();
        int val2 = readInteger();
        int val3 = readInteger();
        std::string name = readString();
        if (trace)
            printf("[%s:%d] [%d] %s %x, %x, %x\n", __FUNCTION__, __LINE__, i, name.c_str(), val1, val2, val3);
    }
    int head8 = readInteger();
    int head9 = readInteger();
    printf("Parse place %x, %x\n", head8, head9);
    int mlen = readMessage();
    checkId(mlen, 2);
    int head11 = messageData[0].val;
    int head12 = messageData[1].val;
    printf("[%s:%d] head11 %x head12 %x\n", __FUNCTION__, __LINE__, head11, head12);
//trace = 1;
    mlen = readMessage();
    checkId(mlen, 1);
    checkId(messageData[0].val, 0);
    mlen = readMessage();
    checkId(mlen, 3);
    checkId(messageData[0].val, 0xdead0000);
    checkStr(messageData[1].str, placerHeader);
    checkId(messageData[2].val, 2);
    printf("Parse QDesign\n");
    mlen = readMessage();
    int sitetypeLen = messageData[0].val;
    for (int sitetypeIndex = 0; sitetypeIndex < sitetypeLen; sitetypeIndex++) {
        mlen = readMessage();
        checkId(messageData[0].val, 0xdead3333);
        checkId(mlen, 4); // site info
        int siteTypeId = messageData[1].val;
        std::string siteName = messageData[2].str;
        std::string siteType = translateName("SiteType", messageData[3].val);
        mlen = readMessage();
        checkId(mlen, 1);
        int IOpipType = messageData[0].val;
        mlen = readMessage();
        checkId(mlen, 1);
        int pipType = messageData[0].val;
        mlen = readMessage();
        checkId(mlen, 1);
        checkId(messageData[0].val, 0xdead4444); // loop for BEL
        mlen = readMessage();
        int belCount = messageData[0].val;
        printf("sitetype %s id %x", siteName.c_str(), siteTypeId);
        printf(" tile %s", site2Tile[siteName].c_str());
        printf(" %s\n", siteType.c_str());
        for (int belIndex = 0; belIndex < belCount; belIndex++) {
            mlen = readMessage();
            checkId(mlen, 1);
            int belNumber = messageData[0].val;
            std::string belType = translateName("Element", belNumber);
            bool isFF = endswith(belType, "FF");
            mlen = readMessage();
            checkId(mlen, 1);
            int cellCount = messageData[0].val;
            int lineId = -1;
            mlen = readMessage();
            checkId(messageData[0].val, 0xdead5555);
            lineValid = false;
            bool hasCell = false;
            int cell1, cell2, cell3;
            if (cellCount) {
                checkId(cellCount, 1);
                belStart(belNumber);
                for (int cellIndex = 0; cellIndex < cellCount; cellIndex++) {
                    readMessage(); // cell info
                    if (lineId == -1)
                        lineId = messageData[0].val;
                    assert(lineId == messageData[0].val);
                    cell1 = messageData[1].val;
                    cell2 = messageData[2].val;
                    cell3 = messageData[3].val;
                    hasCell = true;
                    if (isFF) {
                        if (cell1 != 1 || cell2 != 0 || cell3 != 0) {
                            printf("[%s:%d] cellnumber error %x %x %x\n", __FUNCTION__, __LINE__, cell1, cell2, cell3);
                            exit(-1);
                        }
                    }
                    else if (belType == "BUFBIDI") {
                        if (cell1 != 0xa || cell2 != 1 || cell3 != 0) {
                            printf("[%s:%d] cellnumber error %x %x %x\n", __FUNCTION__, __LINE__, cell1, cell2, cell3);
                            exit(-1);
                        }
                    }
                    else
                        printf(" [%x %x %x]", cell1, cell2, cell3);
                }
                printf("");
            }
            mlen = readMessage();
            checkId(mlen, 1);
            checkId(messageData[0].val, 0xdead6666);
            mlen = readMessage();
            checkId(mlen, 1);
            const char *sep = "";
            if (int nameCount = messageData[0].val) {
                belStart(belNumber);
                std::string pinStr = " {";
                bool hasData = false;
                int save1, save3;
                int pinWrap = 4;
                for (int nameIndex = 0; nameIndex < nameCount; nameIndex++) {
                    mlen = readMessage();
                    int pinId = messageData[0].val;
                    checkId(mlen, 1);
                    mlen = readMessage();
                    checkId(mlen, 1);
                    if (int pinCount = messageData[0].val) {
                    pinStr += sep;
                    for (int pinIndex = 0; pinIndex < pinCount; pinIndex++) {
                        readMessage(); // pin info
                        checkId(messageDataIndex, 4);
                        if (lineId == -1)
                            lineId = messageData[0].val;
                        assert(lineId == messageData[0].val);
                        int val1 = messageData[1].val;
                        int val3 = messageData[3].val;
                        assert(!hasCell || cell1 == val1);
                        if (!hasData || save1 != val1 || save3 != val3) {
                            pinStr += "[";
                            if (!hasCell)
                                pinStr += autostrH(val1) + " ";
                            pinStr += autostrH(val3) + "] ";
                        }
                        pinStr += messageData[2].str + ":" + translateName("ElementPin", pinId);
                        hasData = true;
                        save1 = val1;
                        save3 = val3;
                    }
                    sep = ", ";
                    if (--pinWrap == 0 && nameCount > 20) {
                        pinWrap = 4;
                        sep = ",\n        ";
                    }
                    }
                }
                pinStr += "}";
                if (isFF) {
                    if (pinStr != " {[0] CE:CE, C:CK, D:D, R:SR, Q:Q}") {
                        printf("[%s:%d] incorrect pins for FF '%s'\n", __FUNCTION__, __LINE__, pinStr.c_str());
                        exit(-1);
                    }
                    pinStr = "";
                }
                printf("%s", pinStr.c_str());
            }
            if (lineValid) {
                if (lineId == -1)
                    printf("\n");
                else
                    printf(" %s\n", translatePlace(lineId).c_str());
            }
        }
        mlen = readMessage();
        checkId(mlen, 1);
        checkId(messageData[0].val, 0);
        mlen = readMessage();
        int thisPip = messageData[0].val;
        checkId(mlen, 1);
        assert(thisPip == pipType ? pipType : IOpipType);
        mlen = readMessage();
        checkId(mlen, 1);
        checkId(messageData[0].val, 0xdead7777); // PIP
        mlen = readMessage();
        checkId(mlen, 1);
        int pipCount = messageData[0].val;
        assert((!IOpipType && thisPip == pipType) || (!pipType && IOpipType == thisPip && !pipCount));
        if (pipCount) {
        printf("    SITE_PIPS ");
        int pipWrap = 7;
        const char *sep = "";
        for (int pipIndex = 0; pipIndex < pipCount; pipIndex++) {
            readMessage();
            checkId(messageDataIndex, 4);
            int pipElement, pipUsed, pipElementPin;
            pipElement = messageData[0].val;
            pipElementPin = messageData[1].val;
            pipUsed = messageData[3].val;
            assert(pipUsed == 0 || pipUsed == 1);
            if (!pipUsed)
                continue; // unused
            checkId(messageData[2].val, 0x0f); // always "OUT"
            printf("%s", sep);
            if (--pipWrap == 0) {
                printf("\n        ");
                pipWrap = 7;
            }
            printf("%s:%s", translateName("Element", pipElement).c_str(),
                translateName("ElementPin", pipElementPin).c_str());
            sep = ", ";
        }
        printf("\n");
        }
        mlen = readMessage();
        checkId(mlen, 1);
        checkId(messageData[0].val, 0xdead8888); // net
        mlen = readMessage();
        checkId(mlen, 1);
        bool isSlice = startswith(siteType, "SLICE");
        if (int netCount = messageData[0].val) {
            const char *sep = "";
            printf("    SITE_PINS ");
            int swWrap = 2;
            std::list<std::string> pinNames;
            sitePinCount[siteType]["BASIC"]++;
            bool hasA6LUT_O6 = false;
            int countA6LUT_O6 = 0;
            for (int netIndex = 0; netIndex < netCount; netIndex++) {
                mlen = readMessage();
                checkId(mlen, 1);
                int pinNetId = messageData[0].val;
                mlen = readMessage();
                checkId(mlen, 1);
                if (int connCount = messageData[0].val) {
                    std::string pinName = translateName("SiteTypeNet", pinNetId);
                    if (pinName == "A6LUT_O6")
                        hasA6LUT_O6 = true;
                    pinNames.push_back(pinName);
                    std::string sep2 = "{", end = "";
                    bool isNoneNet = false;
                    std::string valStr = pinName;
                    for (int connIndex = 0; connIndex < connCount; connIndex++) {
                        mlen = readMessage();
                        checkId(mlen, 1);
                        int val0 = messageData[0].val;
                        std::string netName = lookupSiteNet(val0);
                        if (startswith(netName, "NONE"))
                            isNoneNet = true;
                        if (netName != "NONE3") {
                        valStr += sep2 + netName;
                        sep2 = ", ";
                        end = "}";
                        }
                    }
                    valStr += end;
                    countA6LUT_O6 += sitePinMandatory[siteType][pinName].sameA6LUT_O6;
                    if (isSlice)
                        sitePinCount[siteType][pinName]++;
                    if ((!sitePinMandatory[siteType][pinName].mandatory && !sitePinMandatory[siteType][pinName].sameA6LUT_O6) || end != "") {
                    printf("%s %s", sep, valStr.c_str());
                    sep = ",";
                    if (!isNoneNet)
                    if (--swWrap == 0) {
                        sep = ",\n        ";
                        swWrap = 2;
                    }
                    }
                }
            }
            printf("\n");
            if ((!hasA6LUT_O6 && (countA6LUT_O6 != 0)) || (hasA6LUT_O6 && countA6LUT_O6 != sitePinA6LUT_O6[siteType])) {
                printf("[%s:%d] A6LUT_O6 %d does not predict 'CARRY'/etc %d/%d\n", __FUNCTION__, __LINE__, hasA6LUT_O6, countA6LUT_O6, sitePinA6LUT_O6[siteType]);
                exit(-1);
            }
            if (isSlice && sitePinTemplate[siteType].size() < pinNames.size())
                sitePinTemplate[siteType] = pinNames;
        }
        mlen = readMessage();
        checkId(mlen, 1);
        checkId(messageData[0].val, 0xdead9999); // port
        checkId(mlen, 1);
        mlen = readMessage();
        if (int portCount = messageData[0].val) {
            printf("    port");
            for (int portIndex = 0; portIndex < portCount; portIndex++) {
                mlen = readMessage();
                checkId(mlen, 4);
                printf(" {%s %s}", messageData[0].str.c_str(), translateName("Element", messageData[1].val).c_str());
                checkId(messageData[2].val, 1);
                checkId(messageData[3].val, 0);
            }
            printf("\n");
        }
    }
    mlen = readMessage();
    checkId(mlen, 1);
    checkId(messageData[0].val, 0xdeaddddd); // locked
    mlen = readMessage();
    checkId(mlen, 1);
    checkId(messageData[0].val, 0);
    mlen = readMessage();
    checkId(mlen, 1);
    checkId(messageData[0].val, STRING);
    checkStr(messageData[0].str, "");
    mlen = readMessage();
    checkId(mlen, 1);
    checkId(messageData[0].val, 0xdeadeeee);
    mlen = readMessage();
    checkId(mlen, 1);
    checkId(messageData[0].val, 0);
    mlen = readMessage();
    checkId(mlen, 1);
    checkId(messageData[0].val, STRING);
    checkStr(messageData[0].str, "");
    mlen = readMessage();
    checkId(mlen, 2);
    checkId(messageData[0].val, 0xdeadcccc);
    checkId(messageData[1].val, 1);
    mlen = readMessage();
    checkId(mlen, 1);
    checkId(messageData[0].val, 1);

    printf("Parse design Q\n");
    checkId(readInteger(), 3); // routing version
    int tileCount = readInteger();
    for (int tileIndex = 0; tileIndex < tileCount; tileIndex++) {
        std::string tileTypeName = readString();
        int listCount = readInteger();
        for (int listIndex = 0; listIndex < listCount; listIndex++) {
            std::string name = readString();
            tilePin[tileTypeName].push_back(name);
        }
    }
    checkStr(readString(), "END_HEADER");
    bool first = true;
    int nameCount = readInteger();
    int vv2 = readInteger();
    int vv3 = readInteger();
    printf("Read nets %d, %d, %d\n", nameCount, vv2, vv3);
    for (int nameIndex = 0; nameIndex < nameCount; nameIndex++) {
        std::string netSignalName = readString();
        int netId = readInteger();
        printf("NET_%03x %s: ", netId, netSignalName.c_str());
        checkNetName(netId, netSignalName);
        if (first) {
            int extra = readInteger();
            checkId(extra, 0);
        }
        int sec = readInteger();
        checkId(sec, 0);
        if (first) {
            int extra = readInteger();
            checkId(extra, 0);
            extra = readInteger();
            checkId(extra, 0);
            extra = readInteger();
            checkId(extra, 0);
        }
        int groupCount = readInteger();
        int fourth = readInteger();
        checkId(fourth, 0);
        if (first) {
            int extra = readInteger();
            checkId(extra, 0);
            extra = readInteger();
            checkId(extra, 0);
        }
        readNodeList(first);
        printf("\n");
        assert(groupCount == 1);
    }
    int netCount = readInteger();
    printf("Global nets %d\n", netCount);
    int netNumber = 0;
    while(netNumber < netCount) {
        std::string netName = readString();
        int val = readInteger();
        int val2 = readInteger();
        int groupCount = readInteger();
        printf("global %s\n", netName.c_str());
        assert(val == netNumber + 1);
        assert(val2 + 1 == groupCount);
        int fourth = readInteger();
        checkId(fourth, 0);
        for (int groupIndex = 0; groupIndex < groupCount; groupIndex++) {
            printf("    list: ");
            readNodeList(first);
            printf("\n");
        }
        netNumber++;
    }
    mlen = readMessage();
    checkId(mlen, 1);
    checkId(messageData[0].val, 0xdeadaaaa);
    mlen = readMessage();
    checkId(mlen, 1);
    int lval = messageData[0].val;
    for (int lind = 0; lind < lval; lind++) {
        mlen = readMessage();
        checkId(mlen, 3);
        std::string signalName = messageData[0].str;
        int val1 = messageData[1].val;
        int netId = messageData[2].val;
        //printf("Site2Net %s SW_%03x NET_%03x\n", signalName.c_str(), val1, netId);
        siteNet[val1] = SiteNetInfo{netId, signalName};
    };
    mlen = readMessage();
    checkId(mlen, 1);
    checkId(messageData[0].val, 0xdeadbbbb);
    mlen = readMessage();
    checkId(mlen, 1);
    int sval = messageData[0].val;
    for (int sind = 0; sind < sval; sind++) {
        mlen = readMessage();
        checkId(mlen, 3);
        // ordinal of signal = messageData[2].val;
        //printf("PlaceName %s BEL_%03x\n", messageData[0].str.c_str(), messageData[1].val);
        placeName[messageData[1].val] = messageData[0].str;
    };
    printf("Parse device cache\n");
    mlen = readMessage();
    checkId(mlen, 3);
    checkId(messageData[0].val, 0xdead1111);
    checkStr(messageData[1].str, deviceCacheHeader);
    checkId(messageData[2].val, 1);
    std::string sectionIndex;
    do {
        mlen = readMessage();
        if (mlen == 2) {
            std::string name = messageData[0].str;
            int val = messageData[1].val;
            for (auto ch: name)
                if (islower(ch)) {
                    sectionIndex = name;
                    cache[sectionIndex].valr = val;
                    goto next;
                }
            if (cache[sectionIndex].cacheMap.find(val) != cache[sectionIndex].cacheMap.end()) {
                printf("duplicate cache %s %x\n", name.c_str(), val);
                exit(-1);
            }
            else
                cache[sectionIndex].cacheMap[val] = name;
        }
        else {
printf("[%s:%d] mlen %d\n", __FUNCTION__, __LINE__, mlen);
            for (int i = 0; i < mlen; i++)
printf("[%s:%d] [%d] %x\n", __FUNCTION__, __LINE__, i, messageData[i].val);
        }
next:;
    } while(mlen == 2);
    FILE *fcache;
    fcache = fopen("cacheData.h", "w");
    fprintf(fcache, "#define HAS_CACHE_DATA\n");
    for (auto citem: cache) {
        fprintf(fcache, "const char *lookupMap_%s[] = {\n", citem.first.c_str());
        for (auto item: citem.second.cacheMap)
            fprintf(fcache, "    \"%s\",\n", item.second.c_str());
        fprintf(fcache, "};\n");
    }
    fprintf(fcache, "CacheIndex lookupMap[] = {\n");
    for (auto citem: cache)
        fprintf(fcache, "    { \"%s\", %ld, lookupMap_%s},\n", citem.first.c_str(), citem.second.cacheMap.size(), citem.first.c_str());
    fprintf(fcache, "    { nullptr, -1, nullptr} };\n");

    for (auto citem: tilePin) {
        fprintf(fcache, "const char *lookupMap_%s[] = {\n", citem.first.c_str());
        int offset = 0;
        for (auto item: citem.second) {
            std::string valstr = item;
            if (citem.first == "INT_L") {
                int ind = valstr.find("_L");
                if (ind > 0)
                    valstr = valstr.substr(0, ind) + valstr.substr(ind+2);
            }
            fprintf(fcache, "    \"%s\",    // %s:%x\n", valstr.c_str(), citem.first.c_str(), offset);
            offset++;
        }
        fprintf(fcache, "};\n");
    }
    fprintf(fcache, "CacheIndex tilePinMap[] = {\n");
    for (auto citem: tilePin)
        fprintf(fcache, "    { \"%s\", %ld, lookupMap_%s},\n", citem.first.c_str(), citem.second.size(), citem.first.c_str());
    fprintf(fcache, "    { nullptr, -1, nullptr} };\n");
    fprintf(fcache, "PlaceInfo placeMap[] = {\n");
    for (auto citem: placeName)
        fprintf(fcache, "    { %d, \"%s\"},\n", citem.first, citem.second.c_str());
    fprintf(fcache, "    { -1, nullptr} };\n");
    fprintf(fcache, "SiteNetMap siteNetMap[] = {\n");
    for (auto citem: siteNet)
        fprintf(fcache, "    { %d, %d, %d, \"%s\"},\n", citem.first, citem.second.net, netExist[citem.second.net], citem.second.name.c_str());
    fprintf(fcache, "    { 0, 0, 0, nullptr} };\n");
    for (auto citem: sitePinTemplate) {
        fprintf(fcache, "SitePinInfo sitePinMap_%s[] = {    \n", citem.first.c_str());
        for (auto iitem: citem.second)
            fprintf(fcache, " {\"%s\", %d, %d},", iitem.c_str(),
               sitePinCount[citem.first]["BASIC"] == sitePinCount[citem.first][iitem],
               sitePinCount[citem.first]["A6LUT_O6"] == sitePinCount[citem.first][iitem]);
        fprintf(fcache, " };\n");
    }
    fprintf(fcache, "SitePinInfoMap sitePinMap[] = {    \n");
    for (auto citem: sitePinTemplate)
        fprintf(fcache, "    {\"%s\", sitePinMap_%s, %ld},\n", citem.first.c_str(), citem.first.c_str(), citem.second.size());
    fprintf(fcache, "    };\n");
    fclose(fcache);
}

int readCMessage(void)
{
    int vari = readVarint(&bufp, true); // dont trace, since 'assert' will check value
    int tagVal = readMessage();
    if(ttrace || vari != tagVal)
        printf("[%s:%d] var %d mes %d\n", __FUNCTION__, __LINE__, vari, tagVal);
    assert(vari == tagVal);
    if (!vari)
        checkByte(0);
    return vari;
}

void readPairList()
{
    while (messageDataIndex == 2 || messageDataIndex == 3) {
        readCMessage();
        readMessage();
    }
}

void dumpXn()
{
    printf("Parse header\n");
    checkStr(std::string(bufp, bufp + strlen(xnHeader)), xnHeader);
    bufp += strlen(xnHeader);
    skipBuf(0x52);
    int tagVal = readMessage();
    printf("[%s:%d] stringCount %d\n", __FUNCTION__, __LINE__, tagVal);
//mtrace = 1;
    tagVal = readMessage();
    int listCount = messageData[0].val;
printf("[%s:%d] %d val %d\n", __FUNCTION__, __LINE__, tagVal, listCount);
    for (int i = 0; i < listCount - 1; i++) {
        readMessage();
//printf("[%s:%d] %d index %d\n", __FUNCTION__, __LINE__, tagVal, messageDataIndex);
        assert(messageDataIndex == 6);
        int tagVal = readCMessage();
        if (tagVal) {
            readCMessage();
            readCMessage();
        }
    }
uint8_t const *zstart = bufp;
mtrace = 1;
    tagVal = readMessage(); // 6
printf("[%s:%d] %d\n", __FUNCTION__, __LINE__, tagVal);
    tagVal = readMessage(); // 0
printf("[%s:%d] %d\n", __FUNCTION__, __LINE__, tagVal);
    tagVal = readCMessage();
printf("[%s:%d] %d\n", __FUNCTION__, __LINE__, tagVal);
    tagVal = readMessage(); // 0
printf("[%s:%d] %d\n", __FUNCTION__, __LINE__, tagVal);
    tagVal = readMessage();
printf("[%s:%d] %d\n", __FUNCTION__, __LINE__, tagVal);
    assert(tagVal == 5);
printf("[%s:%d] 27914 len %ld\n", __FUNCTION__, __LINE__, bufp - zstart);
mtrace =0;
    for (int i = 0; i < 7; i++) {
        tagVal = readMessage();
printf("[%s:%d] [%d] %d\n", __FUNCTION__, __LINE__, i, tagVal);
    }
    readMessage();
    readPairList();
printf("[%s:%d] index %d\n", __FUNCTION__, __LINE__, messageDataIndex);
    do {
        readMessage();
    } while (messageDataIndex == 5);
    do {
        readPairList();
        assert(messageDataIndex == 4);
//mtrace = 1;
//memdump(bufp, 16, "EE");
        readMessage();
//mtrace = 0;
    } while (messageDataIndex == 3);
printf("[%s:%d] index %d\n", __FUNCTION__, __LINE__, messageDataIndex);
memdump(bufp-32, 128, "TT");
    while (messageDataIndex == 2) {
        if (!*bufp)
            checkByte(0);
        else
            readTag();
        tagVal = readMessage();
        //printf(" nexxx %d", tagVal);
        if (tagVal == 4) {
            tagVal = readMessage();
            //printf(" WAS4 %d", tagVal);
        }
        //printf("\n");
    }
printf("[%s:%d] overindex %d\n", __FUNCTION__, __LINE__, messageDataIndex);
    while (1) {
       readPairList();
       if (messageDataIndex == 0xc)
           break;
       assert(messageDataIndex == 4);
       readMessage();
    }
printf("[%s:%d] %d\n", __FUNCTION__, __LINE__, tagVal);
    tagVal = readMessage(); // 9
printf("[%s:%d] %d\n", __FUNCTION__, __LINE__, tagVal);
    tagVal = readMessage(); // b
printf("[%s:%d] %d\n", __FUNCTION__, __LINE__, tagVal);
    tagVal = readMessage(); // c
printf("[%s:%d] %d\n", __FUNCTION__, __LINE__, tagVal);
    tagVal = readMessage(); // d
printf("[%s:%d] %d\n", __FUNCTION__, __LINE__, tagVal);
    tagVal = readMessage(); // 32.
printf("[%s:%d] %d\n", __FUNCTION__, __LINE__, tagVal);
    do {
        readPairList();
        //printf("[%s:%d] index %d\n", __FUNCTION__, __LINE__, messageDataIndex);
        do {
            readMessage();
        } while (messageDataIndex == 5);
    } while(messageDataIndex != 4);
    printf("[%s:%d] index %d\n", __FUNCTION__, __LINE__, messageDataIndex);
    do {
        readMessage();
        //printf("[%s:%d] %d ", __FUNCTION__, __LINE__, messageDataIndex);
        //memdump(bufp, 16, "");
    } while (messageDataIndex >= 4);
    memdump(bufp, 16, "OVER");
}

int main(int argc, char *argv[])
{
    const char *filename = "xx.xdef";
    int bflag, ch;

    stripPrefix = getenv("STRIP_PREFIX");
    bflag = 0;
    while ((ch = getopt(argc, argv, "bf:")) != -1) {
        switch (ch) {
        case 'b':
            bflag = 1;
            break;
        case 'f':
            filename = optarg;
            break;
        case '?':
        default:
            printf("dumpXdef <args>\n");
            exit(-1);
        }
    }
    argc -= optind;
    argv += optind;
printf("[%s:%d] argc %d\n", __FUNCTION__, __LINE__, argc);
    int fdin = open(filename, O_RDONLY);
    if (fdin < 0) {
        printf("[%s:%d] can't open input file\n", __FUNCTION__, __LINE__);
    }
    long inlen = read(fdin, inbuf, sizeof(inbuf));
    close(fdin);
printf("[%s:%d] inlen 0x%lx\n", __FUNCTION__, __LINE__, inlen);

    if (bflag)
        dumpXn();
    else
        dumpXdef();
    printf("[%s:%d] done, unread length %ld\n", __FUNCTION__, __LINE__, (bufp - inbuf) - inlen);
    return 0;
}
