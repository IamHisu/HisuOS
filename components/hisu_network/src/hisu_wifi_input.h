#pragma once
/* Pure validation shared by the radio service and host tests. */
int hisu_wifi_password_is_valid(const char *password);
int hisu_wifi_can_auto_join(int saved, int supported, int secure, const char *password);
int hisu_wifi_setup_password(const unsigned char random[8], char out[17]);
