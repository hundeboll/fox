/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_MAIN_HPP_
#define FOX_MAIN_HPP_

#include <stdint.h>
#include <stddef.h>
#include <mutex>

#include <glog/logging.h>
#include <gflags/gflags.h>

enum verbose_log_levels {
    LOG_NONE    = 0,
    LOG_GEN     = 1,
    LOG_CTRL    = 2,
    LOG_PKT     = 3,
    LOG_NL      = 4,
    LOG_OBJ     = 5,
    LOG_STATE   = 6,
};

typedef std::lock_guard<std::mutex> guard;

#define FOX_DEFAULT_PATH "/sys/kernel/debug/batman_adv/bat0/rlnc"
#define RLNC_MIN_PACKET_LEN 1602
#define ETH_ALEN 6
#define RLNC_MAX_PAYLOAD (1550 - 18 - 14)

bool handle_packet(const uint8_t type, const struct key &k, const uint8_t *data,
                   const uint16_t len, const uint16_t rank, const uint16_t seq);

#endif
