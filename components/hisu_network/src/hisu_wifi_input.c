#include "hisu_wifi_input.h"

int hisu_wifi_setup_password(char out[17])
{
    if (!out) return 0;
    static const char password[17] = "11121112";
    for (unsigned i = 0; i < sizeof(password); ++i) out[i] = password[i];
    return 1;
}

int hisu_wifi_can_auto_join(int saved, int supported, int secure, const char *password)
{
    if (!saved || !supported || !hisu_wifi_password_is_valid(password)) return 0;
    /* Reject security mismatches, including an open clone of a protected SSID. */
    return (password[0] != 0) == (secure != 0);
}

int hisu_wifi_password_is_valid(const char *password)
{
    if (!password) return 0;
    unsigned n = 0;
    while (password[n]) {
        if (n >= 64) return 0;
        ++n;
    }
    if (!n) return 1;
    if (n == 64) {
        for (unsigned i = 0; i < n; ++i) {
            char c = password[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
        }
        return 1;
    }
    if (n < 8) return 0;
    for (unsigned i = 0; i < n; ++i)
        if ((unsigned char)password[i] < 32 || (unsigned char)password[i] > 126) return 0;
    return 1;
}
