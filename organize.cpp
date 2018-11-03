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

typedef struct {
    std::list<std::string> tiles;
    std::list<std::string> sites;
} LocInfo;
#define BUFFER_SIZE 16384000
uint8_t inbuf[BUFFER_SIZE];
uint8_t *bufp = inbuf;

std::map<std::string, LocInfo> locData;

bool inline endswith(std::string str, std::string suffix)
{
    int skipl = str.length() - suffix.length();
    return skipl >= 0 && str.substr(skipl) == suffix;
}

bool inline startswith(std::string str, std::string suffix)
{
    return str.substr(0, suffix.length()) == suffix;
}

void organizeNames()
{
int jca = 0;
    uint8_t *start = bufp;
    while (*bufp) {
        if (*bufp == '\n') {
            std::string item = std::string(start, bufp);
            int ind = item.find("_X");
            if (ind > 0) {
                std::string loc = item.substr(ind+1);
                item = item.substr(0, ind + 1);
                if (item[0] == '@')
                    locData[loc].sites.push_back(item.substr(1));
                else
                    locData[loc].tiles.push_back(item);
            }
            start = bufp + 1;
        }
        bufp++;
    }
    for (auto item: locData) {
        printf("%s:", item.first.c_str());
        for (auto citem: item.second.tiles)
            printf(" %s", citem.c_str());
        if (item.second.sites.size()) {
            printf(" @:");
            for (auto citem: item.second.sites)
                printf(" %s", citem.c_str());
        }
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
