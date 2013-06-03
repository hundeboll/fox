/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_ENCODER_HPP_
#define FOX_ENCODER_HPP_

#include <netlink/netlink.h>
#include <netlink/genl/genl.h>

#include <boost/make_shared.hpp>

#include <kodo/rlnc/full_vector_codes.hpp>
#include <kodo/storage_aware_generator.hpp>
#include <kodo/shallow_symbol_storage.hpp>

#include "coder.hpp"

DECLARE_bool(systematic);

/**
 * class encoder - RLNC encoder based on the KODO library
 */
template<class Field>
class full_rlnc_encoder_deep
    : public
           // Payload Codec API
           payload_encoder<
           // Codec Header API
           systematic_encoder<
           symbol_id_encoder<
           // Symbol ID API
           plain_symbol_id_writer<
           // Coefficient Generator API
           storage_aware_generator<
           uniform_generator<
           // Codec API
           encode_symbol_tracker<
           zero_symbol_encoder<
           linear_block_encoder<
           storage_aware_encoder<
           // Coefficient Storage API
           coefficient_info<
           // Symbol Storage API
           mutable_shallow_symbol_storage<
           storage_bytes_used<
           storage_block_info<
           // Finite Field API
           finite_field_math<typename fifi::default_field<Field>::type,
           finite_field_info<Field,
           // Factory API
           final_coder_factory<
           // Final type
           full_rlnc_encoder_deep<Field
               > > > > > > > > > > > > > > > > > >, public coder
{
    size_t m_enc_pkt_count, m_plain_pkt_count, m_last_req_seq;
    volatile double m_budget, m_max_budget;
    uint8_t *m_symbol_storage;
    uint8_t m_type;

    /**
     * enum _state - states that this decoder can reside in
     * @STATE_INVALID:    inherited state that should never be entered
     * @STATE_WAIT:       inherited (intial) state when waiting for the first event
     * @STATE_DONE:       inherited (final) state to enter when encoding is done
     * @STATE_SEND_ENC:   sends encoded packets until the generation is acked
     * @STATE_NUM:        counter for the number of states in this class
     */
    enum m_state : state_type {
        STATE_INVALID = __STATE_INVALID,
        STATE_WAIT = __STATE_WAIT,
        STATE_DONE = __STATE_DONE,
        STATE_FULL,
        STATE_SEND_BUDGET,
        STATE_WAIT_ACK,
        STATE_NUM
    };

    /**
     * enum _event - event that can change the state of this decoder
     * @EVENT_FULL:    generation is ready to be encoded
     * @EVENT_TIMEOUT: generation has timed out
     * @EVENT_ACKED:   generation has been acked by next hop
     * @EVENT_DONE:    generation hs nothing more to do
     * @EVENT_NUM:     number of events in this encoder
     */
    enum m_event : event_type {
        EVENT_FULL,
        EVENT_START,
        EVENT_BUDGET_SENT,
        EVENT_ACKED,
        EVENT_TIMEOUT,
        EVENT_NUM
    };

    void enc_wait()
    {
        block_packets(BATADV_HLP_C_BLOCK);
        semaphore_wait();
        dispatch_event(EVENT_START);
        update_timestamp();
    }

    void enc_notify()
    {
        block_packets(BATADV_HLP_C_UNBLOCK);
        semaphore_notify();
    }

    /**
     * _write_enc_packet() - function encode and write a single packet
     */
    void send_encoded_packet(uint8_t type);

    /**
     * _write_enc_packets() - Write encoded packets to batman-adv.
     *
     * Writes symbols + redundants number of encoded packets using the member
     * buffer.
     */
    void send_encoded_budget();

    void send_encoded_credit();

    void block_packets(int block_cmd);

    uint8_t *get_symbol_buffer(size_t i)
    {
        return m_symbol_storage + i * this->symbol_size();
    }

  public:
    /**
     * full_rlnc_encoder_deep() - Construct encoder class
     *
     * Allocates packet buffers and initializes the state machine.
     */
    full_rlnc_encoder_deep() : m_symbol_storage(NULL)
    {
        handler_func full, enc, ack, cre;

        /* prepare state tables */
        states::init(m_coder, STATE_NUM, EVENT_NUM);

        /* add states to the state table */
        full = std::bind(&full_rlnc_encoder_deep::enc_wait, this);
        enc = std::bind(&full_rlnc_encoder_deep::send_encoded_budget, this);
        ack = std::bind(&full_rlnc_encoder_deep::wait, this);

        add_state(STATE_FULL, full);
        add_state(STATE_SEND_BUDGET, enc);
        add_state(STATE_WAIT_ACK, ack);

        /* add allowed transitions between states */
        add_trans(STATE_WAIT, EVENT_FULL, STATE_FULL);
        add_trans(STATE_WAIT, EVENT_TIMEOUT, STATE_DONE);
        add_trans(STATE_WAIT, EVENT_ACKED, STATE_DONE);
        add_trans(STATE_FULL, EVENT_START, STATE_SEND_BUDGET);
        add_trans(STATE_FULL, EVENT_ACKED, STATE_DONE);
        add_trans(STATE_SEND_BUDGET, EVENT_BUDGET_SENT, STATE_WAIT_ACK);
        add_trans(STATE_SEND_BUDGET, EVENT_ACKED, STATE_DONE);
        add_trans(STATE_WAIT_ACK, EVENT_ACKED, STATE_DONE);
        add_trans(STATE_WAIT_ACK, EVENT_TIMEOUT, STATE_DONE);

        if (!FLAGS_systematic)
            this->set_systematic_off();
    }

    ~full_rlnc_encoder_deep()
    {
        if (m_symbol_storage)
            delete[] m_symbol_storage;
    }

    /**
     * init() - Allocate member buffers and setup packet headers.
     *
     * Initializes buffers used when encoding and writing back packets.
     * For each buffer there is a packet header assigned, which is
     * initialized with constant values.
     *
     * Reset the state of the encoder to STATE_WAIT and reset internal
     * counters.
     *
     * Must be called immediately after building the class object.
     */
    void init();

    /**
     * add_plain_packet() - Add uncoded packet to encoder.
     * @param p Packet with plain data.
     *
     * Prepends the plain data with a length field and copies it to the
     * encoder. Timestamp and packet counter are updated.
     */
    void add_plain_packet(const uint8_t *data, const uint16_t len);

    /**
     * add_ack_packet() - add aknowledgement packet to encoder
     *
     * Signals the state machine that next hop has acked the generation.
     */
    void add_ack_packet();

    void add_req_packet(const uint16_t rank, const uint16_t seq);

    /**
     * process() - Check encoder status and take necessary actions.
     *
     * Check if encoder is done working or timed out and returns this
     * to the caller.
     *
     * Returns true if the encoder is not needed anymore; false otherwise.
     */
    bool process();

    /**
     * is_valid() - return if decoder is in a state to accept plain packets
     */
    virtual bool is_valid()
    {
        return !is_full();
    }

    /**
     * is_full() - Return if encoder has enough packets to encode.
     */
    bool is_full()
    {
        return m_plain_pkt_count >= this->symbols();
    }
};

typedef full_rlnc_encoder_deep<rlnc_field> encoder;
typedef full_rlnc_encoder_deep<rlnc_field2> encoder2;

#endif
