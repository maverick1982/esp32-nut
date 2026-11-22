#include "network/dhcp_ntp_capture.h"
#include <atomic>
#include <lwip/ip4_addr.h>

static std::atomic<uint32_t> s_server{0};

uint32_t DhcpNtpCapture::server() {
    return s_server.load();
}

void DhcpNtpCapture::clear() {
    s_server.store(0);
}

// Linked with -Wl,--wrap=dhcp_set_ntp_servers (platformio.ini). In the precompiled
// liblwip.a (esp32s3, esp32p4, esp32p4_es) the function is defined in sntp.c.obj and only
// referenced by dhcp.c.obj, so the DHCP client calls this one. lwIP calls it in the tcpip
// task for an ACK that carries option 42, before it assigns the address: no log, no heap,
// no lock here. The real function is not called: with esp_sntp_servermode_dhcp(true) it
// would write the DHCP server at index 0 of SNTP, over the server of the user, and with
// it off it does nothing. NtpClock chooses the server instead (NtpSourcePolicy).
// CONFIG_LWIP_DHCP_MAX_NTP_SERVERS is 1: only the first server reaches us.
extern "C" void __wrap_dhcp_set_ntp_servers(u8_t num, const ip4_addr_t* servers) {
    s_server.store((num > 0 && servers) ? servers[0].addr : 0);
}
