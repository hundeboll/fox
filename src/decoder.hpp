/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_DECODER_HPP_
#define FOX_DECODER_HPP_

#include <kodo/rlnc/full_vector_codes.hpp>
#include <vector>

#include "coder.hpp"
#include "systematic_decoder.hpp"

/**
 * class decoder - decoder based on the kodo library.
 */
template<class Field>
class full_rlnc_decoder_deep
    : public
             // Payload API
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
             final_coder_factory<
             // Final type
             full_rlnc_decoder_deep<Field>
                 > > > > > > > > > > > > > > >, public coder
{
    std::vector<bool> m_decoded_symbols;
    std::atomic<size_t> m_enc_pkt_count, m_red_pkt_count;
    size_t m_req_seq;

    /**
     * enum m_state - states that this decoder can reside in
     * @STATE_INVALID:   inherited state that should never be entered
     * @STATE_WAIT:      inherited (intial) state when waiting for the first event
     * @STATE_DONE:      inherited (final) state to enter when decoding is done
     * @STATE_WRITE_DEC: state to enter when generation is decoded and packets should be written
     * @STATE_ACKED:     state to enter when packets have been written and possible more encoded
     *                    packets could arrive
     * @STATE_NUM:       counter for the number of states in this class
     */
    enum m_state : state_type {
        STATE_INVALID = __STATE_INVALID,
        STATE_WAIT = __STATE_WAIT,
        STATE_DONE = __STATE_DONE,
        STATE_WRITE_DEC,
        STATE_ACKED,
        STATE_NUM
    };

    /**
     * enum m_event - event that can change the state of this decoder
     * @EVENT_COMPLETE: generation is decoded
     * @EVENT_ACKED:    generation has been acked to encoder
     * @EVENT_TIMEOUT:  generation has timed out
     * @EVENT_DONE:     generation hs nothing more to do
     * @EVENT_NUM:      number of events in this decoder
     */
    enum m_event : event_type {
        EVENT_COMPLETE,
        EVENT_ACKED,
        EVENT_TIMEOUT,
        EVENT_DONE,
        EVENT_NUM
    };

    /**
     * send_plain_packet() - Write decoded packet with index i.
     * @param i Index of decoded packet to write.
     *
     * Reads out the length field from the decoded packets to construct
     * the header and then copies out the payload before writing the
     * packet.
     *
     * Also checks if the specified packet has already been written and
     * skips it if so.
     */
    void send_decoded_packet(size_t i);

    /**
     * send_plain_packets() - Write decoded packets to batman-adv.
     *
     * Write an acknowledgement packet to upstream and write the
     * decoded generation to batman-adv.
     *
     * Signals to the state-machine when done.
     */
    void send_decoded_packets();

    void send_partial_decoded_packets(size_t rank);

    void send_request(size_t seq);

  public:
    /**
     * full_rlnc_decoder_deep() - Construct decoder
     *
     * Allocates packet buffers and initalizes the state machine.
     */
    full_rlnc_decoder_deep()
    {
        handler_func dec, ack;

        /* prepare the state tables */
        states::init(m_coder, STATE_NUM, EVENT_NUM);

        /* add states to table */
        dec = std::bind(&full_rlnc_decoder_deep::send_decoded_packets, this);
        ack = std::bind(&full_rlnc_decoder_deep::wait, this);

        add_state(STATE_WRITE_DEC, dec);
        add_state(STATE_ACKED, ack);

        /* add transitions to state table */
        add_trans(STATE_WAIT, EVENT_TIMEOUT, STATE_DONE);
        add_trans(STATE_WAIT, EVENT_COMPLETE, STATE_WRITE_DEC);
        add_trans(STATE_WRITE_DEC, EVENT_ACKED, STATE_ACKED);
        add_trans(STATE_ACKED, EVENT_TIMEOUT, STATE_DONE);
        add_trans(STATE_DONE, EVENT_COMPLETE, STATE_DONE);
    }

    /**
     * init() - setup packet headers and reset object.
     *
     * For each buffer there is a packet header assigned, which is
     * initialized with constant values
     *
     * Must be called immediately after construction of class and before
     * every reuse of the object.
     */
    void init();

    /**
     * add_enc_packet() - Add encoded symbol to decoder
     * @param pkt encoded packet
     *
     * Data from encoded packet is copied into decoder. Timestamp and packet
     * counter are updated.
     *
     * Signals the state machine if the decoding is complete.
     */
    void add_enc_packet(const uint8_t *data, const uint16_t len);

    /**
     * process() - Check decoder status and take necessary actions.
     *
     * Check if the decoder is done or timed out and signal the state
     * machine if needed.
     *
     * Returns true if the decoder is not needed anymore; false otherwise.
     */
    bool process();

    /**
     * is_valid() - Return if decoder is still open for more packets
     */
    virtual bool is_valid()
    {
        return curr_state() == STATE_WAIT;
    }
};

typedef full_rlnc_decoder_deep<rlnc_field> decoder;

#endif
