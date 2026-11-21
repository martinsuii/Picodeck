// ****************************************************************************
//
//                  PicoDeck — shared store networking
//
// ****************************************************************************

#include "../include.h"
#include "store_net.h"
#include "native_wifi.h"

static void StoreCopy(char* dst, const char* src, int max)
{
    int i = 0;
    if (max <= 0) return;
    for (; src[i] && i < max - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

static int StoreParseInt(const char* s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

// Strip the HTTP status line and headers. Returns the body pointer.
static const char* StoreBody(const char* buf, int len, int* outLen)
{
    for (int i = 0; i + 3 < len; i++)
    {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n')
        {
            *outLen = len - i - 4;
            return buf + i + 4;
        }
    }
    *outLen = len;
    return buf;
}

static Bool StoreIs404(const char* buf, int len)
{
    return len > 11 && buf[9] == '4' && buf[10] == '0' && buf[11] == '4';
}

int StoreFetchIndex(const char* index_url, StoreItem* out, int max)
{
    if (!index_url || !out || max <= 0) return 0;

    int len = 0;
    const char* buf = NativeWifiHttpGet(index_url, &len);
    if (!buf || len <= 0) return 0;

    int bodyLen = 0;
    const char* body = StoreBody(buf, len, &bodyLen);

    int count = 0;
    int i = 0;
    while (i < bodyLen && count < max)
    {
        int e = i;
        while (e < bodyLen && body[e] != '\r' && body[e] != '\n') e++;

        if (e > i && body[i] != '#')
        {
            char f[5][48];
            int fi = 0;
            int p = i;
            while (fi < 5 && p < e)
            {
                int n = 0;
                while (p < e && body[p] != '|' && n < 47)
                    f[fi][n++] = body[p++];
                f[fi][n] = 0;
                fi++;
                if (p < e && body[p] == '|') p++;
            }
            if (fi >= 2 && f[0][0])
            {
                StoreItem* it = &out[count++];
                StoreCopy(it->id, f[0], STORE_ID_LEN);
                StoreCopy(it->version, fi > 1 ? f[1] : "", STORE_VER_LEN);
                it->size = (u32)StoreParseInt(fi > 2 ? f[2] : "0");
                StoreCopy(it->url, fi > 3 ? f[3] : "", STORE_URL_LEN);
                StoreCopy(it->name, fi > 4 ? f[4] : f[0], STORE_NAME_LEN);
            }
        }

        i = e;
        while (i < bodyLen && (body[i] == '\r' || body[i] == '\n')) i++;
    }

    return count;
}

Bool StoreDownloadChunks(const char* base_url, const StoreItem* item,
                         const char* dest_path,
                         void (*progress)(u32 done, u32 total))
{
    if (!base_url || !item || !dest_path) return False;
    if (item->size == 0 || item->size > 1024u * 1024u || !item->url[0])
        return False;

    sFile f;
    FileInit(&f);
    if (!FileCreate(&f, dest_path)) return False;

    u32 done = 0;
    int chunk = 0;

    // 400 chunks is 1.2 MB: well past the package limit.
    while (done < item->size && chunk < 400)
    {
        char url[160];
        strcpy(url, base_url);
        strcat(url, item->url);
        strcat(url, ".");

        int l = (int)StrLen(url);
        url[l]     = (char)('0' + (chunk / 100) % 10);
        url[l + 1] = (char)('0' + (chunk / 10) % 10);
        url[l + 2] = (char)('0' + chunk % 10);
        url[l + 3] = 0;

        int len = 0;
        const char* buf = NativeWifiHttpGet(url, &len);
        if (!buf || len <= 0) break;
        if (StoreIs404(buf, len)) break;      // no more chunks

        int bodyLen = 0;
        const char* body = StoreBody(buf, len, &bodyLen);
        if (bodyLen <= 0) break;

        if (done + (u32)bodyLen > item->size)
            bodyLen = (int)(item->size - done);

        if (progress) progress(done, item->size);
        if (FileWrite(&f, body, (u32)bodyLen) != (u32)bodyLen) break;

        done += (u32)bodyLen;
        chunk++;
    }

    FileClose(&f);

    if (done != item->size)
    {
        FileDelete(dest_path);
        return False;
    }

    if (progress) progress(item->size, item->size);
    return True;
}
