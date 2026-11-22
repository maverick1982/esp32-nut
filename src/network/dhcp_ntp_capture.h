#ifndef DHCP_NTP_CAPTURE_H
#define DHCP_NTP_CAPTURE_H

#include <stdint.h>

/**
 * @brief NTP server of DHCP option 42 of the last lease (US-145, issue #135).
 *
 * Written by the lwIP tcpip task through the wrap of dhcp_set_ntp_servers()
 * (-Wl,--wrap=dhcp_set_ntp_servers in platformio.ini), read by NtpClock::poll(). The
 * value is in the order of ip4_addr_t.addr (formatIpv4 of NtpSource.h), 0 without a
 * server. lwIP calls the function only for an ACK that carries option 42, so NtpClock
 * clears the value when the network goes down. One value for the board: Wi-Fi or
 * Ethernet, whichever runs the DHCP client (ADR 0010).
 */
namespace DhcpNtpCapture {
uint32_t server();
void clear();
}

#endif // DHCP_NTP_CAPTURE_H
