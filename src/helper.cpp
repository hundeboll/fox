/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#include "helper.hpp"

DECLARE_double(helper_timeout);
DECLARE_int32(e1);
DECLARE_int32(e2);
DECLARE_int32(e3);

template<>
void helper::send_hlp_packet()
{
    struct nl_msg *msg;
    struct nlattr *attr;
    uint8_t *data;

    msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, m_io->genl_family(),
                0, 0, BATADV_HLP_C_FRAME, 1);

    nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_io->ifindex());
    nla_put(msg, BATADV_HLP_A_SRC, ETH_ALEN, _key.src);
    nla_put(msg, BATADV_HLP_A_DST, ETH_ALEN, _key.dst);
    nla_put_u16(msg, BATADV_HLP_A_BLOCK, _key.block);
    nla_put_u8(msg, BATADV_HLP_A_TYPE, HLP_PACKET);
    attr = nla_reserve(msg, BATADV_HLP_A_FRAME, this->payload_size());
    data = reinterpret_cast<uint8_t *>(nla_data(attr));

    this->recode(data);
    m_io->send_msg(msg);
    nlmsg_free(msg);

    m_hlp_pkt_count++;
    inc("helper packets");
    VLOG(LOG_PKT) << "Helper " << m_coder << ": Sent helper packet";
}

template<>
void helper::send_hlp_credits()
{
    m_budget += m_credit;

    if (m_budget <= 0)
        return;

    VLOG_IF(LOG_GEN, m_hlp_pkt_count == 0) << "Helper " << m_coder
                                           << ": Sending " << m_max_budget
                                           << " helper packets ";

    for (; m_budget >= 1 && m_hlp_pkt_count <= m_max_budget; m_budget--)
        send_hlp_packet();

    if (m_hlp_pkt_count >= m_max_budget)
        VLOG(LOG_GEN) << "Helper " << m_coder << ": Sent "
                      << m_hlp_pkt_count << " packets";
}

template<>
void helper::init()
{
    guard g(m_lock);

    set_group("helper");
    set_state(STATE_WAIT);
    init_timeout(FLAGS_helper_timeout);

    /* reset counters */
    m_hlp_pkt_count = 0;
    m_enc_pkt_count = 0;
    m_budget = 0;

    /* get link values */
    m_io->read_helpers(_key);
    m_io->read_links(_key);
    /*
    e1 = ONE - m_io->get_link(_key.src);
    e2 = ONE - m_io->get_link(_key.dst);
    e3 = ONE - m_io->get_zero_helper(_key);
    */
    e1 = FLAGS_e1*2.55;
    e2 = FLAGS_e2*2.55;
    e3 = FLAGS_e3*2.55;

    m_max_budget = max_budget();
    m_threshold = get_threshold();
    m_credit = credit();

    VLOG(LOG_GEN) << "Helper " << m_coder << ": Initialized "
                  << _key << std::endl
                  << " e1: " << static_cast<int>(e1)
                  << ", e2: " << static_cast<int>(e2)
                  << ", e3: " << static_cast<int>(e3) << std::endl
                  << " threshold: " << m_threshold << std::endl
                  << " credit: " << credit() << std::endl
                  << " budget: " << m_max_budget;
}

template<>
void helper::add_enc_packet(const uint8_t *data, const uint16_t len)
{
    size_t rank;

    guard g(m_lock);

    /* don't add packets when helper is done */
    if (curr_state() == STATE_DONE)
        return;

    CHECK_EQ(len, this->payload_size()) << "Helper " << m_coder
                                        << ": Encoded data has wrong length:"
                                        << len << " != "
                                        << this->payload_size();

    /* add packet to recoder */
    rank = this->rank();
    this->decode(const_cast<uint8_t *>(data));
    update_timestamp();
    m_enc_pkt_count++;
    inc("encoded received");

    if (rank == this->rank())
        return;

    /* signal if recoding should start */
    if (this->rank() >= m_threshold)
        send_hlp_credits();

    if (m_hlp_pkt_count >= m_max_budget)
        dispatch_event(EVENT_BUDGET_SENT);
}

template<>
void helper::add_ack_packet()
{
    dispatch_event(EVENT_ACKED);
    inc("acks received");
    VLOG(LOG_CTRL) << "Helper " << m_coder << ": Acked after sending "
                   << m_hlp_pkt_count << " packets";
}

template<>
void helper::add_req_packet(const uint16_t rank, const uint16_t seq)
{

}

template<>
bool helper::process()
{
    /* check if helper is done helping */
    if (curr_state() == STATE_DONE)
        return true;

    /* check if helper is timed out */
    if (is_timed_out()) {
        VLOG(LOG_GEN) << "Helper " << m_coder << ": Timed out (rank "
                      << this->rank() << ")";
        inc("timeouts");
        dispatch_event(EVENT_TIMEOUT);
    }

    return false;
}
