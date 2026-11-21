// ****************************************************************************
//
//                  PicoDeck — shared store networking
//
//   The store index and packages are plain HTTP. The WiFi module's response
//   buffer is 4 KB including headers, so every package is fetched as numbered
//   3 KB chunks (url.000, url.001, ...).
//
//   Used by the app store and by Themes > Install from the internet.
//
// ****************************************************************************

#ifndef _STORE_NET_H
#define _STORE_NET_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORE_MAX_ITEMS  16
#define STORE_ID_LEN     16
#define STORE_VER_LEN    12
#define STORE_NAME_LEN   40
#define STORE_URL_LEN    64

typedef struct {
    char id[STORE_ID_LEN];
    char version[STORE_VER_LEN];
    char name[STORE_NAME_LEN];
    char url[STORE_URL_LEN];
    u32  size;
} StoreItem;

// Fetch and parse an index.txt:
//   # id|version|size|url|name
// Returns the number of entries (0 on failure).
int StoreFetchIndex(const char* index_url, StoreItem* out, int max);

// Download base_url + item->url as chunks into dest_path.
// progress may be NULL. The file is deleted if the download is incomplete.
Bool StoreDownloadChunks(const char* base_url, const StoreItem* item,
                         const char* dest_path,
                         void (*progress)(u32 done, u32 total));

#ifdef __cplusplus
}
#endif

#endif // _STORE_NET_H
