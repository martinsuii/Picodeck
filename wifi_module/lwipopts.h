#ifndef WIFI_MODULE_LWIPOPTS_H
#define WIFI_MODULE_LWIPOPTS_H

extern void wifi_lwip_assert(const char *msg);
#define LWIP_PLATFORM_ASSERT(x) wifi_lwip_assert(x)

#define NO_SYS                    1
#define LWIP_SOCKET               0
#define LWIP_NETCONN              0

#define MEM_LIBC_MALLOC           0
#define MEM_ALIGNMENT             4
#define MEM_SIZE                  (4 * 1024)

#define MEMP_NUM_TCP_PCB          7
#define MEMP_NUM_TCP_PCB_LISTEN   1
#define MEMP_NUM_TCP_SEG          8
#define MEMP_NUM_ARP_QUEUE        4
#define PBUF_POOL_SIZE            4
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
#define TCP_WND                   (2 * TCP_MSS)
#define TCP_SND_BUF               (2 * TCP_MSS)
#define TCP_SND_QUEUELEN          8

#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK   1
#define LWIP_NETIF_HOSTNAME        1
#define LWIP_NETIF_TX_SINGLE_PBUF  1
#define DHCP_DOES_ARP_CHECK        0
#define LWIP_DHCP_DOES_ACD_CHECK   0
#define LWIP_STATS                 0

#endif
