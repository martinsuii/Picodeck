// ****************************************************************************
//
//                  PicoDeck — SD file manager
//
// ****************************************************************************

#ifndef _FILE_MANAGER_H
#define _FILE_MANAGER_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Browse the SD card: navigate directories, delete files/folders.
// Returns on X at the root.
void FileManagerRun(void);

#ifdef __cplusplus
}
#endif

#endif // _FILE_MANAGER_H
