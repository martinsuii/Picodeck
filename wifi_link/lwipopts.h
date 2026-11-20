// lwIP options for the WiFi Link app. Kept small: the app only serves one
// tiny line protocol, so no sockets API, no TCP window scaling, few buffers.

#ifndef WIFI_LINK_LWIPOPTS_H
#define WIFI_LINK_LWIPOPTS_H

#define NO_SYS                    1
#define LWIP_SOCKET               0
#define LWIP_NETCONN              0

#define MEM_LIBC_MALLOC           0
#define MEM_ALIGNMENT             4
#define MEM_SIZE                  (6 * 1024)

#define MEMP_NUM_TCP_PCB          4
#define MEMP_NUM_TCP_PCB_LISTEN   1
#define MEMP_NUM_TCP_SEG          16
#define MEMP_NUM_ARP_QUEUE        4
#define PBUF_POOL_SIZE            8
#define PBUF_POOL_BUFSIZE         1536

#define LWIP_ARP                  1
#define LWIP_ETHERNET             1
#define LWIP_ICMP                 1
#define LWIP_RAW                  1
#define LWIP_DHCP                 1
#define LWIP_DNS                  1
#define LWIP_UDP                  1
#define LWIP_TCP                  1
#define LWIP_TCP_KEEPALIVE        1

#define TCP_MSS                   536
#define TCP_WND                   (4 * TCP_MSS)
#define TCP_SND_BUF               (4 * TCP_MSS)
#define TCP_SND_QUEUELEN          ((4 * TCP_SND_BUF) / TCP_MSS)

#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK   1
#define LWIP_NETIF_HOSTNAME        1
#define LWIP_NETIF_TX_SINGLE_PBUF  1
#define DHCP_DOES_ARP_CHECK        0
#define LWIP_DHCP_DOES_ACD_CHECK   0
#define LWIP_STATS                 0

#endif
