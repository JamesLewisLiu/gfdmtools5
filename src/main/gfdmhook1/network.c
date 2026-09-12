#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <string.h>
#include <stdio.h>

#include "gfdmhook1/network.h"
#include "hook/table.h"
#include "util/defs.h"
#include "util/log.h"

static char gfdm_server_host[NET_MAX_URL_HOSTNAME_LEN + 1];
static struct net_addr_ipv4 gfdm_server_addr;
static int(STDCALL *real_getaddrinfo)(
    PCSTR, PCSTR, const ADDRINFOA *, PADDRINFOA *);
static struct hostent *(STDCALL *real_gethostbyname)(const char *);
static int(STDCALL *real_connect)(SOCKET, const struct sockaddr *, int);

static bool gfdm_is_eamuse_host(const char *host)
{
    return host != NULL &&
        (strstr(host, "eamuse") != NULL || strstr(host, "konami.") != NULL);
}

static void gfdm_copy_server_host(const char *host)
{
    size_t len = strlen(host);

    if (len >= sizeof(gfdm_server_host)) {
        len = sizeof(gfdm_server_host) - 1;
    }

    memcpy(gfdm_server_host, host, len);
    gfdm_server_host[len] = '\0';
}

static int STDCALL gfdm_getaddrinfo(
    PCSTR node,
    PCSTR service,
    const ADDRINFOA *hints,
    PADDRINFOA *result)
{
    const char *lookup = node;

    if (gfdm_is_eamuse_host(node)) {
        lookup = gfdm_server_host;
        log_info("Resolving %s through %s", node, lookup);
    }

    return real_getaddrinfo(lookup, service, hints, result);
}

static struct hostent *STDCALL gfdm_gethostbyname(const char *name)
{
    if (gfdm_is_eamuse_host(name)) {
        log_info("Resolving %s through %s", name, gfdm_server_host);
        return real_gethostbyname(gfdm_server_host);
    }

    return real_gethostbyname(name);
}

static int STDCALL gfdm_connect(
    SOCKET sock, const struct sockaddr *name, int namelen)
{
    struct sockaddr_in patched;

    if (name != NULL && name->sa_family == AF_INET &&
        gfdm_server_addr.addr != NET_INVALID_ADDR &&
        gfdm_server_addr.port != NET_INVALID_PORT) {
        const struct sockaddr_in *addr = (const struct sockaddr_in *) name;

        if (addr->sin_addr.S_un.S_addr == gfdm_server_addr.addr &&
            addr->sin_port != htons(gfdm_server_addr.port)) {
            memcpy(&patched, addr, sizeof(patched));
            patched.sin_port = htons(gfdm_server_addr.port);
            name = (const struct sockaddr *) &patched;
        }
    }

    return real_connect(sock, name, namelen);
}

static const struct hook_symbol network_syms[] = {
    {
        .name = "getaddrinfo",
        .ordinal = 176,
        .patch = gfdm_getaddrinfo,
        .link = (void **) &real_getaddrinfo,
    },
    {
        .name = "gethostbyname",
        .ordinal = 52,
        .patch = gfdm_gethostbyname,
        .link = (void **) &real_gethostbyname,
    },
    {
        .name = "connect",
        .ordinal = 4,
        .patch = gfdm_connect,
        .link = (void **) &real_connect,
    },
};

void gfdmhook1_network_init(const struct net_addr *server)
{
    memset(&gfdm_server_addr, 0, sizeof(gfdm_server_addr));
    gfdm_server_addr.addr = NET_INVALID_ADDR;
    gfdm_server_addr.port = NET_INVALID_PORT;
    memset(gfdm_server_host, 0, sizeof(gfdm_server_host));

    if (!net_resolve_hostname_net_addr(server, &gfdm_server_addr)) {
        log_warning("Could not resolve configured e-amusement server");
    }

    if (gfdm_server_addr.port == NET_INVALID_PORT) {
        gfdm_server_addr.port =
            server->type == NET_ADDR_TYPE_URL && server->url.is_https ? 443 : 80;
    }

    if (server->type == NET_ADDR_TYPE_HOSTNAME) {
        gfdm_copy_server_host(server->hostname.host);
    } else if (server->type == NET_ADDR_TYPE_IPV4) {
        snprintf(
            gfdm_server_host,
            sizeof(gfdm_server_host),
            "%u.%u.%u.%u",
            (unsigned) ((server->ipv4.addr >> 0) & 0xFF),
            (unsigned) ((server->ipv4.addr >> 8) & 0xFF),
            (unsigned) ((server->ipv4.addr >> 16) & 0xFF),
            (unsigned) ((server->ipv4.addr >> 24) & 0xFF));
    } else if (server->type == NET_ADDR_TYPE_URL) {
        if (server->url.type == NET_ADDR_TYPE_HOSTNAME) {
            gfdm_copy_server_host(server->url.hostname.host);
        } else if (server->url.type == NET_ADDR_TYPE_IPV4) {
            snprintf(
                gfdm_server_host,
                sizeof(gfdm_server_host),
                "%u.%u.%u.%u",
                (unsigned) ((server->url.ipv4.addr >> 0) & 0xFF),
                (unsigned) ((server->url.ipv4.addr >> 8) & 0xFF),
                (unsigned) ((server->url.ipv4.addr >> 16) & 0xFF),
                (unsigned) ((server->url.ipv4.addr >> 24) & 0xFF));
        }
    }

    hook_table_apply(NULL, "ws2_32.dll", network_syms, lengthof(network_syms));
    log_info("Inserted GFDM e-amusement resolver hook (%s)", gfdm_server_host);
}
