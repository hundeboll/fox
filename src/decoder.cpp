/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#include "decoder.hpp"

DECLARE_double(decoder_timeout);
DECLARE_double(packet_timeout);
DECLARE_int32(ack_interval);

template<>
void decoder::send_decoded_packet(size_t i)
{
    struct nl_msg *msg;
    uint16_t len;
    uint8_t *buf;

    /* don't send already decoded packets */
    if (m_decoded_symbols[i])
        return;

    /* Read out length field from decoded data */
    buf = this->symbol(i);
    len = *reinterpret_cast<uint16_t *>(buf);

    /* avoid wrongly decoded packets by checking that the
     * length is within expected range
     */
    LOG_IF(FATAL, len > RLNC_MAX_PAYLOAD) << "Decoder " << m_coder
                                          << ": Failed packet " << i;

    msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, m_io->genl_family(),
                0, 0, BATADV_HLP_C_FRAME, 1);

    nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_io->ifindex());
    nla_put_u8(msg, BATADV_HLP_A_TYPE, DEC_PACKET);
    nla_put(msg, BATADV_HLP_A_FRAME, len, buf + LEN_SIZE);

    m_io->send_msg(msg);
    nlmsg_free(msg);

    VLOG(LOG_PKT) << "Decoder " << m_coder << ": Send decoded packet " << i;
    inc("decoded sent");
    m_decoded_symbols[i] = true;
}

template<>
void decoder::send_partial_decoded_packets(size_t rank)
{
    for (size_t i = 0; i < rank; i++)
        send_decoded_packet(i);
}

template<>
void decoder::send_decoded_packets()
{
    double ack_budget = source_budget(1, 254, 254, m_e3);

    VLOG(LOG_GEN) << "Decoder " << m_coder << ": Send decoded packets";
    inc("generations decoded");

    guard g(m_lock);
    for (; ack_budget > 0; ack_budget--)
        send_ack_packet();

    send_partial_decoded_packets(this->symbols());
    dispatch_event(EVENT_ACKED);
}

template<>
void decoder::send_request(size_t seq)
{
    struct nl_msg *msg;

    msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, m_io->genl_family(),
                0, 0, BATADV_HLP_C_FRAME, 1);

    nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_io->ifindex());
    nla_put_u8(msg, BATADV_HLP_A_TYPE, REQ_PACKET);
    nla_put(msg, BATADV_HLP_A_SRC, ETH_ALEN, _key.src);
    nla_put(msg, BATADV_HLP_A_DST, ETH_ALEN, _key.dst);
    nla_put_u16(msg, BATADV_HLP_A_BLOCK, _key.block);
    nla_put_u16(msg, BATADV_HLP_A_RANK, this->rank());
    nla_put_u16(msg, BATADV_HLP_A_SEQ, seq);

    m_io->send_msg(msg);
    nlmsg_free(msg);

    inc("request sent");
    VLOG(LOG_CTRL) << "Decoder " << m_coder << ": Sent request packet";
}

template<>
void decoder::init()
{
    guard g(m_lock);

    set_group("decoder");
    set_state(STATE_WAIT);
    init_timeout(FLAGS_decoder_timeout);
    set_pkt_timeout(FLAGS_packet_timeout);

    /* Reset list of decoded packets. */
    m_decoded_symbols.resize(this->symbols());
    std::fill(m_decoded_symbols.begin(), m_decoded_symbols.end(), false);

    m_enc_pkt_count = 0;
    m_red_pkt_count = 0;
    m_req_seq = 1;
    VLOG(LOG_GEN) << "Decoder " << m_coder << ": Initialized " << _key;
}

template<>
void decoder::add_enc_packet(const uint8_t *data, const uint16_t len)
{
    size_t rank, symbol_index, msecs;
    bool systematic;
    size_t size = this->payload_size();

    guard g(m_lock);

    if (this->is_complete()) {
        inc("redundant received");
        if (++m_red_pkt_count % FLAGS_ack_interval == 0)
            send_ack_packet();
        return;
    }

    CHECK_EQ(len, size) << "Decoder " << m_coder
                        << ": Invalid length:" << len << " != " << size;

    rank = this->rank();
    this->decode(const_cast<uint8_t *>(data));
    m_enc_pkt_count++;

    if (this->rank() == rank) {
        VLOG(LOG_PKT) << "Decoder " << m_coder << ": Added non-innovative";
        inc("non-innovative received");
        update_timestamp();
        update_packet_timestamp();
        return;
    }

    systematic = this->last_symbol_is_systematic();
    symbol_index = this->last_symbol_index();

    if (this->is_complete()) {
        dispatch_event(EVENT_COMPLETE);
        return;
    }

    if (this->is_partial_complete())
        send_partial_decoded_packets(this->rank());

    if (systematic) {
        inc("systematic received");
        VLOG(LOG_PKT) << "Decoder " << m_coder << ": Added systematic ("
                      << symbol_index << ")";
        send_decoded_packet(symbol_index);
    } else {
        VLOG(LOG_PKT) << "Decoder " << m_coder << ": Added encoded";
        inc("encoded received");
    }

    update_timestamp();
    update_packet_timestamp();
}

template<>
bool decoder::process()
{
    if (curr_state() == STATE_DONE)
        return true;

    if (is_timed_out() && !this->is_complete() &&
        !this->is_partial_complete()) {
        LOG(ERROR) << "Decoder " << m_coder << ": Timed out (rank "
                   << this->rank() << ")";
        inc("incomplete timeouts");
        dispatch_event(EVENT_TIMEOUT);
        return false;
    }

    if (is_timed_out()) {
        dispatch_event(EVENT_TIMEOUT);
        return false;
    }

    if (curr_state() == STATE_WAIT && packet_timed_out()) {

        if (this->is_partial_complete())
            return false;

        double req_budget = source_budget(1, 254, 254, m_e3);

        VLOG(LOG_GEN) << "Decoder " << m_coder << ": Request more data (rank "
                      << this->rank() << ", seq " << m_req_seq << ")";

        for (; req_budget >= 0; req_budget--)
            send_request(m_req_seq);
        m_req_seq++;
        update_packet_timestamp();

        return false;
    }

    return false;
}
