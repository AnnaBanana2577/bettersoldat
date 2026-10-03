#include "net/hwid.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "sha256.h" // the launcher's

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>
#endif

#define HWID_SALT "soldatreloaded hardware id 1:" // so the hash is this game's alone

// The system's ID of the install, its spaces trimmed, into `out`; false if there is none.
static bool machine_id(char *out, size_t size)
{
    out[0] = '\0';
#ifdef _WIN32
    wchar_t guid[128];
    DWORD bytes = sizeof guid;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, NULL, guid, &bytes) != ERROR_SUCCESS)
        return false;
    if (WideCharToMultiByte(CP_UTF8, 0, guid, -1, out, (int)size, NULL, NULL) <= 0) return false;
#else
    static const char *const FILES[] = {"/etc/machine-id", "/var/lib/dbus/machine-id"};
    for (size_t i = 0; i < sizeof FILES / sizeof FILES[0] && !out[0]; i++) {
        FILE *f = fopen(FILES[i], "rb");
        if (!f) continue;
        if (!fgets(out, (int)size, f)) out[0] = '\0';
        fclose(f);
    }
#endif
    size_t n = strlen(out);
    while (n > 0 && isspace((unsigned char)out[n - 1])) out[--n] = '\0';
    return n > 0;
}

void hwid_get(char out[NET_HWID_SIZE])
{
    out[0] = '\0';
    char id[256];
    if (!machine_id(id, sizeof id)) return;
    Sha256 s;
    sha256_init(&s);
    sha256_feed(&s, HWID_SALT, strlen(HWID_SALT));
    sha256_feed(&s, id, strlen(id));
    uint8_t digest[32];
    sha256_finish(&s, digest);
    char hex[65];
    sha256_to_hex(digest, hex);
    for (int i = 0; i < NET_HWID_SIZE - 1; i++) out[i] = (char)toupper((unsigned char)hex[i]);
    out[NET_HWID_SIZE - 1] = '\0';
}
