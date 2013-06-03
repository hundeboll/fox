/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#include "encoder.hpp"
#include <chrono>
#include <thread>

DECLARE_double(encoder_timeout);
DECLARE_double(encoder_threshold);

template<>
void encoder::send_encoded_packet(uint8_t type)
{
    struct nl_msg *msg;
    struct nlattr *attr;
    uint8_t *data;
    size_t symbols = this->symbols();

    VLOG(LOG_PKT) << "Encoder " << m_coder << ": Send "
                  << (m_enc_pkt_count < symbols ? "systematic" : "encoded");

    msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, m_io->genl_family(),
                0, 0, BATADV_HLP_C_FRAME, 1);

    nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_io->ifindex());
    nla_put(msg, BATADV_HLP_A_SRC, ETH_ALEN, _key.src);
    nla_put(msg, BATADV_HLP_A_DST, ETH_ALEN, _key.dst);
    nla_put_u16(msg, BATADV_HLP_A_BLOCK, _key.block);
    nla_put_u8(msg, BATADV_HLP_A_TYPE, type);
    attr = nla_reserve(msg, BATADV_HLP_A_FRAME, this->payload_size());
    data = reinterpret_cast<uint8_t *>(nla_data(attr));

    this->encode(data);
    m_io->send_msg(msg);
    nlmsg_free(msg);

    m_enc_pkt_count++;
    inc("encoded sent");
    m_budget--;
}

template<>
void encoder::send_encoded_credit()
{
    while (m_budget >= 1 && m_enc_pkt_count < m_max_budget)
        send_encoded_packet(m_type);
}

template<>
void encoder::send_encoded_budget()
{
    VLOG(LOG_GEN) << "Encoder " << m_coder << ": Send "
                  << (m_max_budget - m_enc_pkt_count) << " redundant packets";

    guard g(m_lock);

    while (m_enc_pkt_count < m_max_budget)
        send_encoded_packet(m_type);

    update_timestamp();
    dispatch_event(EVENT_BUDGET_SENT);
}

template<>
void encoder::block_packets(int block_cmd)
{
    struct nl_msg *msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, m_io->genl_family(),
                0, 0, block_cmd, 1);
    nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_io->ifindex());
    m_io->send_msg(msg);
    nlmsg_free(msg);

    VLOG(LOG_GEN) << "Encoder " << m_coder << ": Sent "
                  << (block_cmd == BATADV_HLP_C_BLOCK ? "block" : "unblock")
                  << "message";
}

template<>
void encoder::init()
{
    guard g(m_lock);

    set_group("encoder");
    set_state(STATE_WAIT);
    init_timeout(FLAGS_encoder_timeout);

    /* allocate memory for encoder */
    if (!m_symbol_storage)
        m_symbol_storage = new uint8_t[this->block_size()];

    /* reset counters */
    m_plain_pkt_count = 0;
    m_enc_pkt_count = 0;
    m_last_req_seq = 0;
    m_type = ENC_PACKET;

    m_io->read_link(_key.dst);
    m_io->read_one_hops(_key.dst);

    helper_msg best_helper = m_io->get_best_one_hop(_key.dst);
    m_io->read_link(best_helper.addr);

    /*
    e1 = ONE - m_io->get_link(best_helper.addr);
    e2 = ONE - best_helper.tq_second_hop * 4.5; // Scale to revert hop penalty
    e3 = ONE - m_io->get_link(_key.dst);
    */

    m_max_budget = source_budget(this->symbols(), m_e1, m_e2, m_e3);
    VLOG(LOG_GEN) << "Encoder " << m_coder << ": Initialized (B: "
                  << m_max_budget << ") " << _key;
}

template<>
void encoder::add_plain_packet(const uint8_t *data, const uint16_t len)
{
    uint8_t *buf;
    size_t size = this->symbol_size();

    CHECK_LE(len, size - LEN_SIZE) << "Encoder " << m_coder
                                   << ": Plain packet is too long: "
                                   << len << " > " << size - LEN_SIZE;

    guard g(m_lock);

    /* make sure encoder is in a state to accept plain packets */
    if (curr_state() != STATE_WAIT)
        return;

    buf = get_symbol_buffer(m_plain_pkt_count);
    *reinterpret_cast<uint16_t *>(buf) = len;
    memcpy(buf + LEN_SIZE, data, len);

    /* Copy data into encoder storage */
    sak::mutable_storage symbol(buf, this->symbol_size());
    this->set_symbol(m_plain_pkt_count++, symbol);

    update_timestamp();
    inc("plain packets added");
    VLOG(LOG_PKT) << "Encoder " << m_coder << ": Added plain packet";

    if (is_full()) {
        inc("generations");
        dispatch_event(EVENT_FULL);
    } else if (this->rank() > FLAGS_encoder_threshold*this->symbols() &&
               semaphore_count() > 0) {
        m_budget += recoder_credit(m_e1, m_e2, m_e3);
        send_encoded_credit();
    }
}

template<>
void encoder::add_ack_packet()
{
    guard g(m_lock);

    if (curr_state() == STATE_DONE)
        return;

    if (m_plain_pkt_count == this->symbols())
        enc_notify();

    dispatch_event(EVENT_ACKED);
    inc("ack packets added");
    VLOG(LOG_CTRL) << "Encoder " << m_coder << ": Acked after "
                   << m_enc_pkt_count << " packets";
}

template<>
void encoder::add_req_packet(const uint16_t rank, const uint16_t seq)
{
    double credits = source_budget(this->rank() - rank, 254, 254, m_e3);

    guard g(m_lock);

    if (m_last_req_seq == seq || rank == this->rank())
        return;

    m_budget = credits;
    if (m_enc_pkt_count >= m_max_budget)
        m_max_budget += credits;

    m_type = RED_PACKET;

    std::cout << "Encoder: " << m_coder << ": " << "budget: " << m_budget
              << ", max: " << m_max_budget
              << ", seq: " << seq
              << ", credits: " << credits
              << ", rank: " << rank << std::endl;

    send_encoded_credit();
    update_timestamp();
    m_last_req_seq = seq;

    inc("request packets added");
    VLOG(LOG_CTRL) << "Encoder " << m_coder << ": Request (rank " << rank
                   << ", credits " << credits << ")";
}

template<>
bool encoder::process()
{
    guard g(m_lock);

    if (curr_state() == STATE_FULL) {
        if (is_timed_out(FLAGS_encoder_timeout*5)) {
            inc("blocked timeouts");
            LOG(ERROR) << "Encoder " << m_coder
                          << ": Timed out while blocked";
            enc_notify();
            return true;
        }
        return false;
    }

    /* check if decoder is ready to be reused */
    if (curr_state() == STATE_DONE)
        return true;

    /* check if decoder is timed out */
    if (is_timed_out()) {
        LOG(ERROR) << "Encoder " << m_coder << ": Timed out (rank "
                   << m_plain_pkt_count
                   << ", state " << static_cast<int>(curr_state()) << ")";
        dispatch_event(EVENT_TIMEOUT);
        inc("timeouts");
        if (m_plain_pkt_count == this->symbols())
            enc_notify();
    }

    return false;
}
