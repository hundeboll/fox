/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_HELPER_HPP_
#define FOX_HELPER_HPP_

#include <string>

#include "kodo/rlnc/full_vector_codes.hpp"

#include "coder.hpp"
#include "systematic_decoder.hpp"

DECLARE_double(helper_threshold);
DECLARE_double(fixed_overshoot);

/**
 * class helper - recoder class to assist one-hop links
 */
template<class Field>
class full_rlnc_helper_deep
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
             full_rlnc_helper_deep<Field>
                 > > > > > > > > > > > > > > > >, public coder
{
    std::atomic<size_t> m_hlp_pkt_count, m_enc_pkt_count;
    ssize_t m_max_budget, m_threshold;
    double m_budget, m_credit;
    uint8_t e1, e2, e3;

    /**
     * enum _state - states that this helper can reside in
     * @STATE_INVALID:    inherited state that should never be entered
     * @STATE_WAIT:       inherited (intial) state when waiting for the first event
     * @STATE_DONE:       inherited (final) state to enter when encoding is done
     * @STATE_SEND_HLP:   sends encoded packets until the generation is acked
     * @STATE_NUM:        counter for the number of states in this class
     */
    enum m_state : state_type {
        STATE_INVALID = __STATE_INVALID,
        STATE_WAIT = __STATE_WAIT,
        STATE_DONE = __STATE_DONE,
        STATE_NUM
    };

    /**
     * enum _event - event that can change the state of this decoder
     * @EVENT_GO:      generation is ready to be recoded
     * @EVENT_TIMEOUT: generation has timed out
     * @EVENT_ACKED:   generation has been acked by next hop
     * @EVENT_NUM:     number of events in this encoder
     */
    enum m_event : event_type {
        EVENT_ACKED,
        EVENT_BUDGET_SENT,
        EVENT_TIMEOUT,
        EVENT_NUM
    };

    /**
     * _write_hlp_packet() - Write one recoded packet to batman-adv.
     *
     * Reads one recoded packet from the recoder and writes it to
     * batman-adv.
     */
    void send_hlp_packet();

    /**
     * _write_hlp_packets() - Write recoded packets to assist a link.
     *
     * Write recoded packets until the generation is acked by the
     * next hop.
     */
    void send_hlp_credits();

    size_t max_budget()
    {
        size_t nom, denom, g, r;

        if (e1 == ONE - 1 || e2 == ONE - 1 || e3 == ONE - 1) {
            std::string e;

            if (e1 == ONE - 1)
                e = "e1";

            if (e2 == ONE - 1)
                e = "e2";

            if (e3 == ONE - 1)
                e = "e3";

            VLOG(LOG_GEN) << "Helper " << m_coder
                          << ": Missing link estimate (" << e << ")";

            return this->symbols() / 2;
        }

        g = this->symbols();
        r = r_val(g, e1, e2, e3);
        nom = e3*r - r*ONE + g*ONE;
        denom = 2*ONE - e3 - e2;

        return FLAGS_fixed_overshoot*nom/denom + (nom % denom != 0);
    }

    void update_budget()
    {
        m_budget = m_budget + credit();
    }

    size_t get_threshold()
    {
        size_t g, r;

        if (e1 == ONE - 1 || e2 == ONE - 1 || e3 == ONE - 1)
            return this->symbols() / 2;

        g = this->symbols();
        r = r_val(g, e1, e2, e3);

        return (r - r*e1/ONE)*FLAGS_helper_threshold;
    }

    double credit()
    {
        if (e1 == ONE - 1 || e2 == ONE - 1 || e3 == ONE - 1)
            return 1;

        return static_cast<double>(ONE)/(ONE - e1);
    }

  public:
    /**
     * full_rlnc_helper_deep() - Construct a helper object
     *
     * Allocates packet buffers and initializes the state machine.
     */
    full_rlnc_helper_deep()
    {
        /* prepare state tables */
        states::init(m_coder, STATE_NUM, EVENT_NUM);

        /* add allowed transitions to the state table */
        add_trans(STATE_WAIT, EVENT_TIMEOUT, STATE_DONE);
        add_trans(STATE_WAIT, EVENT_ACKED, STATE_DONE);
        add_trans(STATE_WAIT, EVENT_BUDGET_SENT, STATE_DONE);
        add_trans(STATE_DONE, EVENT_ACKED, STATE_DONE);
        add_trans(STATE_DONE, EVENT_BUDGET_SENT, STATE_DONE);
    }

    /**
     * init() - setup packet headers and reset internal state
     *
     * For each buffer a packet header is assigned, which is initialized
     * with constant values.
     *
     * State machine is reset to init state and counters are zeroed.
     *
     * Must be called immediately after construction of class.
     */
    void init();

    /**
     * add_hlp_packet() - Add encoded packet for helping one-hops.
     * @param pkt Encoded packet to be recoded.
     *
     * Add the encoded packet to the decoder and signal state machine
     * if sending of recoded packets should start.
     */
    void add_enc_packet(const uint8_t *data, const uint16_t len);

    /**
     * add_ack_packet() - react on received acknowledgement
     *
     * Signals state machine that an acknowledgement is received.
     */
    void add_ack_packet();

    void add_req_packet(const uint16_t rank, const uint16_t seq);

    /**
     * process() - Check decoder status and take necessary actions.
     *
     * Check the state of the helper and return it to the caller.
     *
     * Returns true if the decoder is not needed anymore; false otherwise.
     */
    bool process();
};

typedef full_rlnc_helper_deep<rlnc_field> helper;

#endif
