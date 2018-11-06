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
    std::map<std::string, std::list<SiteInfo>> tiles;
} LocInfo;
#define BUFFER_SIZE 16384000
uint8_t inbuf[BUFFER_SIZE];
uint8_t *bufp = inbuf;

std::map<std::string, LocInfo> locData;
std::map<std::string, std::map<std::string, int>> abstractTile;

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

SiteInfo *getChild(std::list<SiteInfo> &children, int ind)
{
    for (auto CI = children.begin(), CE = children.end(); CI != CE; CI++)
        if (ind-- == 0)
            return &*CI;
    return nullptr;
}

void organizeNames()
{
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
                locData[loc].tiles[item].push_back(SiteInfo{site, siteX, siteY});
            }
            start = bufp + 1;
        }
        bufp++;
    }
    for (auto item = locData.begin(), itemE = locData.end(); item != itemE; item++) {
        std::string lname = item->first;
        int baseX = atoi(lname.substr(1).c_str());
        int baseY = atoi(lname.substr(lname.find("Y")+1).c_str());
        char btemp[100];
        sprintf(btemp, "%04d:%04d", baseX, baseY);
        std::string lsort = btemp;
        //printf(" %s baseX %d baseY %d\n", lname.c_str(), baseX, baseY);
        for (auto citem = item->second.tiles.begin(), CE = item->second.tiles.end(); citem != CE; ) {
            std::string tname = citem->first;
            int psize = citem->second.size();
            SiteInfo *c0 = getChild(citem->second, 0);
            SiteInfo *c1 = getChild(citem->second, 1);
            SiteInfo *c2 = getChild(citem->second, 2);
            SiteInfo *c3 = getChild(citem->second, 3);
            SiteInfo *c4 = getChild(citem->second, 4);
            SiteInfo *c5 = getChild(citem->second, 5);
            if (((tname == "INT_L_" && (baseX & 1) == 0) || (tname == "INT_R_" && (baseX & 1) == 1))
                && psize == 1 && c0->root == "TIEOFF" && c0->siteY == baseY
                && (c0->siteX == baseX || c0->siteX == baseX + 1 || c0->siteX == baseX + 2)) {
                int diff = c0->siteX - baseX;
                //tname += autostr(diff);
                if (baseX < 8 ? diff == 0 : baseX < 22 ? diff == 1 : diff == 2)
                    goto addItem;
            }
            if ((tname == "CLBLL_R_" || tname == "CLBLL_L_" || tname == "CLBLM_R_" || tname == "CLBLM_L_")
             && psize == 2 && c0->root == "SLICE" && c1->root == "SLICE"
             && c0->siteY == c1->siteY && c0->siteY == baseY) {
                if (endswith(tname, "_R_") && (c0->siteX & 1) == 0 && c1->siteX == c0->siteX + 1 && (baseX & 1) == 1)
                    goto addItem;
                if (endswith(tname, "_L_") && (c1->siteX & 1) == 0 && c0->siteX == c1->siteX + 1 && (baseX & 1) == 0)
                    goto addItem;
            }
            if (tname == "CMT_FIFO_L_" && psize == 2
                 && c0->root == "OUT_FIFO" && c1->root == "IN_FIFO"
                 && c0->siteX == 0 && c1->siteX == 0 && c0->siteY == c1->siteY) {
                tname += autostr(c0->siteY);
                goto addItem;
            }
            if (tname == "BRAM_L_" && psize == 3
                 && c0->root == "RAMB36" && c1->root == "RAMB18" && c2->root == "RAMB18"
                 && c0->siteX == c1->siteX && c1->siteX == c2->siteX
                 && (c0->siteX == 0 || c0->siteX == 1)
                 && c0->siteY == baseY / 5
                 && c0->siteY * 2 == c1->siteY && c0->siteY * 2 + 1 == c2->siteY) {
                tname += autostr(c0->siteX);
                goto addItem;
            }
            if (tname == "BRAM_R_" && psize == 3
                 && c0->root == "RAMB18" && c1->root == "RAMB18" && c2->root == "RAMB36"
                 && c0->siteX == c1->siteX && c1->siteX == c2->siteX
                 && c0->siteX == 2
                 && c0->siteY == 2 * (baseY / 5)
                 && c0->siteY + 1 == c1->siteY && c0->siteY/2 == c2->siteY) {
                goto addItem;
            }
            if (((tname == "DSP_R_" && c0->siteX == 0) || (tname == "DSP_L_" && c0->siteX == 1)) && psize == 3
                 && c0->root == "DSP48" && c1->root == "DSP48" && c2->root == "TIEOFF"
                 && c1->siteX == c0->siteX && c2->siteX == baseX + 1
                 && c0->siteY == (baseY / 5) * 2
                 && c0->siteY + 1 == c1->siteY && c2->siteY == baseY) {
                goto addItem;
            }
            if (tname == "RIOB33_" && psize == 2
                 && c0->root == "IOB" && c1->root == "IOB"
                 && c0->siteX == 0 && c1->siteX == 0
                 && c0->siteY == baseY
                 && c0->siteY + 1 == c1->siteY) {
                goto addItem;
            }
            if (tname == "RIOB33_SING_" && psize == 1 && c0->root == "IOB"
                 && c0->siteX == 0
                 && c0->siteY == baseY) {
                goto addItem;
            }
            if (tname == "RIOI3_SING_" && psize == 3
                 && c0->root == "OLOGIC" && c1->root == "ILOGIC" && c2->root == "IDELAY"
                 && c0->siteX == 0
                 && c0->siteX == c1->siteX && c1->siteX == c2->siteX
                 && c0->siteY == baseY
                 && c0->siteY == c1->siteY && c1->siteY == c2->siteY) {
                goto addItem;
            }
            if ((tname == "RIOI3_" || tname == "RIOI3_TBYTETERM_" || tname == "RIOI3_TBYTESRC_")
                 && psize == 6
                 && c0->root == "OLOGIC" && c1->root == "ILOGIC"
                 && c2->root == "OLOGIC" && c3->root == "ILOGIC"
                 && c4->root == "IDELAY" && c5->root == "IDELAY"
                 && c0->siteX == 0
                 && c0->siteX == c1->siteX && c1->siteX == c2->siteX
                 && c2->siteX == c3->siteX && c3->siteX == c4->siteX
                 && c4->siteX == c5->siteX
                 && c0->siteY == baseY && c1->siteY == baseY
                 && c2->siteY == baseY+1 && c3->siteY == baseY+1
                 && c4->siteY == baseY && c5->siteY == baseY+1) {
                goto addItem;
            }
            if (tname == "PSS2_" && psize == 131) {
                int ind = 0, offset = 0;
                while (ind < 130) {
                    SiteInfo *c = getChild(citem->second, ind);
                    if (c->root != "IOPAD" || c->siteX != 1 || c->siteY != ind + offset + 1)
                        goto nextItem;
                    if (ind == 71)
                        offset = 4;
                    ind++;
                }
                if (SiteInfo *c = getChild(citem->second, 130))
                if (c->root == "PS7" && c->siteX == 0 && c->siteY == 0)
                    goto addItem;
nextItem:;
            }
            citem++;
            continue;
addItem:;
            abstractTile[tname][lsort] = 1;
            citem = item->second.tiles.erase(citem);
        }
    }
    for (auto item: locData) {
        if (item.second.tiles.size() == 0)
            continue;
        printf("%s:", item.first.c_str());
        for (auto citem: item.second.tiles) {
            printf(" %s", citem.first.c_str());
            if (citem.second.size())
                printf("{");
            for (auto citem: citem.second)
                printf(" %s_X%dY%d", citem.root.c_str(), citem.siteX, citem.siteY);
            if (citem.second.size())
                printf("}, ");
        }
        printf("\n");
    }
    for (auto item: abstractTile) {
        printf("%s:", item.first.c_str());
        bool inRun = false;
        int lastX = -1, lastY = -1, startY = -1;
        for (auto citem: item.second) {
            int offX = atoi(citem.first.c_str());
            int offY = atoi(citem.first.substr(citem.first.find(":")+1).c_str());
            if (!inRun || lastX != offX || offY != lastY + 1) {
                if (inRun && lastY != startY)
                    printf("-%d", lastY);
                printf(" X%dY%d", offX, offY);
                startY = offY;
                inRun = true;
            }
            lastY = offY;
            lastX = offX;
        }
        if (inRun && lastY != startY)
            printf("-%d", lastY);
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
