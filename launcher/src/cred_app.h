// ****************************************************************************
//
//                  PicoDeck — credential manager
//
//   Generates the one-time token used to register a developer account at
//   pico.msuii.dev. Only reachable while /Conf/dev.txt exists.
//
//   The token is a 16 hex digit invite code, shown grouped and as a QR that
//   opens the registration form with the token filled in. It is stored in
//   /Conf/cred.txt so it survives a reboot.
//
//   This is a soft gate, not a secret: anyone who can read the SD card or
//   enable developer mode can make one. The server enforces one account per
//   token.
//
// ****************************************************************************

#ifndef _CRED_APP_H
#define _CRED_APP_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Settings > Credential manager (only listed when developer mode is on).
void CredentialRun(void);

#ifdef __cplusplus
}
#endif

#endif // _CRED_APP_H
