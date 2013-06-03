/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#include <string>

#include "io.hpp"

DECLARE_string(device);
DECLARE_int32(encoders);
DECLARE_int32(e1);
DECLARE_int32(e2);
DECLARE_int32(e3);
DECLARE_bool(benchmark);

bool io::open_netlink()
{
    std::string family_name("batman_adv");

    m_cb = CHECK_NOTNULL(nl_cb_alloc(NL_CB_CUSTOM));

    m_nl_sock = CHECK_NOTNULL(nl_socket_alloc_cb(m_cb));

    CHECK_GE(genl_connect(m_nl_sock), 0)
        << "io: Failed to connect netlink socket";

    CHECK_GE(nl_socket_set_buffer_size(m_nl_sock, 1048576, 1048576), 0)
        << "IO: Unable to set socket buffer size";

    CHECK_GE(genl_ctrl_alloc_cache(m_nl_sock, &m_cache), 0)
        << "IO: Failed to allocate control cache";

    m_family = CHECK_NOTNULL(genl_ctrl_search_by_name(m_cache,
                                                      family_name.c_str()));

    nl_cb_set(m_cb, NL_CB_MSG_IN, NL_CB_CUSTOM, process_messages_wrapper, this);
    m_nl_thread = std::thread(nl_thread, this);

    return true;
}


bool io::register_netlink()
{
    struct nl_msg *msg(nlmsg_alloc());

    if (!msg)
        return false;

    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, genl_family(), 0, NLM_F_REQUEST,
                BATADV_HLP_C_REGISTER, 1);

    CHECK_GE(nla_put_string(msg, BATADV_HLP_A_IFNAME, FLAGS_device.c_str()), 0)
        << "IO: Failed to put ifname attribute";

    CHECK_GE(nla_put_u32(msg, BATADV_HLP_A_ENCS, FLAGS_encoders), 0)
            << "IO: Failed to put encoders attribute";

    CHECK_GE(nla_put_u32(msg, BATADV_HLP_A_E1, FLAGS_e1), 0)
            << "IO: Failed to put e1 attribute";

    CHECK_GE(nla_put_u32(msg, BATADV_HLP_A_E2, FLAGS_e2), 0)
            << "IO: Failed to put e2 attribute";

    CHECK_GE(nla_put_u32(msg, BATADV_HLP_A_E3, FLAGS_e3), 0)
            << "IO: Failed to put e3 attribute";

    guard g(m_nl_lock);

    CHECK_GE(nl_send_auto(m_nl_sock, msg), 0)
        << "IO: failed to send netlink register message";

    nlmsg_free(msg);
    return true;
}

bool io::open()
{
    CHECK(open_netlink()) << "IO: Failed to open netlink";
    CHECK(register_netlink()) << "IO: Failed to register netlink";

    return true;
}

int io::process_messages_cb(struct nl_msg *msg, void *arg)
{
    struct nlattr *attrs[BATADV_HLP_A_NUM], *attr;
    struct nlattr *hop_attrs[BATADV_HLP_HOP_A_NUM], *hop;
    struct nlattr *rly_attr[BATADV_HLP_RLY_A_NUM], *rly;
    struct nlmsghdr *nlh = nlmsg_hdr(msg);
    struct genlmsghdr *gnlh = (struct genlmsghdr *)nlmsg_data(nlh);
    struct helper_msg *h;
    struct key k;
    uint8_t tq, tq2, type;
    uint16_t block, len, rank, seq = 0;
    uint8_t *src, *dst, *data;
    int i, err;
    void *tmp;

    genlmsg_parse(nlh, 0, attrs, BATADV_HLP_A_MAX, NULL);

    switch (gnlh->cmd) {
        case BATADV_HLP_C_REGISTER:
            VLOG(LOG_NL) << "IO: Received register message";

            if (!attrs[BATADV_HLP_A_IFINDEX])
                break;

            m_genl_if_index = nla_get_u32(attrs[BATADV_HLP_A_IFINDEX]);
            break;

        case BATADV_HLP_C_GET_RELAYS:
            VLOG(LOG_NL) << "IO: Received relays message";

            if (!attrs[BATADV_HLP_A_SRC])
                break;

            if (!attrs[BATADV_HLP_A_DST])
                break;

            if (!attrs[BATADV_HLP_A_RLY_LIST])
                break;

            tmp = nla_data(attrs[BATADV_HLP_A_SRC]);
            src = reinterpret_cast<uint8_t *>(tmp);

            tmp = nla_data(attrs[BATADV_HLP_A_DST]);
            dst = reinterpret_cast<uint8_t *>(tmp);
            clear_helpers(src, dst);

            nla_for_each_nested(rly, attrs[BATADV_HLP_A_RLY_LIST], i) {
                if (nla_type(rly) != BATADV_HLP_RLY_A_INFO)
                    continue;

                h = reinterpret_cast<struct helper_msg *>(nla_data(rly));
                add_helper(src, dst, h);
            }
            break;

        case BATADV_HLP_C_GET_LINK:
            VLOG(LOG_NL) << "IO: Received link message";

            if (!attrs[BATADV_HLP_A_TQ])
                break;

            if (!attrs[BATADV_HLP_A_ADDR])
                break;

            tmp = nla_data(attrs[BATADV_HLP_A_ADDR]);
            dst = reinterpret_cast<uint8_t *>(tmp);
            tq = nla_get_u8(attrs[BATADV_HLP_A_TQ]);
            add_link(dst, tq);
            break;

        case BATADV_HLP_C_GET_ONE_HOP:
            VLOG(LOG_NL) << "IO: Received one hops message";

            if (!attrs[BATADV_HLP_A_ADDR])
                break;

            if (!attrs[BATADV_HLP_A_HOP_LIST])
                break;

            tmp = nla_data(attrs[BATADV_HLP_A_ADDR]);
            dst = reinterpret_cast<uint8_t *>(tmp);
            clear_one_hops(dst);

            nla_for_each_nested(hop, attrs[BATADV_HLP_A_HOP_LIST], i) {
                if (nla_type(hop) != BATADV_HLP_HOP_A_INFO)
                    continue;

                h = reinterpret_cast<struct helper_msg *>(nla_data(hop));
                add_one_hop(dst, h);
            }
            break;

        case BATADV_HLP_C_FRAME:
            if (!attrs[BATADV_HLP_A_FRAME])
                break;

            if (attrs[BATADV_HLP_A_RANK])
                rank = nla_get_u16(attrs[BATADV_HLP_A_RANK]);

            if (attrs[BATADV_HLP_A_SEQ])
                seq = nla_get_u16(attrs[BATADV_HLP_A_SEQ]);

            type = nla_get_u8(attrs[BATADV_HLP_A_TYPE]);
            tmp = nla_data(attrs[BATADV_HLP_A_SRC]);
            src = reinterpret_cast<uint8_t *>(tmp);
            tmp = nla_data(attrs[BATADV_HLP_A_DST]);
            dst = reinterpret_cast<uint8_t *>(tmp);
            block = nla_get_u16(attrs[BATADV_HLP_A_BLOCK]);
            tmp = nla_data(attrs[BATADV_HLP_A_FRAME]);
            data = reinterpret_cast<uint8_t *>(tmp);
            len = nla_len(attrs[BATADV_HLP_A_FRAME]);
            k.set(src, dst, block);

            VLOG(LOG_PKT) << "IO: Received frame message: "
                          << static_cast<int>(type);

            if (FLAGS_benchmark) {
                msg = nlmsg_alloc();
                genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, genl_family(),
                            0, 0, BATADV_HLP_C_FRAME, 1);

                nla_put_u32(msg, BATADV_HLP_A_IFINDEX, ifindex());
                nla_put_u8(msg, BATADV_HLP_A_TYPE, PLAIN_PACKET);
                nla_put(msg, BATADV_HLP_A_FRAME, len, data);

                send_msg(msg);
                nlmsg_free(msg);
                break;
            }

            handle_packet(type, k, data, len, rank, seq);
            break;

        default:
            break;
    }

    return NL_STOP;
}

bool io::send_nl(int cmd, int type, uint8_t *data, size_t len)
{
    struct nl_msg *msg;
    struct genlmsghdr *hdr;

    msg = CHECK_NOTNULL(nlmsg_alloc());

    /* set up command */
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, genl_family(), 0, NLM_F_REQUEST,
                cmd, 1);

    CHECK_GE(nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_genl_if_index), 0)
        << "IO: Failed to put interface index";

    CHECK_GE(nla_put(msg, type, len, data), 0)
        << "IO: Failed to put attribute";

    guard g(m_nl_lock);

    CHECK_GE(nl_send_auto(m_nl_sock, msg), 0)
        << "IO: Failed to send netlink message";

    nlmsg_free(msg);

    return true;
}

void io::read_helpers(const key &k)
{
    struct nl_msg *msg;
    struct genlmsghdr *hdr;

    msg = CHECK_NOTNULL(nlmsg_alloc());

    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, genl_family(), 0, NLM_F_REQUEST,
                BATADV_HLP_C_GET_RELAYS, 1);

    CHECK_GE(nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_genl_if_index), 0)
        << "IO: Failed to put interface index";

    CHECK_GE(nla_put(msg, BATADV_HLP_A_SRC, ETH_ALEN, k.src), 0)
        << "IO: Failed to put source address attribute";

    CHECK_GE(nla_put(msg, BATADV_HLP_A_DST, ETH_ALEN, k.dst), 0)
        << "IO: Failed to put destination address attribute";

    guard g(m_nl_lock);

    CHECK_GE(nl_send_auto(m_nl_sock, msg), 0)
        << "IO: failed to send netlink message";

    nlmsg_free(msg);
}
