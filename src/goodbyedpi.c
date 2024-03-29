/*
 * GoodbyeDPI — Passive DPI blocker and Active DPI circumvention utility.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <in6addr.h>
#include <ws2tcpip.h>
#include <WinSock2.h>
#include <winreg.h>
#include "windivert.h"
#include "goodbyedpi.h"
#include "utils/repl_str.h"
#include "service.h"
#include "dnsredir.h"
#include "ttltrack.h"
#include "blackwhitelist.h"
#include "fakepackets.h"
#include "ip_list.h"

// My mingw installation does not load inet_pton definition for some reason
WINSOCK_API_LINKAGE INT WSAAPI inet_pton(INT Family, LPCSTR pStringBuf, PVOID pAddr);

#define GOODBYEDPI_VERSION "v0.2.2"

#define die() do { sleep(20); exit(EXIT_FAILURE); } while (FALSE)

#define MAX_FILTERS 6

/*
#define DIVERT_NO_LOCALNETSv4_DST "(" \
                   "(ip.DstAddr < 127.0.0.1 or ip.DstAddr > 127.255.255.255) and " \
                   "(ip.DstAddr < 10.0.0.0 or ip.DstAddr > 10.255.255.255) and " \
                   "(ip.DstAddr < 192.168.0.0 or ip.DstAddr > 192.168.255.255) and " \
                   "(ip.DstAddr < 172.16.0.0 or ip.DstAddr > 172.31.255.255) and " \
                   "(ip.DstAddr < 169.254.0.0 or ip.DstAddr > 169.254.255.255)" \
                   ")"
#define DIVERT_NO_LOCALNETSv4_SRC "(" \
                   "(ip.SrcAddr < 127.0.0.1 or ip.SrcAddr > 127.255.255.255) and " \
                   "(ip.SrcAddr < 10.0.0.0 or ip.SrcAddr > 10.255.255.255) and " \
                   "(ip.SrcAddr < 192.168.0.0 or ip.SrcAddr > 192.168.255.255) and " \
                   "(ip.SrcAddr < 172.16.0.0 or ip.SrcAddr > 172.31.255.255) and " \
                   "(ip.SrcAddr < 169.254.0.0 or ip.SrcAddr > 169.254.255.255)" \
                   ")"
                   */
#define DIVERT_NO_LOCALNETSv4_DST "(" \
                   "(ip.DstAddr < 127.0.0.1 or ip.DstAddr > 127.255.255.255) and " \
                   "(ip.DstAddr < 10.0.0.0 or ip.DstAddr > 10.255.255.255) and " \
                   "(ip.DstAddr < 172.16.0.0 or ip.DstAddr > 172.31.255.255) and " \
                   "(ip.DstAddr < 169.254.0.0 or ip.DstAddr > 169.254.255.255)" \
                   ")"
#define DIVERT_NO_LOCALNETSv4_SRC "(" \
                   "(ip.SrcAddr < 127.0.0.1 or ip.SrcAddr > 127.255.255.255) and " \
                   "(ip.SrcAddr < 10.0.0.0 or ip.SrcAddr > 10.255.255.255) and " \
                   "(ip.SrcAddr < 172.16.0.0 or ip.SrcAddr > 172.31.255.255) and " \
                   "(ip.SrcAddr < 169.254.0.0 or ip.SrcAddr > 169.254.255.255)" \
                   ")"

#define DIVERT_NO_LOCALNETSv6_DST "(" \
                   "(ipv6.DstAddr > ::1) and " \
                   "(ipv6.DstAddr < 2001::0 or ipv6.DstAddr > 2001:1::0) and " \
                   "(ipv6.DstAddr < fc00::0 or ipv6.DstAddr > fe00::0) and " \
                   "(ipv6.DstAddr < fe80::0 or ipv6.DstAddr > fec0::0) and " \
                   "(ipv6.DstAddr < ff00::0 or ipv6.DstAddr > ffff::0)" \
                   ")"
#define DIVERT_NO_LOCALNETSv6_SRC "(" \
                   "(ipv6.SrcAddr > ::1) and " \
                   "(ipv6.SrcAddr < 2001::0 or ipv6.SrcAddr > 2001:1::0) and " \
                   "(ipv6.SrcAddr < fc00::0 or ipv6.SrcAddr > fe00::0) and " \
                   "(ipv6.SrcAddr < fe80::0 or ipv6.SrcAddr > fec0::0) and " \
                   "(ipv6.SrcAddr < ff00::0 or ipv6.SrcAddr > ffff::0)" \
                   ")"

#define FORWARD_DIVERT_NO_LOCALNETSv4_DST "(" \
                   "(ip.DstAddr < 127.0.0.1 or ip.DstAddr > 127.255.255.255) and " \
                   "(ip.DstAddr < 10.0.0.0 or ip.DstAddr > 10.255.255.255) and " \
                   "(ip.DstAddr < 172.16.0.0 or ip.DstAddr > 172.31.255.255) and " \
                   "(ip.DstAddr < 169.254.0.0 or ip.DstAddr > 169.254.255.255)" \
                   ")"
#define FORWARD_DIVERT_NO_LOCALNETSv4_SRC "(" \
                   "(ip.SrcAddr < 127.0.0.1 or ip.SrcAddr > 127.255.255.255) and " \
                   "(ip.SrcAddr < 10.0.0.0 or ip.SrcAddr > 10.255.255.255) and " \
                   "(ip.SrcAddr < 172.16.0.0 or ip.SrcAddr > 172.31.255.255) and " \
                   "(ip.SrcAddr < 169.254.0.0 or ip.SrcAddr > 169.254.255.255)" \
                   ")"

/* #IPID# is a template to find&replace */
#define IPID_TEMPLATE "#IPID#"
#define MAXPAYLOADSIZE_TEMPLATE "#MAXPAYLOADSIZE#"
#define SUBNET_START_TEMPLATE "#SUBNET_START#"
#define SUBNET_END_TEMPLATE "#SUBNET_END#"
#define FORWARD_INBOUND "(ip.DstAddr >= 192.168.1.1) and (ip.DstAddr < 192.168.1.255) and " \
        "(ip.SrcAddr < " SUBNET_START_TEMPLATE " or ip.SrcAddr > " SUBNET_END_TEMPLATE ")"
#define FORWARD_OUTBOUND "(ip.SrcAddr >= 192.168.1.1) and (ip.SrcAddr < 192.168.1.255) and " \
        "(ip.DstAddr < " SUBNET_START_TEMPLATE " or ip.DstAddr > " SUBNET_END_TEMPLATE ")"

#define ALL_PUBLIC_IP_STRING_TEMPLATE(ipTempl) "((" ipTempl " > 1.0.0.0 and " ipTempl " < 9.255.255.255) or " \
        "(" ipTempl " > 11.0.0.0 and " ipTempl " < 100.63.255.255) or " \
        "(" ipTempl " > 100.128.0.0 and " ipTempl " < 126.255.255.255) or " \
        "(" ipTempl " > 128.0.0.0 and " ipTempl " < 169.253.255.255) or " \
        "(" ipTempl " > 169.255.0.0 and " ipTempl " < 172.15.255.255) or " \
        "(" ipTempl " > 172.32.0.0 and " ipTempl " < 191.255.255.255) or " \
        "(" ipTempl " > 192.0.1.0 and " ipTempl " < 192.0.1.255) or " \
        "(" ipTempl " > 192.0.3.0 and " ipTempl " < 192.88.98.255) or " \
        "(" ipTempl " > 192.88.100.0 and " ipTempl " < 192.167.255.255) or " \
        "(" ipTempl " > 192.169.0.0 and " ipTempl " < 198.17.255.255) or " \
        "(" ipTempl " > 198.20.0.0 and " ipTempl " < 198.51.99.255) or " \
        "(" ipTempl " > 198.51.101.0 and " ipTempl " < 203.0.112.255) or " \
        "(" ipTempl " > 198.51.101.0 and " ipTempl " < 203.0.112.255))"

#define ALL_PACKETS_FILTER_STRING_TEMPLATE "(" ALL_PUBLIC_IP_STRING_TEMPLATE("ip.DstAddr") ") or (" ALL_PUBLIC_IP_STRING_TEMPLATE("ip.SrcAddr") ")"

//#define FORWARD_INBOUND "(ip.DstAddr >= 192.168.1.1) and (ip.DstAddr < 192.168.254.255) or " \
//        "(ip.SrcAddr >= 192.168.1.1 and ip.SrcAddr < 192.168.254.255)"
//#define FORWARD_OUTBOUND "(ip.DstAddr >= 192.168.1.1) and (ip.DstAddr < 192.168.254.255) or " \
//        "(ip.SrcAddr >= 192.168.1.1 and ip.SrcAddr < 192.168.254.255)"
#define FILTER_STRING_TEMPLATE \
        "(tcp and !impostor and !loopback " MAXPAYLOADSIZE_TEMPLATE " and " \
        "((inbound and (" \
         "(" \
          "(" \
           "(ipv6 or (ip.Id >= 0x0 and ip.Id <= 0xF) " IPID_TEMPLATE \
           ") and " \
           "tcp.SrcPort == 80 and tcp.Ack" \
          ") or " \
          "((tcp.SrcPort == 80 or tcp.SrcPort == 443) and tcp.Ack and tcp.Syn)" \
         ")" \
         " and (" DIVERT_NO_LOCALNETSv4_SRC " or " DIVERT_NO_LOCALNETSv6_SRC "))) or " \
        "(outbound and " \
         "(tcp.DstPort == 80 or tcp.DstPort == 443) and tcp.Ack and " \
         "(" DIVERT_NO_LOCALNETSv4_DST " or " DIVERT_NO_LOCALNETSv6_DST "))" \
        "))"
#define FORWARD_FILTER_STRING_TEMPLATE \
        "(tcp " MAXPAYLOADSIZE_TEMPLATE " and " \
        "(((" FORWARD_INBOUND ") and (" \
         "(" \
          "(" \
           "(ipv6 or (ip.Id >= 0x0 and ip.Id <= 0xF) " IPID_TEMPLATE \
           ") and " \
           "tcp.SrcPort == 80 and tcp.Ack" \
          ") or " \
          "((tcp.SrcPort == 80 or tcp.SrcPort == 443) and tcp.Ack and tcp.Syn)" \
         ")" \
         " and (" FORWARD_DIVERT_NO_LOCALNETSv4_SRC " or " DIVERT_NO_LOCALNETSv6_SRC "))) or " \
        "((" FORWARD_OUTBOUND ") and " \
         "(tcp.DstPort == 80 or tcp.DstPort == 443) and tcp.Ack and " \
         "(" FORWARD_DIVERT_NO_LOCALNETSv4_DST " or " DIVERT_NO_LOCALNETSv6_DST "))" \
        "))"
#define FILTER_PASSIVE_STRING_TEMPLATE "inbound and ip and tcp and " \
        "!impostor and !loopback and " \
        "((ip.Id <= 0xF and ip.Id >= 0x0) " IPID_TEMPLATE ") and " \
        "(tcp.SrcPort == 443 or tcp.SrcPort == 80) and tcp.Rst and " \
        DIVERT_NO_LOCALNETSv4_SRC
#define FILTER_PASSIVE_FORWARD_STRING_TEMPLATE \
        "((ip.DstAddr >= " SUBNET_START_TEMPLATE ") and (ip.DstAddr < " SUBNET_END_TEMPLATE "))" \
        " and ip and tcp and " \
        "((ip.Id <= 0xF and ip.Id >= 0x0) " IPID_TEMPLATE ") and " \
        "(tcp.SrcPort == 443 or tcp.SrcPort == 80) and tcp.Rst and " \
        FORWARD_DIVERT_NO_LOCALNETSv4_SRC

#define SET_HTTP_FRAGMENT_SIZE_OPTION(fragment_size) do { \
    if (!http_fragment_size) { \
        http_fragment_size = (unsigned int)fragment_size; \
    } \
    else if (http_fragment_size != (unsigned int)fragment_size) { \
        printf( \
            "WARNING: HTTP fragment size is already set to %u, not changing.\n", \
            http_fragment_size \
        ); \
    } \
} while (FALSE)

#define TCP_HANDLE_OUTGOING_TTL_PARSE_PACKET_IF() \
    if ((packet_v4 && tcp_handle_outgoing(&ppIpHdr->SrcAddr, &ppIpHdr->DstAddr, \
                        ppTcpHdr->SrcPort, ppTcpHdr->DstPort, \
                        &tcp_conn_info, 0)) \
        || \
        (packet_v6 && tcp_handle_outgoing(ppIpV6Hdr->SrcAddr, ppIpV6Hdr->DstAddr, \
                        ppTcpHdr->SrcPort, ppTcpHdr->DstPort, \
                        &tcp_conn_info, 1)))

#define TCP_HANDLE_OUTGOING_FAKE_PACKET(func) do { \
    consts.should_send_fake = 1; \
    if (consts.do_auto_ttl || consts.ttl_min_nhops) { \
        TCP_HANDLE_OUTGOING_TTL_PARSE_PACKET_IF() { \
            if (consts.do_auto_ttl) { \
                /* If Auto TTL mode */ \
                consts.ttl_of_fake_packet = tcp_get_auto_ttl(tcp_conn_info.ttl, consts.auto_ttl_1, consts.auto_ttl_2, \
                                                      consts.ttl_min_nhops, consts.auto_ttl_max); \
                if (consts.do_tcp_verb) { \
                    printf("Connection TTL = %d, Fake TTL = %d\n", tcp_conn_info.ttl, consts.ttl_of_fake_packet); \
                } \
            } \
            else if (consts.ttl_min_nhops) { \
                /* If not Auto TTL mode but --min-ttl is set */ \
                if (!tcp_get_auto_ttl(tcp_conn_info.ttl, 0, 0, consts.ttl_min_nhops, 0)) { \
                    /* Send only if nhops >= min_ttl */ \
                    consts.should_send_fake = 0; \
                } \
            } \
        } \
    } \
    if (consts.should_send_fake) \
        func(w_filter, &addr, packet, packetLen, packet_v6, \
             consts.ttl_of_fake_packet, consts.do_wrong_chksum, consts.do_wrong_seq); \
} while (FALSE)


static int running_from_service = 0;
static int exiting = 0;
static HANDLE filters[MAX_FILTERS];
static int filter_num = 0;
static const char http10_redirect_302[] = "HTTP/1.0 302 ";
static const char http11_redirect_302[] = "HTTP/1.1 302 ";
static const char http_host_find[] = "\r\nHost: ";
static const char http_host_replace[] = "\r\nhoSt: ";
static const char http_useragent_find[] = "\r\nUser-Agent: ";
static const char location_http[] = "\r\nLocation: http://";
static const char connection_close[] = "\r\nConnection: close";
static const char *http_methods[] = {
    "GET ",
    "HEAD ",
    "POST ",
    "PUT ",
    "DELETE ",
    "CONNECT ",
    "OPTIONS ",
};

static struct option long_options[] = {
    {"port",        required_argument, 0,  'z' },
    {"dns-addr",    required_argument, 0,  'd' },
    {"dns-port",    required_argument, 0,  'g' },
    {"dnsv6-addr",  required_argument, 0,  '!' },
    {"dnsv6-port",  required_argument, 0,  '@' },
    {"dns-verb",    no_argument,       0,  'v' },
    {"blacklist",   required_argument, 0,  'b' },
    {"allow-no-sni",no_argument,       0,  ']' },
    {"ip-id",       required_argument, 0,  'i' },
    {"set-ttl",     required_argument, 0,  '$' },
    {"min-ttl",     required_argument, 0,  '[' },
    {"auto-ttl",    optional_argument, 0,  '+' },
    {"wrong-chksum",no_argument,       0,  '%' },
    {"wrong-seq",   no_argument,       0,  ')' },
    {"native-frag", no_argument,       0,  '*' },
    {"reverse-frag",no_argument,       0,  '(' },
    {"max-payload", optional_argument, 0,  '|' },
    {0,             0,                 0,   0  }
};

static char *filter_string = NULL;
static char *filter_passive_string = NULL;
static char *filter_forward_string = NULL;
static char *filter_passive_forward_string = NULL;
static char* all_packets_filter_string = NULL;
static char *subnet_start = NULL;
static char *subnet_end = NULL;

static LimitedIpList GlobalIpList = { 6, 0, NULL };

static void change_filter(char** string, int proto, int port, const char* udp, const char* tcp) {
    char* current_filter = *string;
    size_t new_filter_size = strlen(current_filter) +
        (proto == IPPROTO_UDP ? strlen(udp) : strlen(tcp)) + 16;
    char* new_filter = malloc(new_filter_size);

    strcpy(new_filter, current_filter);
    if (proto == IPPROTO_UDP)
        sprintf(new_filter + strlen(new_filter), udp, port, port);
    else
        sprintf(new_filter + strlen(new_filter), tcp, port, port);

    *string = new_filter;
    free(current_filter);
}

static void add_filter_str(int proto, int port) {
    const char *udp = " or (udp and !impostor and !loopback and " \
                      "(udp.SrcPort == %d or udp.DstPort == %d))";
    const char *tcp = " or (tcp and !impostor and !loopback " MAXPAYLOADSIZE_TEMPLATE " and " \
                      "(tcp.SrcPort == %d or tcp.DstPort == %d))";
    const char* udp_forward = " or (udp and !impostor and " \
        "(udp.SrcPort == %d or udp.DstPort == %d))";
    const char* tcp_forward = " or (tcp and !impostor " MAXPAYLOADSIZE_TEMPLATE " and " \
        "(tcp.SrcPort == %d or tcp.DstPort == %d))";

    change_filter(&filter_string, proto, port, udp, tcp);
    change_filter(&filter_forward_string, proto, port, udp_forward, tcp_forward);
}

static void replace_template_and_clear_strings(char** replacable_string, const char* from, const char* to) {
    char* newstr = repl_str(*replacable_string, from, to);
    free(*replacable_string);
    *replacable_string = newstr;
}

static void add_ip_id_str(int id) {
    const char *ipid = " or ip.Id == %d";
    char *addfilter = malloc(strlen(ipid) + 16);

    sprintf(addfilter, ipid, id);

    replace_template_and_clear_strings(&filter_string, IPID_TEMPLATE, addfilter);
    replace_template_and_clear_strings(&filter_passive_string, IPID_TEMPLATE, addfilter);
    replace_template_and_clear_strings(&filter_forward_string, IPID_TEMPLATE, addfilter);
    replace_template_and_clear_strings(&filter_passive_forward_string, IPID_TEMPLATE, addfilter);
    free(addfilter);
}

static void add_maxpayloadsize_str(unsigned short maxpayload) {
    /* 0x47455420 is "GET ", 0x504F5354 is "POST", big endian. */
    const char *maxpayloadsize_str = "and (tcp.PayloadLength ? tcp.PayloadLength < %hu or tcp.Payload32[0] == 0x47455420 or tcp.Payload32[0] == 0x504F5354 : true)";
    char *addfilter = malloc(strlen(maxpayloadsize_str) + 16);

    sprintf(addfilter, maxpayloadsize_str, maxpayload);

    replace_template_and_clear_strings(&filter_string, MAXPAYLOADSIZE_TEMPLATE, addfilter);
    replace_template_and_clear_strings(&filter_forward_string, MAXPAYLOADSIZE_TEMPLATE, addfilter);
    free(addfilter);
}

static void finalize_filter_strings() {
    replace_template_and_clear_strings(&filter_string, IPID_TEMPLATE, "");
    replace_template_and_clear_strings(&filter_string, MAXPAYLOADSIZE_TEMPLATE, "");

    replace_template_and_clear_strings(&filter_forward_string, IPID_TEMPLATE, "");
    replace_template_and_clear_strings(&filter_forward_string, MAXPAYLOADSIZE_TEMPLATE, "");

    replace_template_and_clear_strings(&filter_passive_string, IPID_TEMPLATE, "");

    replace_template_and_clear_strings(&filter_passive_forward_string, IPID_TEMPLATE, "");
}

static char* dumb_memmem(const char* haystack, unsigned int hlen,
                         const char* needle, unsigned int nlen)
{
    // naive implementation
    if (nlen > hlen) return NULL;
    size_t i;
    for (i=0; i<hlen-nlen+1; i++) {
        if (memcmp(haystack+i,needle,nlen)==0) {
            return (char*)(haystack+i);
        }
    }
    return NULL;
}

unsigned short int atousi(const char *str, const char *msg) {
    long unsigned int res = strtoul(str, NULL, 10u);
    enum {
        limitValue=0xFFFFu
    };

    if(res > limitValue) {
        puts(msg);
        exit(EXIT_FAILURE);
    }
    return (unsigned short int)res;
}

BYTE atoub(const char *str, const char *msg) {
    long unsigned int res = strtoul(str, NULL, 10u);
    enum {
        limitValue=0xFFu
    };

    if(res > limitValue) {
        puts(msg);
        exit(EXIT_FAILURE);
    }
    return (BYTE)res;
}


static HANDLE init(char *filter, UINT64 flags, int isForward, INT16 priority) {
    LPTSTR errormessage = NULL;
    DWORD errorcode = 0;
    debug("%s\n", filter);
    filter = WinDivertOpen(filter, isForward ? WINDIVERT_LAYER_NETWORK_FORWARD : WINDIVERT_LAYER_NETWORK, priority, flags);
    if (filter != INVALID_HANDLE_VALUE)
        return filter;
    errorcode = GetLastError();
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL, errorcode, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT),
                  (LPTSTR)&errormessage, 0, NULL);
    printf("Error opening filter: %s", errormessage);
    LocalFree(errormessage);
    if (errorcode == 577)
        printf("Windows Server 2016 systems must have secure boot disabled to be "
               "able to load WinDivert driver.\n"
               "Windows 7 systems must be up-to-date or at least have KB3033929 installed.\n"
               "https://www.microsoft.com/en-us/download/details.aspx?id=46078\n\n"
               "WARNING! If you see this error on Windows 7, it means your system is horribly "
               "outdated and SHOULD NOT BE USED TO ACCESS THE INTERNET!\n"
               "Most probably, you don't have security patches installed and anyone in you LAN or "
               "public Wi-Fi network can get full access to your computer (MS17-010 and others).\n"
               "You should install updates IMMEDIATELY.\n");
    return NULL;
}

static int deinit(HANDLE handle) {
    if (handle) {
        WinDivertShutdown(handle, WINDIVERT_SHUTDOWN_BOTH);
        WinDivertClose(handle);
        return TRUE;
    }
    return FALSE;
}

void deinit_all() {
    for (int i = 0; i < filter_num; i++) {
        deinit(filters[i]);
    }
    filter_num = 0;
}

static void sigint_handler(int sig __attribute__((unused))) {
    exiting = 1;
    deinit_all();
    exit(EXIT_SUCCESS);
}

static void mix_case(char *pktdata, unsigned int pktlen) {
    unsigned int i;

    if (pktlen <= 0) return;
    for (i = 0; i < pktlen; i++) {
        if (i % 2) {
            pktdata[i] = (char) toupper(pktdata[i]);
        }
    }
}

static int is_passivedpi_redirect(const char *pktdata, unsigned int pktlen) {
    /* First check if this is HTTP 302 redirect */
    if (memcmp(pktdata, http11_redirect_302, sizeof(http11_redirect_302)-1) == 0 ||
        memcmp(pktdata, http10_redirect_302, sizeof(http10_redirect_302)-1) == 0)
    {
        /* Then check if this is a redirect to new http site with Connection: close */
        if (dumb_memmem(pktdata, pktlen, location_http, sizeof(location_http)-1) &&
            dumb_memmem(pktdata, pktlen, connection_close, sizeof(connection_close)-1)) {
            return TRUE;
        }
    }
    return FALSE;
}

static int find_header_and_get_info(const char *pktdata, unsigned int pktlen,
                const char *hdrname,
                char **hdrnameaddr,
                char **hdrvalueaddr, unsigned int *hdrvaluelen) {
    char *data_addr_rn;
    char *hdr_begin;

    *hdrvaluelen = 0u;
    *hdrnameaddr = NULL;
    *hdrvalueaddr = NULL;

    /* Search for the header */
    hdr_begin = dumb_memmem(pktdata, pktlen,
                hdrname, strlen(hdrname));
    if (!hdr_begin) return FALSE;
    if (pktdata > hdr_begin) return FALSE;

    /* Set header address */
    *hdrnameaddr = hdr_begin;
    *hdrvalueaddr = hdr_begin + strlen(hdrname);

    /* Search for header end (\r\n) */
    data_addr_rn = dumb_memmem(*hdrvalueaddr,
                        pktlen - (uintptr_t)(*hdrvalueaddr - pktdata),
                        "\r\n", 2);
    if (data_addr_rn) {
        *hdrvaluelen = (uintptr_t)(data_addr_rn - *hdrvalueaddr);
        if (*hdrvaluelen >= 3 && *hdrvaluelen <= HOST_MAXLEN)
            return TRUE;
    }
    return FALSE;
}

/**
 * Very crude Server Name Indication (TLS ClientHello hostname) extractor.
 */
static int extract_sni(const char *pktdata, unsigned int pktlen,
                    char **hostnameaddr, unsigned int *hostnamelen) {
    unsigned int ptr = 0;
    unsigned const char *d = (unsigned const char *)pktdata;
    unsigned const char *hnaddr = 0;
    int hnlen = 0;

    while (ptr + 8 < pktlen) {
        /* Search for specific Extensions sequence */
        if (d[ptr] == '\0' && d[ptr+1] == '\0' && d[ptr+2] == '\0' &&
            d[ptr+4] == '\0' && d[ptr+6] == '\0' && d[ptr+7] == '\0' &&
            /* Check Extension length, Server Name list length
            *  and Server Name length relations
            */
            d[ptr+3] - d[ptr+5] == 2 && d[ptr+5] - d[ptr+8] == 3)
            {
                if (ptr + 8 + d[ptr+8] > pktlen) {
                    return FALSE;
                }
                hnaddr = &d[ptr+9];
                hnlen = d[ptr+8];
                /* Limit hostname size up to 253 bytes */
                if (hnlen < 3 || hnlen > HOST_MAXLEN) {
                    return FALSE;
                }
                /* Validate that hostname has only ascii lowercase characters */
                for (int i=0; i<hnlen; i++) {
                    if (!( (hnaddr[i] >= '0' && hnaddr[i] <= '9') ||
                         (hnaddr[i] >= 'a' && hnaddr[i] <= 'z') ||
                         hnaddr[i] == '.' || hnaddr[i] == '-'))
                    {
                        return FALSE;
                    }
                }
                *hostnameaddr = (char*)hnaddr;
                *hostnamelen = (unsigned int)hnlen;
                return TRUE;
            }
        ptr++;
    }
    return FALSE;
}

static inline void change_window_size(const PWINDIVERT_TCPHDR ppTcpHdr, unsigned int size) {
    if (size >= 1 && size <= 0xFFFFu) {
        ppTcpHdr->Window = htons((u_short)size);
    }
}

/* HTTP method end without trailing space */
static PVOID find_http_method_end(const char *pkt, unsigned int http_frag, int *is_fragmented) {
    unsigned int i;
    for (i = 0; i<(sizeof(http_methods) / sizeof(*http_methods)); i++) {
        if (memcmp(pkt, http_methods[i], strlen(http_methods[i])) == 0) {
            if (is_fragmented)
                *is_fragmented = 0;
            return (char*)pkt + strlen(http_methods[i]) - 1;
        }
        /* Try to find HTTP method in a second part of fragmented packet */
        if ((http_frag == 1 || http_frag == 2) &&
            memcmp(pkt, http_methods[i] + http_frag,
                   strlen(http_methods[i]) - http_frag) == 0
           )
        {
            if (is_fragmented)
                *is_fragmented = 1;
            return (char*)pkt + strlen(http_methods[i]) - http_frag - 1;
        }
    }
    return NULL;
}

/** Fragment and send the packet.
 *
 * This function cuts off the end of the packet (step=0) or
 * the beginning of the packet (step=1) with fragment_size bytes.
 */
static void send_native_fragment(HANDLE w_filter, WINDIVERT_ADDRESS addr,
                        char *packet, UINT packetLen, PVOID packet_data,
                        UINT packet_dataLen, int packet_v4, int packet_v6,
                        PWINDIVERT_IPHDR ppIpHdr, PWINDIVERT_IPV6HDR ppIpV6Hdr,
                        PWINDIVERT_TCPHDR ppTcpHdr,
                        unsigned int fragment_size, int step) {
    char packet_bak[MAX_PACKET_SIZE];
    memcpy(packet_bak, packet, packetLen);
    UINT orig_packetLen = packetLen;

    if (fragment_size >= packet_dataLen) {
        if (step == 1)
            fragment_size = 0;
        else
            return;
    }

    if (step == 0) {
        if (packet_v4)
            ppIpHdr->Length = htons(
                ntohs(ppIpHdr->Length) -
                packet_dataLen + fragment_size
            );
        else if (packet_v6)
            ppIpV6Hdr->Length = htons(
                ntohs(ppIpV6Hdr->Length) -
                packet_dataLen + fragment_size
            );
        //printf("step0 (%d:%d), pp:%d, was:%d, now:%d\n",
        //                packet_v4, packet_v6, ntohs(ppIpHdr->Length),
        //                packetLen, packetLen - packet_dataLen + fragment_size);
        packetLen = packetLen - packet_dataLen + fragment_size;
    }

    else if (step == 1) {
        if (packet_v4)
            ppIpHdr->Length = htons(
                ntohs(ppIpHdr->Length) - fragment_size
            );
        else if (packet_v6)
            ppIpV6Hdr->Length = htons(
                ntohs(ppIpV6Hdr->Length) - fragment_size
            );
        //printf("step1 (%d:%d), pp:%d, was:%d, now:%d\n", packet_v4, packet_v6, ntohs(ppIpHdr->Length),
        //                packetLen, packetLen - fragment_size);
        memmove(packet_data,
                (char*)packet_data + fragment_size,
                packet_dataLen - fragment_size);
        packetLen -= fragment_size;

        ppTcpHdr->SeqNum = htonl(ntohl(ppTcpHdr->SeqNum) + fragment_size);
    }

    addr.IPChecksum = 0;
    addr.TCPChecksum = 0;

    WinDivertHelperCalcChecksums(
        packet, packetLen, &addr, 0
    );
    WinDivertSend(
        w_filter, packet,
        packetLen,
        NULL, &addr
    );
    memcpy(packet, packet_bak, orig_packetLen);
    //printf("Sent native fragment of %d size (step%d)\n", packetLen, step);
}

int get_subnet() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\SharedAccess\\Parameters",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char subnet[16];
        DWORD subnetSize = 16;
        if (RegGetValueA(hKey, NULL, "ScopeAddress", RRF_RT_REG_SZ, NULL, (PVOID)&subnet, &subnetSize) 
            != ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return -1;
        }

        subnet_start = strdup(subnet);

        char* substring = strstr(strstr(strstr(subnet, ".") + 1, ".") + 1, ".") + 1;
        substring[0] = '2';
        substring[1] = '5';
        substring[2] = '5';
        substring[3] = '\0';

        subnet_end = strdup(subnet);

        RegCloseKey(hKey);
        return 0;
    }

    return -1;
}

UINT32 from_big_endian_to_little_endian(UINT32 num) {
    return ((num >> 24) & 0xff) | // move byte 3 to byte 0
        ((num << 8) & 0xff0000) | // move byte 1 to byte 2
        ((num >> 8) & 0xff00) | // move byte 2 to byte 1
        ((num << 24) & 0xff000000); // byte 0 to byte 3
}

int IsOutbound(int is_forward, UINT32 src, WINDIVERT_ADDRESS addr) {
    if (!is_forward)
        return addr.Outbound;
    UINT32 subnet_start_num = from_big_endian_to_little_endian(inet_addr("192.168.1.1")),
        subnet_end_num = from_big_endian_to_little_endian(inet_addr("192.168.1.255"));
    src = from_big_endian_to_little_endian(src);
    return (src >= subnet_start_num && src <= subnet_end_num);
}

typedef struct {
    int do_fragment_https, do_passivedpi;
    unsigned https_fragment_size;
    int do_fake_packet, do_native_frag, do_blacklist, do_allow_no_sni, do_http_allports, do_fragment_http;
    unsigned http_fragment_size;
    int do_host, do_host_removespace, do_host_mixedcase, do_fragment_http_persistent,
        do_additional_space, do_auto_ttl;
    BYTE ttl_min_nhops;
    int do_tcp_verb, do_dnsv4_redirect, do_dnsv6_redirect, do_dns_verb;
    uint32_t dnsv4_addr;
    uint16_t dnsv4_port;
    struct in6_addr dnsv6_addr;
    uint16_t dnsv6_port;
    BYTE should_send_fake, ttl_of_fake_packet, auto_ttl_1, auto_ttl_2, auto_ttl_max;
    int do_wrong_chksum, do_wrong_seq, do_reverse_frag;
} IntConsts;

int AnalyzePacket(HANDLE w_filter, char packet[9016], UINT packetLen, WINDIVERT_ADDRESS addr,
    IntConsts consts, int is_forward)
{
    int sni_ok = 0, should_reinject = 1, should_recalc_checksum = 0,
        http_req_fragmented, packet_v4 = 0, packet_v6 = 0;

    PWINDIVERT_IPHDR ppIpHdr = (PWINDIVERT_IPHDR)NULL;
    PWINDIVERT_IPV6HDR ppIpV6Hdr = (PWINDIVERT_IPV6HDR)NULL;
    PWINDIVERT_TCPHDR ppTcpHdr = (PWINDIVERT_TCPHDR)NULL;
    PWINDIVERT_UDPHDR ppUdpHdr = (PWINDIVERT_UDPHDR)NULL;
    static enum packet_type_e {
        unknown,
        ipv4_tcp, ipv4_tcp_data, ipv4_udp_data,
        ipv6_tcp, ipv6_tcp_data, ipv6_udp_data
    } packet_type = unknown;
    PVOID packet_data;
    UINT packet_dataLen;
    char* host_addr, * useragent_addr, * method_addr,
        * hdr_name_addr = NULL, * hdr_value_addr = NULL;
    unsigned int host_len, useragent_len, hdr_value_len;
    conntrack_info_t dns_conn_info;
    tcp_conntrack_info_t tcp_conn_info;

    // Parse network packet and set it's type
    if (WinDivertHelperParsePacket(packet, packetLen, &ppIpHdr,
        &ppIpV6Hdr, NULL, NULL, NULL, &ppTcpHdr, &ppUdpHdr, &packet_data, &packet_dataLen,
        NULL, NULL))
    {
        if (ppIpHdr) {
            packet_v4 = 1;
            if (ppTcpHdr) {
                packet_type = ipv4_tcp;
                if (packet_data) {
                    packet_type = ipv4_tcp_data;
                }
            }
            else if (ppUdpHdr && packet_data) {
                packet_type = ipv4_udp_data;
            }
        }

        else if (ppIpV6Hdr) {
            packet_v6 = 1;
            if (ppTcpHdr) {
                packet_type = ipv6_tcp;
                if (packet_data) {
                    packet_type = ipv6_tcp_data;
                }
            }
            else if (ppUdpHdr && packet_data) {
                packet_type = ipv6_udp_data;
            }
        }
    }
    addr.Outbound = IsOutbound(is_forward, ppIpHdr->SrcAddr, addr);
    char srcAddr[INET_ADDRSTRLEN], destAddr[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &(ppIpHdr->SrcAddr), srcAddr, sizeof(srcAddr)))
        debug("Didn't return char src");
    if (!inet_ntop(AF_INET, &(ppIpHdr->DstAddr), destAddr, sizeof(destAddr)))
        debug("Didn't return char dest");
    debug("Got %s packet, len=%d! SrcAddr = %s, DstAddr = %s, StartSrc = %s, EndSrc = %s\n", addr.Outbound ? "outbound" : "inbound",
        packetLen, srcAddr, destAddr,
        "192.168.1.0", "192.168.1.255");
        //from_big_endian_to_little_endian(inet_addr(subnet_start)),
        //from_big_endian_to_little_endian(inet_addr(subnet_end)));
    debug("packet_type: %d, packet_v4: %d, packet_v6: %d\n", packet_type, packet_v4, packet_v6);

    if (packet_v6 && is_forward) {
        debug("packet wasn\'t analyzed\n");
        return;
    }

    if (packet_type == ipv4_tcp_data || packet_type == ipv6_tcp_data) {
        //printf("Got parsed packet, len=%d!\n", packet_dataLen);
        /* Got a TCP packet WITH DATA */
        debug("ipv4_tcp_data or ipv6_tcp_data\n");

        /* Handle INBOUND packet with data and find HTTP REDIRECT in there */
        if (!addr.Outbound && packet_dataLen > 16) {
            /* If INBOUND packet with DATA (tcp.Ack) */
            debug("INBOUND && packet_dataLen > 16\n");

            /* Drop packets from filter with HTTP 30x Redirect */
            if (consts.do_passivedpi && is_passivedpi_redirect(packet_data, packet_dataLen)) {
                debug("do_passivedpi && is_passivedpi_redirect\n");
                if (packet_v4) {
                    //printf("Dropping HTTP Redirect packet!\n");
                    debug("packet_v4\n");
                    should_reinject = 0;
                }
                else if (packet_v6 && WINDIVERT_IPV6HDR_GET_FLOWLABEL(ppIpV6Hdr) == 0x0) {
                    /* Contrary to IPv4 where we get only packets with IP ID 0x0-0xF,
                     * for IPv6 we got all the incoming data packets since we can't
                     * filter them in a driver.
                     *
                     * Handle only IPv6 Flow Label == 0x0 for now
                     */
                     //printf("Dropping HTTP Redirect packet!\n");
                    debug("packet_v6 && WINDIVERT_IPV6HDR_GET_FLOWLABEL\n");
                    should_reinject = 0;
                }
            }
        }
        /* Handle OUTBOUND packet on port 443, search for something that resembles
         * TLS handshake, send fake request.
         */
        else if (addr.Outbound &&
            ((consts.do_fragment_https ? packet_dataLen == consts.https_fragment_size : 0) ||
                packet_dataLen > 16) &&
            ppTcpHdr->DstPort != htons(80) &&
            (consts.do_fake_packet || consts.do_native_frag)
            )
        {
            /**
             * In case of Window Size fragmentation=2, we'll receive only 2 byte packet.
             * But if the packet is more than 2 bytes, check ClientHello byte.
            */
            debug("OUTBOUND && 1\n");
            if ((packet_dataLen == 2 && memcmp(packet_data, "\x16\x03", 2) == 0) ||
                (packet_dataLen >= 3 && memcmp(packet_data, "\x16\x03\x01", 3) == 0))
            {
                debug("(packet_dataLen == 2 && memcmp(packet_data, x16x03, 2) == 0) ||\n (packet_dataLen >= 3 && memcmp(packet_data, x16x03x01, 3) == 0)\n");
                if (consts.do_blacklist) {
                    debug("do_blacklist\n");
                    sni_ok = extract_sni(packet_data, packet_dataLen,
                        &host_addr, &host_len);
                }
                if (
                    (consts.do_blacklist && sni_ok &&
                        blackwhitelist_check_hostname(host_addr, host_len)
                        ) ||
                    (consts.do_blacklist && !sni_ok && consts.do_allow_no_sni) ||
                    (!consts.do_blacklist)
                    )
                {
#ifdef DEBUG
                    char lsni[HOST_MAXLEN + 1] = { 0 };
                    extract_sni(packet_data, packet_dataLen,
                        &host_addr, &host_len);
                    memcpy(lsni, host_addr, host_len);
                    printf("Blocked HTTPS website SNI: %s\n", lsni);
#endif
                    debug("do_blacklist && sni_ok && blackwhitelist_check_hostname\n");
                    if (consts.do_fake_packet) {
                        debug("do_fake_packet\n");
                        TCP_HANDLE_OUTGOING_FAKE_PACKET(send_fake_https_request);
                    }
                    if (consts.do_native_frag) {
                        // Signal for native fragmentation code handler
                        debug("do_native_frag\n");
                        should_recalc_checksum = 1;
                    }
                }
            }
        }
        /* Handle OUTBOUND packet on port 80, search for Host header */
        else if (addr.Outbound &&
            packet_dataLen > 16 &&
            (consts.do_http_allports ? 1 : (ppTcpHdr->DstPort == htons(80))) &&
            find_http_method_end(packet_data,
                (consts.do_fragment_http ? consts.http_fragment_size : 0u),
                &http_req_fragmented) &&
            (consts.do_host || consts.do_host_removespace ||
                consts.do_host_mixedcase || consts.do_fragment_http_persistent ||
                consts.do_fake_packet))
        {
            debug("OUTBOUND packet on port 80\n");
            /* Find Host header */
            if (find_header_and_get_info(packet_data, packet_dataLen,
                http_host_find, &hdr_name_addr, &hdr_value_addr, &hdr_value_len) &&
                hdr_value_len > 0 && hdr_value_len <= HOST_MAXLEN &&
                (consts.do_blacklist ? blackwhitelist_check_hostname(hdr_value_addr, hdr_value_len) : 1))
            {
                debug("Find Host header\n");
                host_addr = hdr_value_addr;
                host_len = hdr_value_len;
#ifdef DEBUG
                char lhost[HOST_MAXLEN + 1] = { 0 };
                memcpy(lhost, host_addr, host_len);
                printf("Blocked HTTP website Host: %s\n", lhost);
#endif

                if (consts.do_native_frag) {
                    // Signal for native fragmentation code handler
                    debug("do_native_frag\n");
                    should_recalc_checksum = 1;
                }

                if (consts.do_fake_packet) {
                    debug("do_fake_packet\n");
                    TCP_HANDLE_OUTGOING_FAKE_PACKET(send_fake_http_request);
                }

                if (consts.do_host_mixedcase) {
                    debug("do_host_mixedcase\n");
                    mix_case(host_addr, host_len);
                    should_recalc_checksum = 1;
                }

                if (consts.do_host) {
                    debug("do_host\n");
                    /* Replace "Host: " with "hoSt: " */
                    memcpy(hdr_name_addr, http_host_replace, strlen(http_host_replace));
                    should_recalc_checksum = 1;
                    //printf("Replaced Host header!\n");
                }

                /* If removing space between host header and its value
                 * and adding additional space between Method and Request-URI */
                if (consts.do_additional_space && consts.do_host_removespace) {
                    /* End of "Host:" without trailing space */
                    debug("do_additional_space && do_host_removespace\n");
                    method_addr = find_http_method_end(packet_data,
                        (consts.do_fragment_http ? consts.http_fragment_size : 0),
                        NULL);

                    if (method_addr) {
                        debug("method_addr\n");
                        memmove(method_addr + 1, method_addr,
                            (size_t)(host_addr - method_addr - 1));
                        should_recalc_checksum = 1;
                    }
                }
                /* If just removing space between host header and its value */
                else if (consts.do_host_removespace) {
                    debug("do_host_removespace\n");
                    if (find_header_and_get_info(packet_data, packet_dataLen,
                        http_useragent_find, &hdr_name_addr,
                        &hdr_value_addr, &hdr_value_len))
                    {
                        debug("find_header_and_get_info\n");
                        useragent_addr = hdr_value_addr;
                        useragent_len = hdr_value_len;

                        /* We move Host header value by one byte to the left and then
                         * "insert" stolen space to the end of User-Agent value because
                         * some web servers are not tolerant to additional space in the
                         * end of Host header.
                         *
                         * Nothing is done if User-Agent header is missing.
                         */
                        if (useragent_addr && useragent_len > 0) {
                            /* useragent_addr is in the beginning of User-Agent value */
                            debug("useragent_addr && useragent_len > 0\n");
                            if (useragent_addr > host_addr) {
                                /* Move one byte to the LEFT from "Host:"
                                * to the end of User-Agent
                                */
                                debug("useragent_addr > host_addr\n");
                                memmove(host_addr - 1, host_addr,
                                    (size_t)(useragent_addr + useragent_len - host_addr));
                                host_addr -= 1;
                                /* Put space in the end of User-Agent header */
                                *(char*)((unsigned char*)useragent_addr + useragent_len - 1) = ' ';
                                should_recalc_checksum = 1;
                                //printf("Replaced Host header!\n");
                            }
                            else {
                                /* User-Agent goes BEFORE Host header */

                                /* Move one byte to the RIGHT from the end of User-Agent
                                * to the "Host:"
                                */
                                debug("not useragent_addr > host_addr\n");
                                memmove(useragent_addr + useragent_len + 1,
                                    useragent_addr + useragent_len,
                                    (size_t)(host_addr - 1 - (useragent_addr + useragent_len)));
                                /* Put space in the end of User-Agent header */
                                *(char*)((unsigned char*)useragent_addr + useragent_len) = ' ';
                                should_recalc_checksum = 1;
                                //printf("Replaced Host header!\n");
                            }
                        } /* if (host_len <= HOST_MAXLEN && useragent_addr) */
                    } /* if (find_header_and_get_info http_useragent) */
                } /* else if (do_host_removespace) */
            } /* if (find_header_and_get_info http_host) */
        } /* Handle OUTBOUND packet with data */

        /*
        * should_recalc_checksum mean we have detected a packet to handle and
        * modified it in some way.
        * Handle native fragmentation here, incl. sending the packet.
        */
        if (should_reinject && should_recalc_checksum && consts.do_native_frag)
        {
            debug("should_reinject && should_recalc_checksum && do_native_frag\n");
            unsigned int current_fragment_size = 0;
            if (consts.do_fragment_http && ppTcpHdr->DstPort == htons(80)) {
                current_fragment_size = consts.http_fragment_size;
                debug("do_fragment_http && ppTcpHdr->DstPort == htons(80)\n");
            }
            else if (consts.do_fragment_https && ppTcpHdr->DstPort != htons(80)) {
                debug("do_fragment_https && ppTcpHdr->DstPort != htons(80)\n");
                current_fragment_size = consts.https_fragment_size;
            }

            if (current_fragment_size) {
                debug("current_fragment_size\n");
                send_native_fragment(w_filter, addr, packet, packetLen, packet_data,
                    packet_dataLen, packet_v4, packet_v6,
                    ppIpHdr, ppIpV6Hdr, ppTcpHdr,
                    current_fragment_size, consts.do_reverse_frag);

                send_native_fragment(w_filter, addr, packet, packetLen, packet_data,
                    packet_dataLen, packet_v4, packet_v6,
                    ppIpHdr, ppIpV6Hdr, ppTcpHdr,
                    current_fragment_size, !consts.do_reverse_frag);
            }
        }
    } /* Handle TCP packet with data */

    /* Else if we got TCP packet without data */
    else if (packet_type == ipv4_tcp || packet_type == ipv6_tcp) {
        debug("TCP packet without data\n");
        /* If we got INBOUND SYN+ACK packet */
        if (!addr.Outbound &&
            ppTcpHdr->Syn == 1 && ppTcpHdr->Ack == 1) {
            debug("INBOUND SYN+ACK packet\n");
            //printf("Changing Window Size!\n");
            /*
             * Window Size is changed even if do_fragment_http_persistent
             * is enabled as there could be non-HTTP data on port 80
             */

            if (consts.do_fake_packet && (consts.do_auto_ttl || consts.ttl_min_nhops)) {
                debug("do_fake_packet\n");
                if (!((packet_v4 && tcp_handle_incoming(&ppIpHdr->SrcAddr, &ppIpHdr->DstAddr,
                    ppTcpHdr->SrcPort, ppTcpHdr->DstPort,
                    0, ppIpHdr->TTL))
                    ||
                    (packet_v6 && tcp_handle_incoming((uint32_t*)&ppIpV6Hdr->SrcAddr,
                        (uint32_t*)&ppIpV6Hdr->DstAddr,
                        ppTcpHdr->SrcPort, ppTcpHdr->DstPort,
                        1, ppIpV6Hdr->HopLimit))))
                {
                    debug("tcp_handle_incoming\n");
                    if (consts.do_tcp_verb)
                        puts("[TCP WARN] Can't add TCP connection record.");
                }
            }

            if (!consts.do_native_frag) {
                debug("not do_native_frag\n");
                if (consts.do_fragment_http && ppTcpHdr->SrcPort == htons(80)) {
                    debug("do_fragment_http\n");
                    change_window_size(ppTcpHdr, consts.http_fragment_size);
                    should_recalc_checksum = 1;
                }
                else if (consts.do_fragment_https && ppTcpHdr->SrcPort != htons(80)) {
                    debug("do_fragment_https\n");
                    change_window_size(ppTcpHdr, consts.https_fragment_size);
                    should_recalc_checksum = 1;
                }
            }
        }
    }

    /* Else if we got UDP packet with data */
    else if ((consts.do_dnsv4_redirect && (packet_type == ipv4_udp_data)) ||
        (consts.do_dnsv6_redirect && (packet_type == ipv6_udp_data)))
    {
        debug("UDP packet with data\n");
        if (!addr.Outbound) {
            debug("INBOUND\n");
            if ((packet_v4 && dns_handle_incoming(&ppIpHdr->DstAddr, ppUdpHdr->DstPort,
                packet_data, packet_dataLen,
                &dns_conn_info, 0))
                ||
                (packet_v6 && dns_handle_incoming(ppIpV6Hdr->DstAddr, ppUdpHdr->DstPort,
                    packet_data, packet_dataLen,
                    &dns_conn_info, 1)))
            {
                debug("dns_handle_incoming\n");
                /* Changing source IP and port to the values
                 * from DNS conntrack */
                if (packet_v4)
                    ppIpHdr->SrcAddr = dns_conn_info.dstip[0];
                else if (packet_v6)
                    ipv6_copy_addr(ppIpV6Hdr->SrcAddr, dns_conn_info.dstip);
                ppUdpHdr->DstPort = dns_conn_info.srcport;
                ppUdpHdr->SrcPort = dns_conn_info.dstport;
                should_recalc_checksum = 1;
            }
            else {
                debug("not dns_handle_incoming\n");
                if (dns_is_dns_packet(packet_data, packet_dataLen, 0)) {
                    debug("dns_is_dns_packet\n");
                    should_reinject = 0;
                }

                if (consts.do_dns_verb && !should_reinject) {
                    printf("[DNS] Error handling incoming packet: srcport = %hu, dstport = %hu\n",
                        ntohs(ppUdpHdr->SrcPort), ntohs(ppUdpHdr->DstPort));
                }
            }
        }

        else if (addr.Outbound) {
            debug("INBOUND\n");
            if ((packet_v4 && dns_handle_outgoing(&ppIpHdr->SrcAddr, ppUdpHdr->SrcPort,
                &ppIpHdr->DstAddr, ppUdpHdr->DstPort,
                packet_data, packet_dataLen, 0))
                ||
                (packet_v6 && dns_handle_outgoing(ppIpV6Hdr->SrcAddr, ppUdpHdr->SrcPort,
                    ppIpV6Hdr->DstAddr, ppUdpHdr->DstPort,
                    packet_data, packet_dataLen, 1)))
            {
                debug("dns_handle_outgoing\n");
                /* Changing destination IP and port to the values
                 * from configuration */
                if (packet_v4) {
                    debug("packet_v4\n");
                    ppIpHdr->DstAddr = consts.dnsv4_addr;
                    ppUdpHdr->DstPort = consts.dnsv4_port;
                }
                else if (packet_v6) {
                    debug("packet_v6\n");
                    ipv6_copy_addr(ppIpV6Hdr->DstAddr, (uint32_t*)consts.dnsv6_addr.s6_addr);
                    ppUdpHdr->DstPort = consts.dnsv6_port;
                }
                should_recalc_checksum = 1;
            }
            else {
                debug("not dns_handle_outgoing\n");
                if (dns_is_dns_packet(packet_data, packet_dataLen, 1)) {
                    debug("dns_is_dns_packet\n");
                    should_reinject = 0;
                }

                if (consts.do_dns_verb && !should_reinject) {
                    printf("[DNS] Error handling outgoing packet: srcport = %hu, dstport = %hu\n",
                        ntohs(ppUdpHdr->SrcPort), ntohs(ppUdpHdr->DstPort));
                }
            }
        }
    }

    if (should_reinject) {
        debug("Re-injecting!\n");
        //printf("Re-injecting!\n");
        if (should_recalc_checksum) {
            debug("CalcCheck");
            WinDivertHelperCalcChecksums(packet, packetLen, &addr, (UINT64)0LL);
        }
        WinDivertSend(w_filter, packet, packetLen, NULL, &addr);
    }
}

typedef struct ThreadFunctionArguments {
    HANDLE filter;
    IntConsts consts;
} ARGS, * PARGS;

int AddOrFindNewPacket(char* packet, UINT packetLen, WINDIVERT_ADDRESS addr, int isForward)
{
    PWINDIVERT_IPHDR ppIpHdr = (PWINDIVERT_IPHDR)NULL;
    WinDivertHelperParsePacket(packet, packetLen, &ppIpHdr, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    // handling ip
    if (IsOutbound(isForward, ppIpHdr->SrcAddr, addr))
    {
        return Insert(ppIpHdr->DstAddr);
    }
    else
    {
        size_t index = Find(ppIpHdr->SrcAddr);
        if (index != GlobalIpList.m_ipLimit)
        {
            for (Erase(index);
                index != GlobalIpList.m_ipLimit;
                Erase(index))
                index = Find(ppIpHdr->SrcAddr);
        }
        else
        {
            char srcAddr[INET_ADDRSTRLEN];
            if (!inet_ntop(AF_INET, &(ppIpHdr->SrcAddr), srcAddr, sizeof(srcAddr)))
                debug("Didn't return char src");
            debug("Packet with unknown SrcIp: %s\n", srcAddr);
        }
        return FALSE;
    }
}

void WinDivertRecvHandling(int isForward, LPVOID args)
{
    WINDIVERT_ADDRESS addr;
    char packet[MAX_PACKET_SIZE];
    UINT packetLen;
    PARGS data = (PARGS)args;
    debug(isForward ? "Forward\n" : "Straight\n");
    while (TRUE) {
        if (WinDivertRecv(data->filter, packet, sizeof(packet), &packetLen, &addr))
        {
            AnalyzePacket(data->filter, packet, packetLen, addr, data->consts, 0);
        }
        else
        {
            // error, ignore
            if (!exiting)
                printf("Error receiving packet!\nError: %d", GetLastError());
            break;
        }
    }
}

DWORD WinDivertRecvStraight(LPVOID args)
{
    WinDivertRecvHandling(FALSE, args);
    return 1;
}

DWORD WinDivertRecvForward(LPVOID args)
{
    WinDivertRecvHandling(TRUE, args);
    return 1;
}

void WinDivertRecvAll(LPVOID args, int isForward)
{
    WINDIVERT_ADDRESS addr;
    char packet[MAX_PACKET_SIZE];
    UINT packetLen;
    PHANDLE pAllPacketFilter = (PHANDLE)args;
    while (TRUE)
    {
        int packetIsNotFound = FALSE;
        if (WinDivertRecv(*pAllPacketFilter, packet, sizeof(packet), &packetLen, &addr))
        {
            packetIsNotFound = AddOrFindNewPacket(packet, packetLen, addr, isForward);
            WinDivertSend(*pAllPacketFilter, packet, packetLen, NULL, &addr);

            debug("============================================================\n");
            debug(isForward ? "RecvForward:\n" : "RecvStraight:\n");
            debug("packetIsNotFound = %d\n", packetIsNotFound);
            DebugPrintList();
            debug("============================================================\n");

            if (packetIsNotFound)
            {
                /*DWORD waitReadFlag = WaitForSingleObject(receivingPacketMutex, INFINITE);
                if (packetIsNotFound || GlobalPacketIsNotFound)
                {
                    GlobalPacketIsNotFound = TRUE;
                    ReleaseMutex(receivingPacketMutex);
                    break;
                }
                ReleaseMutex(receivingPacketMutex);*/
                return;
            }
        }
    }
}

DWORD WinDivertRecvAllStraight(LPVOID args)
{
    WinDivertRecvAll(args, FALSE);
    return 1;
}

DWORD WinDivertRecvAllForward(LPVOID args)
{
    WinDivertRecvAll(args, TRUE);
    return 1;
}

int firstLoad = TRUE;

void TurnOnOrReloadGDPI(const int argc, char* const argv[], char* actualMode)
{
    int i;
    int opt;
    HANDLE w_filter = NULL, w_forward_filter = NULL, w_all_packet_filter = NULL, w_all_packet_forward_filter = NULL;

    int do_passivedpi = 0, do_fragment_http = 0,
        do_fragment_http_persistent = 0,
        do_fragment_http_persistent_nowait = 0,
        do_fragment_https = 0, do_host = 0,
        do_host_removespace = 0, do_additional_space = 0,
        do_http_allports = 0,
        do_host_mixedcase = 0,
        do_dnsv4_redirect = 0, do_dnsv6_redirect = 0,
        do_dns_verb = 0, do_tcp_verb = 0, do_blacklist = 0,
        do_allow_no_sni = 0,
        do_fake_packet = 0,
        do_auto_ttl = 0,
        do_wrong_chksum = 0,
        do_wrong_seq = 0,
        do_native_frag = 0, do_reverse_frag = 0;
    unsigned int http_fragment_size = 0;
    unsigned int https_fragment_size = 0;
    unsigned short max_payload_size = 0;
    BYTE should_send_fake = 0;
    BYTE ttl_of_fake_packet = 0;
    BYTE ttl_min_nhops = 0;
    BYTE auto_ttl_1 = 0;
    BYTE auto_ttl_2 = 0;
    BYTE auto_ttl_max = 0;
    uint32_t dnsv4_addr = 0;
    struct in6_addr dnsv6_addr = { 0 };
    struct in6_addr dns_temp_addr = { 0 };
    uint16_t dnsv4_port = htons(53);
    uint16_t dnsv6_port = htons(53);

    if (argc == 1) {
        /* enable mode -5 by default */
        *actualMode = '5';
        do_fragment_http = do_fragment_https = 1;
        do_reverse_frag = do_native_frag = 1;
        http_fragment_size = https_fragment_size = 2;
        do_fragment_http_persistent = do_fragment_http_persistent_nowait = 1;
        do_fake_packet = 1;
        do_auto_ttl = 1;
        max_payload_size = 1200;
    }

    while ((opt = getopt_long(argc, argv, "123456prsaf:e:mwk:n", long_options, NULL)) != -1) {
        switch (opt) {
        case '1':
            do_passivedpi = do_host = do_host_removespace \
                = do_fragment_http = do_fragment_https \
                = do_fragment_http_persistent \
                = do_fragment_http_persistent_nowait = 1;
            break;
        case '2':
            do_passivedpi = do_host = do_host_removespace \
                = do_fragment_http = do_fragment_https \
                = do_fragment_http_persistent \
                = do_fragment_http_persistent_nowait = 1;
            https_fragment_size = 40u;
            break;
        case '3':
            do_passivedpi = do_host = do_host_removespace \
                = do_fragment_https = 1;
            https_fragment_size = 40u;
            break;
        case '4':
            do_passivedpi = do_host = do_host_removespace = 1;
            break;
        case '5':
            do_fragment_http = do_fragment_https = 1;
            do_reverse_frag = do_native_frag = 1;
            http_fragment_size = https_fragment_size = 2;
            do_fragment_http_persistent = do_fragment_http_persistent_nowait = 1;
            do_fake_packet = 1;
            do_auto_ttl = 1;
            max_payload_size = 1200;
            break;
        case '6':
            do_fragment_http = do_fragment_https = 1;
            do_reverse_frag = do_native_frag = 1;
            http_fragment_size = https_fragment_size = 2;
            do_fragment_http_persistent = do_fragment_http_persistent_nowait = 1;
            do_fake_packet = 1;
            do_wrong_seq = 1;
            max_payload_size = 1200;
            break;
        case 'p':
            do_passivedpi = 1;
            break;
        case 'r':
            do_host = 1;
            break;
        case 's':
            do_host_removespace = 1;
            break;
        case 'a':
            do_additional_space = 1;
            do_host_removespace = 1;
            break;
        case 'm':
            do_host_mixedcase = 1;
            break;
        case 'f':
            do_fragment_http = 1;
            SET_HTTP_FRAGMENT_SIZE_OPTION(atousi(optarg, "Fragment size should be in range [0 - 0xFFFF]\n"));
            break;
        case 'k':
            do_fragment_http_persistent = 1;
            do_native_frag = 1;
            SET_HTTP_FRAGMENT_SIZE_OPTION(atousi(optarg, "Fragment size should be in range [0 - 0xFFFF]\n"));
            break;
        case 'n':
            do_fragment_http_persistent = 1;
            do_fragment_http_persistent_nowait = 1;
            do_native_frag = 1;
            break;
        case 'e':
            do_fragment_https = 1;
            https_fragment_size = atousi(optarg, "Fragment size should be in range [0 - 65535]\n");
            break;
        case 'w':
            do_http_allports = 1;
            break;
        case 'z': // --port
            /* i is used as a temporary variable here */
            i = atoi(optarg);
            if (i <= 0 || i > 65535) {
                printf("Port parameter error!\n");
                exit(EXIT_FAILURE);
            }
            if (i != 80 && i != 443)
                add_filter_str(IPPROTO_TCP, i);
            i = 0;
            break;
        case 'i': // --ip-id
            /* i is used as a temporary variable here */
            i = atousi(optarg, "IP ID parameter error!\n");
            add_ip_id_str(i);
            i = 0;
            break;
        case 'd': // --dns-addr
            if ((inet_pton(AF_INET, optarg, dns_temp_addr.s6_addr) == 1) &&
                !do_dnsv4_redirect)
            {
                do_dnsv4_redirect = 1;
                if (inet_pton(AF_INET, optarg, &dnsv4_addr) != 1) {
                    puts("DNS address parameter error!");
                    exit(EXIT_FAILURE);
                }
                add_filter_str(IPPROTO_UDP, 53);
                flush_dns_cache();
                break;
            }
            puts("DNS address parameter error!");
            exit(EXIT_FAILURE);
            break;
        case '!': // --dnsv6-addr
            if ((inet_pton(AF_INET6, optarg, dns_temp_addr.s6_addr) == 1) &&
                !do_dnsv6_redirect)
            {
                do_dnsv6_redirect = 1;
                if (inet_pton(AF_INET6, optarg, dnsv6_addr.s6_addr) != 1) {
                    puts("DNS address parameter error!");
                    exit(EXIT_FAILURE);
                }
                add_filter_str(IPPROTO_UDP, 53);
                flush_dns_cache();
                break;
            }
            puts("DNS address parameter error!");
            exit(EXIT_FAILURE);
            break;
        case 'g': // --dns-port
            if (!do_dnsv4_redirect) {
                puts("--dns-port should be used with --dns-addr!\n"
                    "Make sure you use --dns-addr and pass it before "
                    "--dns-port");
                exit(EXIT_FAILURE);
            }
            dnsv4_port = atousi(optarg, "DNS port parameter error!");
            if (dnsv4_port != 53) {
                add_filter_str(IPPROTO_UDP, dnsv4_port);
            }
            dnsv4_port = htons(dnsv4_port);
            break;
        case '@': // --dnsv6-port
            if (!do_dnsv6_redirect) {
                puts("--dnsv6-port should be used with --dnsv6-addr!\n"
                    "Make sure you use --dnsv6-addr and pass it before "
                    "--dnsv6-port");
                exit(EXIT_FAILURE);
            }
            dnsv6_port = atousi(optarg, "DNS port parameter error!");
            if (dnsv6_port != 53) {
                add_filter_str(IPPROTO_UDP, dnsv6_port);
            }
            dnsv6_port = htons(dnsv6_port);
            break;
        case 'v':
            do_dns_verb = 1;
            do_tcp_verb = 1;
            break;
        case 'b': // --blacklist
            do_blacklist = 1;
            if (!blackwhitelist_load_list(optarg)) {
                printf("Can't load blacklist from file!\n");
                exit(EXIT_FAILURE);
            }
            break;
        case ']': // --allow-no-sni
            do_allow_no_sni = 1;
            break;
        case '$': // --set-ttl
            do_auto_ttl = auto_ttl_1 = auto_ttl_2 = auto_ttl_max = 0;
            do_fake_packet = 1;
            ttl_of_fake_packet = atoub(optarg, "Set TTL parameter error!");
            break;
        case '[': // --min-ttl
            do_fake_packet = 1;
            ttl_min_nhops = atoub(optarg, "Set Minimum TTL number of hops parameter error!");
            break;
        case '+': // --auto-ttl
            do_fake_packet = 1;
            do_auto_ttl = 1;

            if (!optarg && argv[optind] && argv[optind][0] != '-')
                optarg = argv[optind];

            if (optarg) {
                char* autottl_copy = strdup(optarg);
                if (strchr(autottl_copy, '-')) {
                    // token "-" found, start X-Y parser
                    char* autottl_current = strtok(autottl_copy, "-");
                    auto_ttl_1 = atoub(autottl_current, "Set Auto TTL parameter error!");
                    autottl_current = strtok(NULL, "-");
                    if (!autottl_current) {
                        puts("Set Auto TTL parameter error!");
                        exit(EXIT_FAILURE);
                    }
                    auto_ttl_2 = atoub(autottl_current, "Set Auto TTL parameter error!");
                    autottl_current = strtok(NULL, "-");
                    if (!autottl_current) {
                        puts("Set Auto TTL parameter error!");
                        exit(EXIT_FAILURE);
                    }
                    auto_ttl_max = atoub(autottl_current, "Set Auto TTL parameter error!");
                }
                else {
                    // single digit parser
                    auto_ttl_2 = atoub(optarg, "Set Auto TTL parameter error!");
                    auto_ttl_1 = auto_ttl_2;
                }
                free(autottl_copy);
            }
            break;
        case '%': // --wrong-chksum
            do_fake_packet = 1;
            do_wrong_chksum = 1;
            break;
        case ')': // --wrong-seq
            do_fake_packet = 1;
            do_wrong_seq = 1;
            break;
        case '*': // --native-frag
            do_native_frag = 1;
            do_fragment_http_persistent = 1;
            do_fragment_http_persistent_nowait = 1;
            break;
        case '(': // --reverse-frag
            do_reverse_frag = 1;
            do_native_frag = 1;
            do_fragment_http_persistent = 1;
            do_fragment_http_persistent_nowait = 1;
            break;
        case '|': // --max-payload
            if (!optarg && argv[optind] && argv[optind][0] != '-')
                optarg = argv[optind];
            if (optarg)
                max_payload_size = atousi(optarg, "Max payload size parameter error!");
            else
                max_payload_size = 1200;
            break;
        default:
            puts("Usage: goodbyedpi.exe [OPTION...]\n"
                " -p          block passive DPI\n"
                " -r          replace Host with hoSt\n"
                " -s          remove space between host header and its value\n"
                " -a          additional space between Method and Request-URI (enables -s, may break sites)\n"
                " -m          mix Host header case (test.com -> tEsT.cOm)\n"
                " -f <value>  set HTTP fragmentation to value\n"
                " -k <value>  enable HTTP persistent (keep-alive) fragmentation and set it to value\n"
                " -n          do not wait for first segment ACK when -k is enabled\n"
                " -e <value>  set HTTPS fragmentation to value\n"
                " -w          try to find and parse HTTP traffic on all processed ports (not only on port 80)\n"
                " --port        <value>    additional TCP port to perform fragmentation on (and HTTP tricks with -w)\n"
                " --ip-id       <value>    handle additional IP ID (decimal, drop redirects and TCP RSTs with this ID).\n"
                " --dns-addr    <value>    redirect UDPv4 DNS requests to the supplied IPv4 address (experimental)\n"
                " --dns-port    <value>    redirect UDPv4 DNS requests to the supplied port (53 by default)\n"
                " --dnsv6-addr  <value>    redirect UDPv6 DNS requests to the supplied IPv6 address (experimental)\n"
                " --dnsv6-port  <value>    redirect UDPv6 DNS requests to the supplied port (53 by default)\n"
                " --dns-verb               print verbose DNS redirection messages\n"
                " --blacklist   <txtfile>  perform circumvention tricks only to host names and subdomains from\n"
                "                          supplied text file (HTTP Host/TLS SNI).\n"
                "                          This option can be supplied multiple times.\n"
                " --allow-no-sni           perform circumvention if TLS SNI can't be detected with --blacklist enabled.\n"
                " --set-ttl     <value>    activate Fake Request Mode and send it with supplied TTL value.\n"
                "                          DANGEROUS! May break websites in unexpected ways. Use with care (or --blacklist).\n"
                " --auto-ttl    [a1-a2-m]  activate Fake Request Mode, automatically detect TTL and decrease\n"
                "                          it based on a distance. If the distance is shorter than a2, TTL is decreased\n"
                "                          by a2. If it's longer, (a1; a2) scale is used with the distance as a weight.\n"
                "                          If the resulting TTL is more than m(ax), set it to m.\n"
                "                          Default (if set): --auto-ttl 1-4-10. Also sets --min-ttl 3.\n"
                "                          DANGEROUS! May break websites in unexpected ways. Use with care (or --blacklist).\n"
                " --min-ttl     <value>    minimum TTL distance (128/64 - TTL) for which to send Fake Request\n"
                "                          in --set-ttl and --auto-ttl modes.\n"
                " --wrong-chksum           activate Fake Request Mode and send it with incorrect TCP checksum.\n"
                "                          May not work in a VM or with some routers, but is safer than set-ttl.\n"
                "                          Could be combined with --set-ttl\n"
                " --wrong-seq              activate Fake Request Mode and send it with TCP SEQ/ACK in the past.\n"
                " --native-frag            fragment (split) the packets by sending them in smaller packets, without\n"
                "                          shrinking the Window Size. Works faster (does not slow down the connection)\n"
                "                          and better.\n"
                " --reverse-frag           fragment (split) the packets just as --native-frag, but send them in the\n"
                "                          reversed order. Works with the websites which could not handle segmented\n"
                "                          HTTPS TLS ClientHello (because they receive the TCP flow \"combined\").\n"
                " --max-payload [value]    packets with TCP payload data more than [value] won't be processed.\n"
                "                          Use this option to reduce CPU usage by skipping huge amount of data\n"
                "                          (like file transfers) in already established sessions.\n"
                "                          May skip some huge HTTP requests from being processed.\n"
                "                          Default (if set): --max-payload 1200.\n"
                "\n");
            puts("LEGACY modesets:\n"
                " -1          -p -r -s -f 2 -k 2 -n -e 2 (most compatible mode)\n"
                " -2          -p -r -s -f 2 -k 2 -n -e 40 (better speed for HTTPS yet still compatible)\n"
                " -3          -p -r -s -e 40 (better speed for HTTP and HTTPS)\n"
                " -4          -p -r -s (best speed)"
                "\n"
                "Modern modesets (more stable, more compatible, faster):\n"
                " -5          -f 2 -e 2 --auto-ttl --reverse-frag --max-payload (this is the default)\n"
                " -6          -f 2 -e 2 --wrong-seq --reverse-frag --max-payload\n");
            exit(EXIT_FAILURE);
        }

        *actualMode = opt;
    }

    if (!http_fragment_size)
        http_fragment_size = 2;
    if (!https_fragment_size)
        https_fragment_size = 2;
    if (!auto_ttl_1)
        auto_ttl_1 = 1;
    if (!auto_ttl_2)
        auto_ttl_2 = 4;
    if (do_auto_ttl) {
        if (!ttl_min_nhops)
            ttl_min_nhops = 3;
        if (!auto_ttl_max)
            auto_ttl_max = 10;
    }

    printf("Block passive: %d\n"                    /* 1 */
        "Fragment HTTP: %u\n"                    /* 2 */
        "Fragment persistent HTTP: %u\n"         /* 3 */
        "Fragment HTTPS: %u\n"                   /* 4 */
        "Native fragmentation (splitting): %d\n" /* 5 */
        "Fragments sending in reverse: %d\n"     /* 6 */
        "hoSt: %d\n"                             /* 7 */
        "Host no space: %d\n"                    /* 8 */
        "Additional space: %d\n"                 /* 9 */
        "Mix Host: %d\n"                         /* 10 */
        "HTTP AllPorts: %d\n"                    /* 11 */
        "HTTP Persistent Nowait: %d\n"           /* 12 */
        "DNS redirect: %d\n"                     /* 13 */
        "DNSv6 redirect: %d\n"                   /* 14 */
        "Allow missing SNI: %d\n"                /* 15 */
        "Fake requests, TTL: %s (fixed: %hu, auto: %hu-%hu-%hu, min distance: %hu)\n"  /* 16 */
        "Fake requests, wrong checksum: %d\n"    /* 17 */
        "Fake requests, wrong SEQ/ACK: %d\n"     /* 18 */
        "Max payload size: %hu\n",               /* 19 */
        do_passivedpi,                                         /* 1 */
        (do_fragment_http ? http_fragment_size : 0),           /* 2 */
        (do_fragment_http_persistent ? http_fragment_size : 0),/* 3 */
        (do_fragment_https ? https_fragment_size : 0),         /* 4 */
        do_native_frag,        /* 5 */
        do_reverse_frag,       /* 6 */
        do_host,               /* 7 */
        do_host_removespace,   /* 8 */
        do_additional_space,   /* 9 */
        do_host_mixedcase,     /* 10 */
        do_http_allports,      /* 11 */
        do_fragment_http_persistent_nowait, /* 12 */
        do_dnsv4_redirect,                  /* 13 */
        do_dnsv6_redirect,                  /* 14 */
        do_allow_no_sni,                    /* 15 */
        do_auto_ttl ? "auto" : (do_fake_packet ? "fixed" : "disabled"),  /* 16 */
        ttl_of_fake_packet, do_auto_ttl ? auto_ttl_1 : 0, do_auto_ttl ? auto_ttl_2 : 0,
        do_auto_ttl ? auto_ttl_max : 0, ttl_min_nhops,
        do_wrong_chksum, /* 17 */
        do_wrong_seq,    /* 18 */
        max_payload_size /* 19 */
    );

    if (do_fragment_http && http_fragment_size > 2 && !do_native_frag) {
        puts("\nWARNING: HTTP fragmentation values > 2 are not fully compatible "
            "with other options. Please use values <= 2 or disable HTTP fragmentation "
            "completely.");
    }

    if (do_native_frag && !(do_fragment_http || do_fragment_https)) {
        puts("\nERROR: Native fragmentation is enabled but fragment sizes are not set.\n"
            "Fragmentation has no effect.");
        die();
    }

    if (max_payload_size)
        add_maxpayloadsize_str(max_payload_size);
    finalize_filter_strings();
    puts("\nOpening filter");
    filter_num = 0;

    if (do_passivedpi) {
        /* IPv4 only filter for inbound RST packets with ID [0x0; 0xF] */
        filters[filter_num] = init(
            filter_passive_string,
            WINDIVERT_FLAG_DROP,
            FALSE, WINDIVERT_PRIORITY_HIGHEST);
        if (filters[filter_num] == NULL)
            die();
        ++filter_num;

        filters[filter_num] = init(
            filter_passive_forward_string,
            WINDIVERT_FLAG_DROP,
            TRUE, WINDIVERT_PRIORITY_HIGHEST);
        if (filters[filter_num] == NULL)
            die();
        ++filter_num;
    }

    /*
     * IPv4 & IPv6 filter for inbound HTTP redirection packets and
     * active DPI circumvention
     */

    if (*actualMode != '6')
    {
        filters[filter_num] = init(all_packets_filter_string, 0, FALSE, WINDIVERT_PRIORITY_LOWEST);

        w_all_packet_filter = filters[filter_num];
        ++filter_num;

        filters[filter_num] = init(all_packets_filter_string, 0, TRUE, WINDIVERT_PRIORITY_LOWEST);

        w_all_packet_forward_filter = filters[filter_num];
        ++filter_num;
    }

    filters[filter_num] = init(filter_string, 0, FALSE, 0);

    w_filter = filters[filter_num];
    ++filter_num;

    filters[filter_num] = init(filter_forward_string, 0, TRUE, 0);

    w_forward_filter = filters[filter_num];
    ++filter_num;

    for (i = 0; i < filter_num; ++i) {
        if (filters[i] == NULL) {
            printf("Dying on %d", i);
            die();
        }
    }
    printf("Filter activated, GoodbyeDPI is now running!\n");
    signal(SIGINT, sigint_handler);
    IntConsts consts = { do_fragment_https, do_passivedpi, https_fragment_size, do_fake_packet, do_native_frag,
        do_blacklist, do_allow_no_sni, do_http_allports, do_fragment_http, http_fragment_size, do_host,
        do_host_removespace, do_host_mixedcase, do_fragment_http_persistent, do_additional_space,
        do_auto_ttl, ttl_min_nhops, do_tcp_verb, do_dnsv4_redirect, do_dnsv6_redirect, do_dns_verb,
        dnsv4_addr, dnsv4_port, dnsv6_addr, dnsv6_port, should_send_fake, ttl_of_fake_packet,
        auto_ttl_1, auto_ttl_2, auto_ttl_max, do_wrong_chksum, do_wrong_seq, do_reverse_frag
    };

    PARGS pStraightArguments = (PARGS)malloc(sizeof(ARGS)), pForwardArguments = (PARGS)malloc(sizeof(ARGS));
    pStraightArguments->consts = consts;
    pStraightArguments->filter = w_filter;
    pForwardArguments->consts = consts;
    pForwardArguments->filter = w_forward_filter;

    DWORD dwStraightThreadId, dwForwardThreadId, dwAllStraightThreadId, dwAllForwardThreadId;
    size_t filterNumWithoutPassive = do_passivedpi ? filter_num - 2 : filter_num;
    PHANDLE hThreadArray = (PHANDLE)malloc(sizeof(HANDLE) * (filterNumWithoutPassive));

    hThreadArray[0] = CreateThread(NULL, 0, WinDivertRecvAllStraight, &w_all_packet_filter, 0, &dwAllStraightThreadId);
    hThreadArray[1] = CreateThread(NULL, 0, WinDivertRecvAllForward, &w_all_packet_forward_filter, 0, &dwAllForwardThreadId);
    hThreadArray[2] = CreateThread(NULL, 0, WinDivertRecvStraight, pStraightArguments, 0, &dwStraightThreadId);
    hThreadArray[3] = CreateThread(NULL, 0, WinDivertRecvForward, pForwardArguments, 0, &dwForwardThreadId);

    WaitForMultipleObjects(filterNumWithoutPassive, hThreadArray, FALSE, INFINITE);
    deinit_all();
    for(size_t i = 0; i < filterNumWithoutPassive; ++i)
        CloseHandle(hThreadArray[i]);
    debug("Closed handles\n");
    free(hThreadArray);
    free(pStraightArguments);
    free(pForwardArguments);
}

void ChangeModes(size_t* argumentSize, char*** actualArgv, char actualMode)
{
    debug("Changing\n");

    int newSize = ++(*argumentSize);
    *actualArgv = (char**)realloc(*actualArgv, sizeof(char*) * newSize);
    (*actualArgv)[newSize - 1] = (char*)malloc(sizeof(char) * 3);

    char modes[] = { '4', '3', '2', '1', '5', '6' };

    size_t i = 0;
    while (i < 5)
        if (modes[i++] == actualMode)
            break;

    char* actualArgvLocal = (*actualArgv)[newSize - 1];
    actualArgvLocal[0] = '-';
    actualArgvLocal[1] = modes[i];
    actualArgvLocal[2] = '\0';
    debug("Changing mode to %c\n", actualArgvLocal[1]);
}

int main(int argc, char *argv[])
{
    // Make sure to search DLLs only in safe path, not in current working dir.
    SetDllDirectory("");
    SetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE | BASE_SEARCH_PATH_PERMANENT);

    if (!running_from_service) {
        running_from_service = 1;
        if (service_register(argc, argv)) {
            /* We've been called as a service. Register service
             * and exit this thread. main() would be called from
             * service.c next time.
             *
             * Note that if service_register() succeedes it does
             * not return until the service is stopped.
             * That is why we should set running_from_service
             * before calling service_register and unset it
             * afterwards.
             */
            return 0;
        }
        running_from_service = 0;
    }

    if (filter_string == NULL)
        filter_string = strdup(FILTER_STRING_TEMPLATE);
    if (filter_passive_string == NULL)
        filter_passive_string = strdup(FILTER_PASSIVE_STRING_TEMPLATE);
    if (filter_forward_string == NULL)
        filter_forward_string = strdup(FORWARD_FILTER_STRING_TEMPLATE);
    if (filter_passive_forward_string == NULL)
        filter_passive_forward_string = strdup(FILTER_PASSIVE_FORWARD_STRING_TEMPLATE);
    if (all_packets_filter_string == NULL)
        all_packets_filter_string = strdup(ALL_PACKETS_FILTER_STRING_TEMPLATE);

    if (get_subnet())
        return -1;

    replace_template_and_clear_strings(&filter_forward_string, SUBNET_START_TEMPLATE, subnet_start);
    replace_template_and_clear_strings(&filter_forward_string, SUBNET_END_TEMPLATE, subnet_end);

    replace_template_and_clear_strings(&filter_passive_forward_string, SUBNET_START_TEMPLATE, subnet_start);
    replace_template_and_clear_strings(&filter_passive_forward_string, SUBNET_END_TEMPLATE, subnet_end);

    replace_template_and_clear_strings(&all_packets_filter_string, SUBNET_START_TEMPLATE, subnet_start);
    replace_template_and_clear_strings(&all_packets_filter_string, SUBNET_END_TEMPLATE, subnet_end);

    printf(
        "GoodbyeDPI " GOODBYEDPI_VERSION
        ": Passive DPI blocker and Active DPI circumvention utility\n"
        "https://github.com/ValdikSS/GoodbyeDPI\n\n"
    );

    char** actualArgv = (char**)malloc(sizeof(char*) * argc), actualMode;
    size_t argumentSize = argc;
    for (int i = 0; i < argc; ++i)
        actualArgv[i] = strdup(argv[i]);

#ifdef DEBUG
    TestList();
#endif // DEBUG

    do
    {
        TurnOnOrReloadGDPI(argumentSize, actualArgv, &actualMode);
        ChangeModes(&argumentSize, &actualArgv, actualMode);
        DebugPrintList();
        ClearList();
        debug("Cleared list\nargumentSize: %u, actualArgv[0]: %s, actualArgv[1]: %s, actualMode: %c\n", argumentSize, actualArgv[0], actualArgv[1], actualMode);
    }
    while (TRUE);
}
