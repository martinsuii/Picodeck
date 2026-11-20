// PicoPad example app — raw TCP test
// A: connect + GET /   Y: close   X: exit
// Host is read from config.ini: host=example.com

#include "include.h"

static char g_host[64] = "example.com";
static char g_reply[1024];
static int  g_sock = -1;
static int  g_len = 0;
static int  g_state = 0;

static void DrawScreen(void)
{
    DrawClear();
    DrawText2("TCP test", 10, 10, AppTheme(APP_TH_TITLE));
    DrawRect(10, 36, WIDTH - 20, 1, AppTheme(APP_TH_DIM));

    DrawText("Host:", 12, 50, AppTheme(APP_TH_DIM));
    DrawText(g_host, 70, 50, AppTheme(APP_TH_TEXT));

    DrawText("Module:", 12, 68, AppTheme(APP_TH_DIM));
    DrawText(AppWifiReady() ? "ready" : "not ready",
             90, 68, AppWifiReady() ? AppTheme(APP_TH_OK) : AppTheme(APP_TH_WARN));

    DrawText("Socket:", 12, 86, AppTheme(APP_TH_DIM));
    {
        char b[24];
        if (g_sock < 0) strcpy(b, "closed");
        else
        {
            strcpy(b, "sock ");
            DecNum(b + StrLen(b), (u32)g_sock, 0);
            strcat(b, " state ");
            DecNum(b + StrLen(b), (u32)g_state, 0);
        }
        DrawText(b, 90, 86, AppTheme(APP_TH_TEXT));
    }

    if (g_len > 0)
    {
        DrawRect(10, 104, WIDTH - 20, 1, AppTheme(APP_TH_DIM));
        int y = 112;
        int i = 0;
        int line = 0;
        while (i < g_len && line < 8)
        {
            char tmp[41];
            int n = 0;
            while (i < g_len && n < 40)
            {
                char c = g_reply[i++];
                if (c == '\r') continue;
                if (c == '\n') break;
                if (c < 32) c = '.';
                tmp[n++] = c;
            }
            tmp[n] = 0;
            DrawText(tmp, 12, y, AppTheme(APP_TH_TEXT));
            y += 10;
            line++;
        }
    }

    DrawText("A: connect+get   Y: close   X: exit",
             12, 212, AppTheme(APP_TH_DIM));
    DispUpdate();
}

void AppMain(void)
{
    AppConfigRead("host", g_host, sizeof(g_host));

    DrawScreen();

    while (1)
    {
        u8 k = KeyGet();
        if (k == KEY_X) return;

        if (k == KEY_A)
        {
            if (!AppNetFeatures())
            {
                DrawText("No raw TCP support (flash module 1.2.18+)",
                         12, 195, AppTheme(APP_TH_ERR));
                DispUpdate();
                WaitMs(1500);
                DrawScreen();
                continue;
            }

            g_sock = AppNetOpen(g_host, 80);
            g_len = 0;
            if (g_sock < 0)
            {
                DrawText("Open failed", 12, 195, AppTheme(APP_TH_ERR));
                DispUpdate();
                WaitMs(1200);
                DrawScreen();
                continue;
            }

            // Wait for the connection.
            for (int i = 0; i < 500 && AppNetState(g_sock) != 3; i++)
            {
                g_state = AppNetState(g_sock);
                if (g_state == 5 || g_state == 4) break;
                WaitMs(20);
            }
            g_state = AppNetState(g_sock);

            if (g_state == 3)
            {
                char req[160];
                strcpy(req, "GET / HTTP/1.0\r\nHost: ");
                strcat(req, g_host);
                strcat(req, "\r\nConnection: close\r\n\r\n");
                AppNetSend(g_sock, req, (int)StrLen(req));

                // Poll for a reply for a few seconds.
                for (int t = 0; t < 300; t++)
                {
                    int n = AppNetRecv(g_sock, g_reply + g_len,
                                       (int)sizeof(g_reply) - g_len);
                    if (n > 0) g_len += n;
                    if (AppNetState(g_sock) == 4) break;
                    if (g_len >= (int)sizeof(g_reply)) break;
                    WaitMs(20);
                }
                g_state = AppNetState(g_sock);
            }
            DrawScreen();
        }
        else if (k == KEY_Y)
        {
            if (g_sock >= 0) AppNetClose(g_sock);
            g_sock = -1;
            g_state = 0;
            DrawScreen();
        }

        WaitMs(30);
    }
}
