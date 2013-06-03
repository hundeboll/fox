/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_RECODER_HPP_
#define FOX_RECODER_HPP_

#include "kodo/rlnc/full_vector_codes.hpp"

#include "coder.hpp"
#include "systematic_decoder.hpp"

/**
 * class recoder - handle encoded packets at intermediate relays
 */
template<class Field>
class full_rlnc_recoder_deep
    : public
             // Payload API
             payload_recoder<recoding_stack,
             payload_decoder<
             // Codec Header API
             systematic_decoder<
             systematic_decoder_info<
             symbol_id_decoder<
             // Symbol ID API
             plain_symbol_id_reader<
             // Codec API
             aligned_coefficients_decoder<
             linear_block_decoder<
             // Coefficient Storage API
             coefficient_storage<
             coefficient_info<
             // Storage API
             deep_symbol_storage<
             storage_bytes_used<
             storage_block_info<
             // Finite Field API
             finite_field_math<typename fifi::default_field<Field>::type,
             finite_field_info<Field,
             // Factory API
             final_coder_factory_pool<
             // Final type
             full_rlnc_recoder_deep<Field>
                 > > > > > > > > > > > > > > > >, public coder
{
    std::atomic<size_t> m_rec_pkt_count;
    uint8_t e1, e2, e3;
    ssize_t m_budget, m_max_budget;

    /**
     * enum _state - states that this helper can reside in
     * @STATE_INVALID:    inherited state that should never be entered
     * @STATE_WAIT:       inherited (intial) state when waiting for the first event
     * @STATE_DONE:       inherited (final) state to enter when encoding is done
     * @STATE_SEND_FWD:   sends encoded packets until the generation is acked
     * @STATE_NUM:        counter for the number of states in this class
     */
    enum m_state : state_type {
        STATE_INVALID = __STATE_INVALID,
        STATE_WAIT = __STATE_WAIT,
        STATE_DONE = __STATE_DONE,
        STATE_SEND_CREDIT,
        STATE_SEND_BUDGET,
        STATE_WAIT_ACK,
        STATE_NUM
    };

    /**
     * enum _event - event that can change the state of this decoder
     * @EVENT_COMPLETE: generation is ready to be recoded
     * @EVENT_TIMEOUT:  generation has timed out
     * @EVENT_ACKED:    generation has been acked by next hop
     * @EVENT_NUM:      number of events in this encoder
     */
    enum m_event : event_type {
        EVENT_TIMEOUT,
        EVENT_RX,
        EVENT_COMPLETE,
        EVENT_ACKED,
        EVENT_MAXED,
        EVENT_CREDIT_SENT,
        EVENT_BUDGET_SENT,
        EVENT_NUM
    };

    /**
     * _write_fwd_packet() - write one recoded packet to batman-adv.
     */
    void send_rec_packet();

    /**
     * _write_fwd_packets() - write recoded packets until generation is
     * acked by next-hop.
     *
     * First the generation is acknowledged to the previous hop. Secondly,
     * one "generation" of recoded packets is sent. Thirdly, redundant
     * packets are sent until the generation is either acknowledged or times
     * out.
     */
    void send_systematic_packet(const uint8_t *data, const uint16_t len);

    void send_rec_credits();

    void send_rec_budget();

    void send_rec_redundant();

    ssize_t update_budget()
    {
        if (e1 == ONE || e2 == ONE || e3 == ONE)
            return m_budget + 2;

        m_budget += recoder_credit(m_e1, m_e2, m_e3);
        return m_budget;
    }

  public:
    /**
     * full_rlnc_recoder_deep() - construct recoder object
     *
     * Allocates packet buffers and initializes state machine
     */
    full_rlnc_recoder_deep()
    {
        states::handler_func credit, budget, ack;

        /* prepare state tables */
        states::init(m_coder, STATE_NUM, EVENT_NUM);

        /* add states to state table */
        credit = std::bind(&full_rlnc_recoder_deep::send_rec_credits, this);
        budget = std::bind(&full_rlnc_recoder_deep::send_rec_budget, this);
        ack = std::bind(&full_rlnc_recoder_deep::send_rec_redundant, this);
        add_state(STATE_SEND_CREDIT, credit);
        add_state(STATE_SEND_BUDGET, budget);
        add_state(STATE_WAIT_ACK, ack);

        /* add allowed transitions to state table */
        add_trans(STATE_WAIT, EVENT_RX, STATE_SEND_CREDIT);
        add_trans(STATE_WAIT, EVENT_COMPLETE, STATE_SEND_BUDGET);
        add_trans(STATE_WAIT, EVENT_TIMEOUT, STATE_DONE);
        add_trans(STATE_WAIT, EVENT_ACKED, STATE_DONE);
        add_trans(STATE_SEND_CREDIT, EVENT_CREDIT_SENT, STATE_WAIT);
        add_trans(STATE_SEND_CREDIT, EVENT_ACKED, STATE_DONE);
        add_trans(STATE_SEND_CREDIT, EVENT_MAXED, STATE_WAIT_ACK);
        add_trans(STATE_SEND_CREDIT, EVENT_RX, STATE_SEND_CREDIT);
        add_trans(STATE_SEND_CREDIT, EVENT_COMPLETE, STATE_SEND_BUDGET);
        add_trans(STATE_SEND_BUDGET, EVENT_ACKED, STATE_DONE);
        add_trans(STATE_SEND_BUDGET, EVENT_BUDGET_SENT, STATE_WAIT_ACK);
        add_trans(STATE_WAIT_ACK, EVENT_ACKED, STATE_DONE);
        add_trans(STATE_WAIT_ACK, EVENT_TIMEOUT, STATE_DONE);
        add_trans(STATE_WAIT_ACK, EVENT_RX, STATE_WAIT_ACK);
        add_trans(STATE_WAIT_ACK, EVENT_COMPLETE, STATE_WAIT_ACK);
        add_trans(STATE_DONE, EVENT_ACKED, STATE_DONE);
        add_trans(STATE_DONE, EVENT_RX, STATE_DONE);
    }

    /**
     * init() - Allocate member buffers and setup packet headers.
     *
     * For each buffer there is a packet header assigned, which is
     * initialized with constant values.
     *
     * Internal counters are reset and the state is changed to the
     * initial state (STATE_WAIT).
     *
     * Must be called immediately after construction of class.
     */
    void init();

    /**
     * add_fwd_packet() - Add encoded packet to recoder
     * @param pkt Encoded packet to be recoded.
     *
     * Add the encoded packet to the decoder and writes one recoded packet
     * immediately afterwards. Timestamp and packet counter are updated.
     */
    void add_enc_packet(const uint8_t *data, const uint16_t len);

    /**
     * add_ack_packet() - handle aknowledgement from next-hop
     * pkt: packet header with aknowledgement
     *
     * Signals state machine to stop sending more recoded packets.
     */
    void add_ack_packet();

    /**
     * process() - Check recoder status and take necessary actions.
     *
     * Checks if recoder is done coding or has timed out.
     *
     * Returns true if the decoder is not needed anymore; false otherwise.
     */
    bool process();

    /**
     * is_valid() - return if recoder is open for more packets
     */
    bool is_valid()
    {
        return curr_state() == STATE_WAIT;
    }
};

typedef full_rlnc_recoder_deep<rlnc_field> recoder;

#endif
