/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_IO_HPP_
#define FOX_IO_HPP_

#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <string>
#include <utility>

#include "fox.hpp"
#include "counters.hpp"
#include "key.hpp"
#include "timeout.hpp"


enum batadv_rlnc_io {
    PLAIN_PACKET = 0,
    ENC_PACKET,
    RED_PACKET,
    DEC_PACKET,
    REC_PACKET,
    HLP_PACKET,
    REQ_PACKET,
    ACK_PACKET,
};

/**
 * struct helper_msg - information about helpers on one-hop links
 * addr: address of helper
 * tq_total: estimated src->dst link quality for this helper
 * tq_second_hop: estimated link quality from this helper to dst
 */
struct helper_msg {
    uint8_t addr[ETH_ALEN];
    uint8_t tq_total;
    uint8_t tq_second_hop;
};

#define LEN_SIZE sizeof(uint16_t)

#ifdef nla_for_each_nested
#undef nla_for_each_nested
#endif

#define nla_for_each_nested(pos, nla, rem) \
    for (pos = (struct nlattr *)nla_data(nla), rem = nla_len(nla); \
            nla_ok(pos, rem); \
            pos = nla_next(pos, &(rem)))

enum {
    BATADV_HLP_A_UNSPEC,
    BATADV_HLP_A_IFNAME,
    BATADV_HLP_A_IFINDEX,
    BATADV_HLP_A_SRC,
    BATADV_HLP_A_DST,
    BATADV_HLP_A_ADDR,
    BATADV_HLP_A_TQ,
    BATADV_HLP_A_HOP_LIST,
    BATADV_HLP_A_RLY_LIST,
    BATADV_HLP_A_FRAME,
    BATADV_HLP_A_BLOCK,
    BATADV_HLP_A_INT,
    BATADV_HLP_A_TYPE,
    BATADV_HLP_A_RANK,
    BATADV_HLP_A_SEQ,
    BATADV_HLP_A_ENCS,
    BATADV_HLP_A_E1,
    BATADV_HLP_A_E2,
    BATADV_HLP_A_E3,
    BATADV_HLP_A_NUM,
};
#define BATADV_HLP_A_MAX (BATADV_HLP_A_NUM - 1)

enum {
    BATADV_HLP_HOP_A_UNSPEC,
    BATADV_HLP_HOP_A_INFO,
    BATADV_HLP_HOP_A_NUM,
};
#define BATADV_HLP_HOP_A_MAX (BATADV_HLP_HOP_A_NUM - 1)

enum {
    BATADV_HLP_RLY_A_UNSPEC,
    BATADV_HLP_RLY_A_INFO,
    BATADV_HLP_RLY_A_NUM,
};
#define BATADV_HLP_RLY_A_MAX (BATADV_HLP_RLY_A_NUM - 1)

enum {
    BATADV_HLP_C_UNSPEC,
    BATADV_HLP_C_REGISTER,
    BATADV_HLP_C_GET_RELAYS,
    BATADV_HLP_C_GET_LINK,
    BATADV_HLP_C_GET_ONE_HOP,
    BATADV_HLP_C_FRAME,
    BATADV_HLP_C_BLOCK,
    BATADV_HLP_C_UNBLOCK,
    BATADV_HLP_C_NUM,
};
#define BATADV_HLP_C_MAX (BATADV_HLP_C_NUM - 1)

/**
 * class io - Handle read and write operations to batman-adv.
 */
class io : public counter_api
{
    std::thread m_nl_thread;
    std::mutex m_nl_lock;
    struct nl_sock *m_nl_sock;
    struct nl_cb *m_cb;
    struct nl_cache *m_cache;
    struct genl_family *m_family;
    int m_genl_if_index;
    volatile bool m_running;
    typedef std::pair<uint8_t, uint8_t> helper_val;
    typedef std::unordered_map<std::string, helper_val> helper_map;
    typedef std::unordered_map<std::string, helper_map> path_map;
    path_map m_helpers, m_one_hops;
    std::unordered_map<std::string, uint8_t> m_links;

    bool open_netlink();
    bool register_netlink();

    static int process_messages_wrapper(struct nl_msg *msg, void *arg)
    {
        return ((class io *)arg)->process_messages_cb(msg, NULL);
    }

    static void nl_thread(class io *i)
    {
        int ret;

        while (i->m_running) {
            ret = nl_recvmsgs_default(i->m_nl_sock);
            LOG_IF(ERROR, ret < 0) << "Netlink read error: " << nl_geterror(ret)
                                   << " (" << ret << ")";
        }
    }

    void add_link(const uint8_t *addr, const uint8_t tq)
    {
        std::string k(reinterpret_cast<const char *>(addr), ETH_ALEN);
        VLOG(LOG_NL) << "IO: Add link: " << k << " = " << tq;
        m_links[k] = tq;
    }

    void add_msg(path_map &map, const std::string &k1,
                 const struct helper_msg *m)
    {
        std::string k2(reinterpret_cast<const char *>(m->addr), ETH_ALEN);

        helper_map &h(map[k1]);
        helper_val &v(h[k2]);
        v.first = m->tq_total;
        v.second = m->tq_second_hop;
    }

    void add_helper(uint8_t *src, uint8_t *dst, struct helper_msg *m)
    {
        std::string k1((const char *)src, ETH_ALEN);
        k1 += std::string((const char *)dst, ETH_ALEN);

        VLOG(LOG_NL) << "IO: Add helper to path: " << k1 << "->"
                     << std::string(reinterpret_cast<char *>(m->addr), ETH_ALEN)
                     << " = (" << m->tq_total << ", " << m->tq_second_hop;

        add_msg(m_helpers, k1, m);
    }

    void clear_helpers(uint8_t *src, uint8_t *dst)
    {
        std::string k1((const char *)src, ETH_ALEN);
        k1 += std::string((const char *)dst, ETH_ALEN);

        VLOG(LOG_NL) << "IO: Clear helpers on path: " << k1;

        m_helpers[k1].clear();
    }

    void add_one_hop(uint8_t *addr, struct helper_msg *m)
    {
        std::string k1((const char *)addr, ETH_ALEN);

        VLOG(LOG_NL) << "IO: Add one hop towards: " << k1 << "->"
                     << std::string(reinterpret_cast<char *>(m->addr), ETH_ALEN)
                     << " = (" << m->tq_total << ", " << m->tq_second_hop;

        add_msg(m_one_hops, k1, m);
    }

    void clear_one_hops(uint8_t *addr)
    {
        std::string k1((const char *)addr, ETH_ALEN);

        VLOG(LOG_NL) << "IO: Clear one hops towards: " << k1;

        m_one_hops[k1].clear();
    }

  public:
    typedef std::shared_ptr<io> pointer;

    io() : m_nl_sock(NULL), m_running(true)
    {}

    /**
     * ~io() - Destruct io object.
     *
     * Closes and resets open sockets.
     */
    ~io()
    {
        m_running = false;

        /* force recv() to return by sending an empty message */
        genl_send_simple(m_nl_sock, genl_family(), BATADV_HLP_C_UNSPEC, 1, 0);

        guard g(m_nl_lock);
        if (m_nl_sock) {
            nl_close(m_nl_sock);
            m_nl_thread.join();
            nl_socket_free(m_nl_sock);
            free(m_cb);
            free(m_cache);
            free(m_family);
        }
    }

    void set_counts(counters::pointer counts)
    {
        counter_api::set_counts(counts);
        set_group("input/ouput");
    }

    bool open();
    int process_messages_cb(struct nl_msg *msg, void *arg);
    bool send_nl(int cmd, int type, uint8_t *data, size_t len);
    void read_helpers(const key &k);

    void send_msg(struct nl_msg *msg)
    {
        guard g(m_nl_lock);

        CHECK_GE(nl_send_auto(m_nl_sock, msg), 0)
            << "IO: Failed to send netling message";
    }

    void read_link(const uint8_t *addr)
    {
        VLOG(LOG_NL) << "IO: Read link: "
                     << std::string(reinterpret_cast<const char *>(addr),
                                    ETH_ALEN);
        send_nl(BATADV_HLP_C_GET_LINK, BATADV_HLP_A_ADDR,
                const_cast<uint8_t *>(addr), ETH_ALEN);
    }

    void read_links(const key &k)
    {
        read_link(k.src);
        read_link(k.dst);
    }

    void read_one_hops(const uint8_t *addr)
    {
        VLOG(LOG_NL) << "IO: Read one hops towards: "
                     << std::string(reinterpret_cast<const char *>(addr),
                                    ETH_ALEN);
        send_nl(BATADV_HLP_C_GET_ONE_HOP, BATADV_HLP_A_ADDR,
                const_cast<uint8_t *>(addr), ETH_ALEN);
    }

    uint8_t get_link(const uint8_t *addr)
    {
        return m_links[std::string((const char *)addr, ETH_ALEN)] ? : 1;
    }

    uint8_t get_zero_helper(const key &k)
    {
        std::string k1((const char *)k.raw, sizeof(k.raw));
        std::string k2("\0\0\0\0\0\0", ETH_ALEN);

        helper_map &m(m_helpers[k1]);

        return m[k2].first ? : 1;
    }

    helper_msg get_best_one_hop(const uint8_t *dst)
    {
        helper_map &h(m_one_hops[std::string((const char *)dst, ETH_ALEN)]);
        helper_msg one_hop = {1, 1};

        if (h.size() == 0)
            return one_hop;

        for (auto o : h) {
            if (o.second.first > one_hop.tq_total) {
                memcpy(one_hop.addr, o.first.c_str(), ETH_ALEN);
                one_hop.tq_total = o.second.first;
                one_hop.tq_second_hop = o.second.second;
            }
        }

        return one_hop;
    }

    int genl_family()
    {
        return genl_family_get_id(m_family);
    }

    int ifindex()
    {
        return m_genl_if_index;
    }
};

class io_api {
  protected:
    io::pointer m_io;

  public:
    void set_io(io::pointer io)
    {
        m_io = io;
    }
};

#endif
