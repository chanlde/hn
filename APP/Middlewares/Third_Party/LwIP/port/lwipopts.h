#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* Single-task lwIP setup for Air780E PPPoS, MQTT, HTTP download, and UDP audio. */

#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0

#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ICMP                       1
#define LWIP_RAW                        0
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0
#define LWIP_NETIF_API                  0
#define LWIP_DHCP                       0
#define LWIP_AUTOIP                     0
#define LWIP_ARP                        0
#define LWIP_ETHERNET                   0
#define IP_FORWARD                      0
#define IP_REASSEMBLY                   0
#define IP_FRAG                         0
#define IP_OPTIONS_ALLOWED              0
#define LWIP_DNS                        1
#define DNS_MAX_SERVERS                 2
#define LWIP_ACD                        0

#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_NETIF_HOSTNAME             0

#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (40 * 1024)
#define MEMP_NUM_PBUF                   32
#define MEMP_NUM_NETBUF                 0
#define MEMP_NUM_NETCONN                0
#define MEMP_NUM_TCP_PCB                10
#define MEMP_NUM_TCP_PCB_LISTEN         0
#define MEMP_NUM_TCP_SEG                64
#define MEMP_NUM_UDP_PCB                4
#define MEMP_NUM_SYS_TIMEOUT            24

#define PBUF_POOL_SIZE                  24
#define PBUF_POOL_BUFSIZE               1536

/*
 * PPP link can carry normal IPv4 TCP segments. The lwIP default 536-byte MSS
 * made OTA arrive as hundreds of tiny chunks; use Ethernet-sized MSS/window
 * to improve HTTP download throughput over Air780E PPP.
 */
#define TCP_MSS                         1460
#define TCP_WND                         (8 * TCP_MSS)
#define TCP_SND_BUF                     (4 * TCP_MSS)
#define TCP_SND_QUEUELEN                ((4 * TCP_SND_BUF + (TCP_MSS - 1)) / TCP_MSS)
#define TCP_LISTEN_BACKLOG              0

#define LWIP_TIMERS                     1
#define LWIP_TIMERS_CUSTOM              0

#define PPP_SUPPORT                     1
#define PPPOS_SUPPORT                   1
#define PPP_IPV4_SUPPORT                1
#define PPP_IPV6_SUPPORT                0
#define PPP_NOTIFY_PHASE                1
#define PPP_SERVER                      0
#define PPP_INPROC_IRQ_SAFE             0

#define PPPOE_SUPPORT                   0
#define PPPOL2TP_SUPPORT                0
#define PAP_SUPPORT                     0
#define CHAP_SUPPORT                    0
#define MSCHAP_SUPPORT                  0
#define CBCP_SUPPORT                    0
#define EAP_SUPPORT                     0
#define CCP_SUPPORT                     0
#define ECP_SUPPORT                     0
#define MPPE_SUPPORT                    0
#define DEMAND_SUPPORT                  0
#define VJ_SUPPORT                      0

#define PPP_DEBUG                       LWIP_DBG_OFF
#define MEM_DEBUG                       LWIP_DBG_OFF
#define MEMP_DEBUG                      LWIP_DBG_OFF
#define PBUF_DEBUG                      LWIP_DBG_OFF
#define NETIF_DEBUG                     LWIP_DBG_OFF

#define LWIP_STATS                      0
#define LWIP_CHECKSUM_CTRL_PER_NETIF    0
#define LWIP_PROVIDE_ERRNO              1

#define LWIP_RAND()                     ((u32_t)sys_now())

#endif /* LWIPOPTS_H */
