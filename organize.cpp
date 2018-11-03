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

// foreach foo [tiles get] { foreach bar [get_sites -of $foo] { puts "$foo: $bar" } }
typedef struct {
    std::string root;
    int siteX;
    int siteY;
} SiteInfo;
typedef struct {
    std::map<std::string, int> tiles;
    std::list<SiteInfo> sites;
} LocInfo;
#define BUFFER_SIZE 16384000
uint8_t inbuf[BUFFER_SIZE];
uint8_t *bufp = inbuf;

std::map<std::string, LocInfo> locData;
std::map<std::string, std::list<std::string>> abstractTile;
typedef std::__list_iterator<SiteInfo, void *> SiteIterator;

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

bool inline endswith(std::string str, std::string suffix)
{
    int skipl = str.length() - suffix.length();
    return skipl >= 0 && str.substr(skipl) == suffix;
}

bool inline startswith(std::string str, std::string suffix)
{
    return str.substr(0, suffix.length()) == suffix;
}

typedef struct {
    const char *root;
    int siteX;
    int siteY;
} MatchSite;

typedef struct {
    const char *name;
    const char *newName;
    MatchSite   node[20];
} MatchPattern;

#define ABSOLUTE   0x00100000
#define MINUS1     0x00200000
#define SETY       0x00400000
#define YPLUS0     0x00800000
#define YPLUS1     0x01000000
#define YTIMES2    0x02000000
#define YTIMES2P1  0x04000000
#define YDIV2      0x08000000

const char *GCC[] = {"CLBLM_R_", "INT_R_", nullptr};
const char *GCL[] = {"CLBLM_L_", "INT_L_", nullptr};
const char *GCR[] = {"CLBLL_R_", "INT_R_", nullptr};
const char *GLL[] = {"CLBLL_L_", "INT_L_", nullptr};
const char *GFL[] = {"CMT_FIFO_L_", nullptr};
const char *GHH[] = {"HCLK_IOI3_", nullptr};
const char *GBL[] = {"BRAM_L_", "INT_L_", nullptr};
const char *GBR[] = {"BRAM_R_", "INT_R_", nullptr};
const char *GDL[] = {"DSP_L_", "INT_L_", nullptr};
const char *GDR[] = {"DSP_R_", "INT_R_", nullptr};
const char *GIR[] = {"INT_R_", nullptr};
const char *GIL[] = {"INT_L_", nullptr};
const char *GRR[] = {"INT_R_", "RIOB33_", "RIOI3_", nullptr};
const char *GRS[] = {"INT_R_", "RIOB33_SING_", "RIOI3_SING_", nullptr};
const char *GRT[] = {"INT_R_", "RIOB33_", "RIOI3_TBYTETERM_", nullptr};
const char *GST[] = {"INT_R_", "RIOB33_", "RIOI3_TBYTESRC_", nullptr};
struct {
    const char *name;
    const char **params;
} groups[] = {
   {"GCL", GCL}, {"GCC", GCC}, {"GCR", GCR}, {"GLL", GLL}, {"GFL", GFL},
   {"GBL", GBL}, {"GBR", GBR}, {"GHH", GHH}, {"GDL", GDL}, {"GDR", GDR},
   {"GIR", GIR}, {"GIL", GIL}, {"GRR", GRR}, {"GRS", GRS}, {"GRT", GRT},
   {"GST", GST}, {nullptr}};

static MatchPattern tss[] = {
    { "GFL", "", {{"OUT_FIFO", ABSOLUTE+0, SETY}, {"IN_FIFO", ABSOLUTE+0, YPLUS0}}},
    { "GCC|GIL|GIR", "T0", {{"TIEOFF", 0, 0}}},
    { "GCC|GIL|GIR", "T1", {{"TIEOFF", 1, 0}}},
    { "GCC|GIL|GIR", "T2", {{"TIEOFF", 2, 0}}},
    { "GCR",  "", {{"TIEOFF", 1, 0}, {"SLICE", 7, 0}, {"SLICE", 8, 0}}},
    { "GCR", "6", {{"TIEOFF", 1, 0}, {"SLICE", 5, 0}, {"SLICE", 6, 0}}},
    { "GCR", "4", {{"TIEOFF", 1, 0}, {"SLICE", 3, 0}, {"SLICE", 4, 0}}},
    { "GCC",  "", {{"TIEOFF", 1, 0}, {"SLICE", 3, 0}, {"SLICE", 4, 0}}},
    { "GCC", "0", {{"TIEOFF", 0, 0}, {"SLICE", 1, 0}, {"SLICE", 2, 0}}},
    { "GCC", "14", {{"TIEOFF", 2, 0}, {"SLICE", 13, 0}, {"SLICE", 14, 0}}},
    { "GCC", "12", {{"TIEOFF", 2, 0}, {"SLICE", 11, 0}, {"SLICE", 12, 0}}},
    { "GCC", "_M1", {{"TIEOFF", 0, 0}, {"SLICE", MINUS1, 0}, {"SLICE", 0, 0}}},
    { "GCC", "8", {{"TIEOFF", 1, 0}, {"SLICE", 7, 0}, {"SLICE", 8, 0}}},
    { "GCC", "10", {{"TIEOFF", 1, 0}, {"SLICE", 9, 0}, {"SLICE", 10, 0}}},
    { "GCC", "102", {{"TIEOFF", 2, 0}, {"SLICE", 9, 0}, {"SLICE", 10, 0}}},
    { "GLL",  "", {{"SLICE", 1, 0}, {"SLICE", 0, 0}, {"TIEOFF", 0, 0}}},
    { "GLL", "13", {{"SLICE", 13, 0}, {"SLICE", 12, 0}, {"TIEOFF", 2, 0}}},
    { "GLL", "11", {{"SLICE", 11, 0}, {"SLICE", 10, 0}, {"TIEOFF", 2, 0}}},
    { "GLL", "7", {{"SLICE", 7, 0}, {"SLICE", 6, 0}, {"TIEOFF", 1, 0}}},
    { "GCL", "0", {{"SLICE", 3, 0}, {"SLICE", 2, 0}, {"TIEOFF", 0, 0}}},
    { "GCL",  "", {{"SLICE", 3, 0}, {"SLICE", 2, 0}, {"TIEOFF", 1, 0}}},
    { "GCL", "11", {{"SLICE", 11, 0}, {"SLICE", 10, 0}, {"TIEOFF", 2, 0}}},
    { "GCL", "9", {{"SLICE", 9, 0}, {"SLICE", 8, 0}, {"TIEOFF", 1, 0}}},
    { "GCL", "7", {{"SLICE", 7, 0}, {"SLICE", 6, 0}, {"TIEOFF", 1, 0}}},
    { "GCL", "5", {{"SLICE", 5, 0}, {"SLICE", 4, 0}, {"TIEOFF", 1, 0}}},
    { "GRS",  "", {{"TIEOFF", 2, 0}, {"OLOGIC", ABSOLUTE+0, 0}, {"ILOGIC", ABSOLUTE+0, 0}, {"IDELAY", ABSOLUTE+0, 0}, {"IOB", ABSOLUTE+0, 0}}},
    { "GRR|GST|GRT",  "", {{"TIEOFF", 2, 0}, {"OLOGIC", ABSOLUTE+0, 0}, {"ILOGIC", ABSOLUTE+0, 0},
          {"OLOGIC", ABSOLUTE+0, 1}, {"ILOGIC", ABSOLUTE+0, 1},
          {"IDELAY", ABSOLUTE+0, 0}, {"IDELAY", ABSOLUTE+0, 1},
          {"IOB", ABSOLUTE+0, 0}, {"IOB", ABSOLUTE+0, 1}}},
    { "GDR",  "", {{"TIEOFF", 0, 0}, {"DSP48", ABSOLUTE+0, SETY}, {"DSP48", ABSOLUTE+0, YPLUS1}, {"TIEOFF", 1, 0}}},
    { "GDL",  "1", {{"DSP48", ABSOLUTE+1, SETY}, {"DSP48", ABSOLUTE+1, YPLUS1}, {"TIEOFF", 1, 0}, {"TIEOFF", 2, 0}}},
    { "GBL",  "0", {{"RAMB36", ABSOLUTE+0, SETY}, {"RAMB18", ABSOLUTE+0, YTIMES2}, {"RAMB18", ABSOLUTE+0, YTIMES2P1}, {"TIEOFF", 0, 0}}},
    { "GBL",  "1", {{"RAMB36", ABSOLUTE+1, SETY}, {"RAMB18", ABSOLUTE+1, YTIMES2}, {"RAMB18", ABSOLUTE+1, YTIMES2P1}, {"TIEOFF", 1, 0}}},
    { "GBR",  "2", {{"TIEOFF", 2, 0}, {"RAMB18", ABSOLUTE+2, SETY}, {"RAMB18", ABSOLUTE+2, YPLUS1}, {"RAMB36", ABSOLUTE+2, YDIV2}}},
    {nullptr}};

void organizeNames()
{
int jca = 0;
    uint8_t *start = bufp;
    while (*bufp) {
        if (*bufp == '\n') {
            std::string item = std::string(start, bufp), site;
            int siteX = -1;
            int siteY = -1;
            int ind = item.find(": ");
            if (ind > 0) {
                site = item.substr(ind + 2);
                item = item.substr(0, ind);
//printf("[%s:%d] item '%s' site '%s'\n", __FUNCTION__, __LINE__, item.c_str(), site.c_str());
//if (++jca > 10) exit(-1);
                ind = site.find("_X");
                if (ind > 0) {
                    std::string temp = site.substr(ind+2);
                    site = site.substr(0, ind);
                    siteX = atoi(temp.c_str());
                    siteY = atoi(temp.substr(temp.find("Y")+1).c_str());
                }
            }
            ind = item.find("_X");
            if (ind > 0) {
                std::string loc = item.substr(ind+1);
                item = item.substr(0, ind + 1);
                locData[loc].tiles[item] = 1;
                locData[loc].sites.push_back(SiteInfo{site, siteX, siteY});
            }
            start = bufp + 1;
        }
        bufp++;
    }
    for (auto item: locData) {
        int index = 0;
        while (groups[index].name) {
            std::string name = groups[index].name;
            int ind = item.first.find("Y");
            std::string rem = item.first.substr(ind);
            std::string newName;
            int xoff = atoi(item.first.substr(1).c_str());
            int yoffBase = atoi(rem.substr(1).c_str());
            auto matchPattern = [&](MatchPattern *pattern) -> bool {
                int reuseY = -1;
                while (pattern->name) {
                int ind = 0;
                SiteIterator par = item.second.sites.begin();
                if (pattern->name == std::string("GCC|GIL|GIR")) {
                    if (name != "GCC" && name != "GIL" && name != "GIR")
                        goto nextm;
                }
                else if (pattern->name == std::string("GRR|GST|GRT")) {
                    if (name != "GRR" && name != "GST" && name != "GRT")
                        goto nextm;
                }
                else if (name != pattern->name)
                    goto nextm;
                newName = pattern->newName;
                while(pattern->node[ind].root) {
                    int tempX = pattern->node[ind].siteX;
                    if (tempX & ABSOLUTE)
                        tempX &= ~ABSOLUTE;
                    else if (tempX == MINUS1)
                        tempX = xoff - 1;
                    else
                        tempX += xoff;
                    int tempY = pattern->node[ind].siteY;
                    if (tempY == SETY) {
                        reuseY = par->siteY;
                        newName += "_" + autostr(reuseY);
                        tempY = reuseY;
                    }
                    else if (tempY == YPLUS0)
                        tempY = reuseY;
                    else if (tempY == YPLUS1)
                        tempY = reuseY + 1;
                    else if (tempY == YTIMES2)
                        tempY = 2 * reuseY;
                    else if (tempY == YTIMES2P1)
                        tempY = 2 * reuseY + 1;
                    else if (tempY == YDIV2) {
                        if (reuseY & 1)
                            goto nextm;
                        tempY = reuseY / 2;
                    }
                    else
                        tempY += yoffBase;
                    if (par == item.second.sites.end()
                     || par->root != pattern->node[ind].root
                     || par->siteX != tempX || par->siteY != tempY)
                        goto nextm;
                    par++;
                    ind++;
                }
                if (par != item.second.sites.end())
                    goto nextm;
                return true;
nextm:
                pattern++;
                }
                return false;
            };
            int i = 0;
            for (auto citem: item.second.tiles) {
                 if (!groups[index].params[i] || groups[index].params[i] != citem.first)
                     goto next2;
                 i++;
            }
            if (groups[index].params[i])
                goto next2;
            locData[item.first].tiles.clear();
            if (matchPattern(tss))
                abstractTile[name + newName].push_back(item.first);
            else
                locData[item.first].tiles[groups[index].name] = 1;
            break;
next2:;
            index++;
        }
    }
    for (auto item: locData) {
        if (!item.second.tiles.size())
            continue;
        printf("%s:", item.first.c_str());
        for (auto citem: item.second.tiles)
            printf(" %s", citem.first.c_str());
        if (item.second.sites.size()) {
            printf(" @:");
            for (auto citem: item.second.sites)
                printf(" %s_X%dY%d", citem.root.c_str(), citem.siteX, citem.siteY);
        }
        printf("\n");
    }
    for (auto item: abstractTile) {
        printf("%s:", item.first.c_str());
        for (auto citem: item.second)
            printf(" %s", citem.c_str());
        printf("\n");
    }
}

int main(int argc, char *argv[])
{
    const char *filename = "xx.original";
    int bflag, ch;

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

    organizeNames();
    printf("[%s:%d] done, unread length %ld\n", __FUNCTION__, __LINE__, (bufp - inbuf) - inlen);
    return 0;
}
