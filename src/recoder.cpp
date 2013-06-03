/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#include "recoder.hpp"

DECLARE_double(recoder_timeout);
DECLARE_double(fixed_overshoot);

template<>
void recoder::send_rec_packet()
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
    nla_put_u8(msg, BATADV_HLP_A_TYPE, REC_PACKET);
    attr = nla_reserve(msg, BATADV_HLP_A_FRAME, this->payload_size());
    data = reinterpret_cast<uint8_t *>(nla_data(attr));

    this->recode(data);
    m_io->send_msg(msg);
    nlmsg_free(msg);

    m_rec_pkt_count++;
    inc("forward packets written");
}

template<>
void recoder::send_systematic_packet(const uint8_t *data, const uint16_t len)
{
    struct nl_msg *msg;

    msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, m_io->genl_family(),
                0, 0, BATADV_HLP_C_FRAME, 1);

    nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_io->ifindex());
    nla_put(msg, BATADV_HLP_A_SRC, ETH_ALEN, _key.src);
    nla_put(msg, BATADV_HLP_A_DST, ETH_ALEN, _key.dst);
    nla_put_u16(msg, BATADV_HLP_A_BLOCK, _key.block);
    nla_put_u8(msg, BATADV_HLP_A_TYPE, REC_PACKET);
    nla_put(msg, BATADV_HLP_A_FRAME, len, const_cast<uint8_t *>(data));

    m_io->send_msg(msg);
    nlmsg_free(msg);

    m_rec_pkt_count++;
    inc("systematic packets written");
}

template<>
void recoder::send_rec_credits()
{
    update_budget();

    if (m_budget <= 0) {
        dispatch_event(EVENT_CREDIT_SENT);
        return;
    }

    for (; m_budget > 0 && m_rec_pkt_count <= m_max_budget; m_budget--) {
        guard g(m_lock);
        send_rec_packet();
    }

    if (m_rec_pkt_count >= m_max_budget) {
        dispatch_event(EVENT_MAXED);
    } else {
        dispatch_event(EVENT_CREDIT_SENT);
    }
}

template<>
void recoder::send_rec_budget()
{
    while (m_rec_pkt_count < m_max_budget && next_state() == STATE_SEND_BUDGET) {
        guard g(m_lock);
        send_rec_packet();
    }

    dispatch_event(EVENT_BUDGET_SENT);
    inc("forward generations written");
    VLOG(LOG_GEN) << "Recoder " << m_coder << ": Write recoded packets ("
                  << m_rec_pkt_count << " of " << m_max_budget << ")";
}

template<>
void recoder::send_rec_redundant()
{
    VLOG(LOG_PKT) << "Recoder " << m_coder
                  << ": Sending redundant packets (state: "
                  << static_cast<int>(curr_state()) << ")";

    guard g(m_lock);
    send_rec_packet();
}

template<>
void recoder::init()
{
    guard g(m_lock);

    set_group("recoder");
    set_state(STATE_WAIT);
    init_timeout(FLAGS_recoder_timeout);

    /* reset counters */
    m_budget = 0;
    m_rec_pkt_count = 0;

    m_io->read_one_hops(_key.dst);
    helper_msg best_helper = m_io->get_best_one_hop(_key.dst);
    if (best_helper.tq_total == 0) {
        VLOG(LOG_GEN) << "Recoder " << m_coder << ": No best one hop";
        m_max_budget = this->symbols()*FLAGS_fixed_overshoot;
        return;
    }

    m_io->read_link(best_helper.addr);
    m_io->read_link(_key.dst);
    e1 = ONE - m_io->get_link(best_helper.addr);
    e2 = ONE - best_helper.tq_second_hop * 4.5;  // Scale to revert hop penalty
    e3 = ONE - m_io->get_link(_key.dst);

    if (e1 == ONE || e2 == ONE || e3 == ONE) {
        VLOG(LOG_GEN) << "Recoder " << m_coder << ": Missing link estimate";
        m_max_budget = this->symbols()*FLAGS_fixed_overshoot;
        return;
    }

    m_max_budget = recoder_budget(this->symbols(), e1, e2, e3);
    VLOG(LOG_GEN) << "Recoder " << m_coder << ": Initialized" << _key;
}

template<>
void recoder::add_enc_packet(const uint8_t *data, const uint16_t len)
{
    size_t tmp_rank;

    guard g(m_lock);

    /* don't add packets when we have enough, and try to stop encoder
     * from sending more packets
     */
    if (this->is_complete()) {
        send_ack_packet();
        return;
    }

    if (curr_state() == STATE_DONE)
        return;

    CHECK_EQ(len, this->payload_size()) << "Recoder " << m_coder
                                        << ": Encoded data is too short:"
                                        << len << " < " << this->payload_size();

    /* keep track of changes in rank */
    tmp_rank = this->rank();

    this->decode(const_cast<uint8_t *>(data));

    /* check if rank improved */
    if (this->rank() == tmp_rank)
        inc("non-innovative recoded packets");

    update_timestamp();

    if (this->last_symbol_is_systematic()) {
        inc("systematic packets added");
        send_systematic_packet(data, len);
        m_budget--;
    } else {
        inc("encoded packets added");
    }

    /* signal state machine if generation is complete */
    if (this->is_complete()) {
        send_ack_packet();
        dispatch_event(EVENT_COMPLETE);
    } else {
        dispatch_event(EVENT_RX);
    }

    VLOG(LOG_PKT) << "Recoder " << m_coder << ": Added encoded packet";
}

template<>
void recoder::add_ack_packet()
{
    dispatch_event(EVENT_ACKED);
    VLOG(LOG_CTRL) << "Recoder " << m_coder << ": Sent "
                   << m_rec_pkt_count << " recoded packets";
}

template<>
bool recoder::process()
{
    /* check if done coding */
    if (curr_state() == STATE_DONE)
        return true;

    /* check if timed out */
    if (is_timed_out()) {
        VLOG(LOG_GEN) << "Recoder " << m_coder << ": Timed out";
        dispatch_event(EVENT_TIMEOUT);
        return false;
    }

    return false;
}
