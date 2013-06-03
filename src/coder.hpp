/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_CODER_HPP_
#define FOX_CODER_HPP_

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "fox.hpp"
#include "timeout.hpp"
#include "key.hpp"
#include "io.hpp"
#include "counters.hpp"
#include "states.hpp"
#include "semaphore.hpp"

typedef fifi::binary8 rlnc_field;
typedef fifi::binary16 rlnc_field2;

using namespace kodo;

static size_t coder_num = 0;

DECLARE_double(fixed_overshoot);
DECLARE_int32(e1);
DECLARE_int32(e2);
DECLARE_int32(e3);

/**
 * class coder - collection of general classes for coder classes
 *
 * This class includes other classes used by all (en-, re-, de-, and helper)coders
 * and defines a functiont o write acknowledgements.
 */
class coder
    : public timeout,
      public key_api,
      public io_api,
      public counter_api,
      public states,
      public semaphore_api
{
  protected:
    uint8_t m_e1, m_e2, m_e3;
    size_t m_coder;
    std::mutex m_lock;
    size_t ONE = {255};

    /**
     * coder() - Constructor to setup ACK packet.
     *
     * Also increases the static coder counter to identify coders across
     * uses from the factory pool.
     */
    coder() : m_coder(coder_num++)
    {
        guard g(m_lock);
        VLOG(LOG_OBJ) << "Coder " << m_coder << ": Constructed";
        m_e1 = FLAGS_e1*2.55;
        m_e2 = FLAGS_e2*2.55;
        m_e3 = FLAGS_e3*2.55;
    }

    /**
     * send_ack_packet() - write acknowledgement packet to batman-adv.
     */
    void send_ack_packet()
    {
        struct nl_msg *msg;

        msg = nlmsg_alloc();
        genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, m_io->genl_family(),
                    0, 0, BATADV_HLP_C_FRAME, 1);

        nla_put_u32(msg, BATADV_HLP_A_IFINDEX, m_io->ifindex());
        nla_put(msg, BATADV_HLP_A_SRC, ETH_ALEN, _key.src);
        nla_put(msg, BATADV_HLP_A_DST, ETH_ALEN, _key.dst);
        nla_put_u16(msg, BATADV_HLP_A_BLOCK, _key.block);
        nla_put_u8(msg, BATADV_HLP_A_TYPE, ACK_PACKET);
        nla_put_u16(msg, BATADV_HLP_A_INT, 0);

        m_io->send_msg(msg);
        nlmsg_free(msg);

        inc("ack sent");
        VLOG(LOG_CTRL) << "Coder " << m_coder << ": Sent ACK packet";
    }

    bool r_test(uint8_t e1, uint8_t e2, uint8_t e3)
    {
        return (ONE - e2) < (e3 - e1*e3/ONE);
    }

    size_t r_val(size_t g, uint8_t e1, uint8_t e2, uint8_t e3)
    {
        size_t nom, denom;

        if (r_test(e1, e2, e3)) {
            denom = e3 - e1*e3/ONE ? : ONE;
            return ONE/denom + (ONE % denom != 0);
        } else {
            nom = ONE*g - g*e2 - g*e3 + g*e1*e3/ONE;
            denom = ONE + e1*e3*e2/ONE/ONE - e2 - e1*e3/ONE ? : ONE;
            return nom/denom + (nom % denom != 0);
        }
    }

    double source_budget(size_t g, uint8_t e1, uint8_t e2, uint8_t e3)
    {
        double nom, denom;
        size_t r = r_val(g, e1, e2, e3);

        if (e3 >= ONE - 1) {
            VLOG(LOG_GEN) << "Encoder " << m_coder << ": Missing link estimate";
            return FLAGS_fixed_overshoot*g;
        }

        nom = g*ONE + r*ONE - r*e2;
        denom = 2*ONE - e3 - e2 ? : ONE;

        return FLAGS_fixed_overshoot*nom/denom;
    }

    size_t recoder_budget(size_t g, uint8_t e1, uint8_t e2, uint8_t e3)
    {
        size_t nom, denom, r = r_val(g, e1, e2, e3);

        nom = g*ONE + r*ONE - r*e2;
        denom = 2*ONE - e3 - e2;

        return nom/denom + (nom % denom != 0);
    }

    double recoder_credit(size_t e1, size_t e2, size_t e3)
    {
        size_t denom = ONE - e3*e1/ONE;
        return static_cast<double>(ONE)/denom;
    }

  public:
    size_t num()
    {
        return m_coder;
    }

    virtual bool is_valid()
    {
        return true;
    }
};

#endif
