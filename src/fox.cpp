/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#include <gflags/gflags.h>
#include <gflags/gflags_completions.h>
#include <signal.h>
#include <mcheck.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <string>

#include "fox.hpp"
#include "io.hpp"
#include "key.hpp"
#include "coder_map.hpp"
#include "encoder.hpp"
#include "decoder.hpp"
#include "recoder.hpp"
#include "helper.hpp"
#include "counters.hpp"


DEFINE_string(device, "bat0", "Virtual interface from batman-adv");
DEFINE_int32(generation_size, 64, "The generation size, the number of packets "
                                  "which are coded together.");
DEFINE_int32(packet_size, 1454, "The payload size without RLNC overhead.");
DEFINE_double(packet_timeout, .3, "The number of averaged inter-packet "
                                       "arrival times to wait for more data");
DEFINE_double(encoder_timeout, 1, "Time to wait for more packets before "
                                  "dropping encoder generation.");
DEFINE_double(decoder_timeout, 2, "Time to wait for more packets before "
                                  "dropping decoder generation.");
DEFINE_double(recoder_timeout, 2, "Time to wait for more packets before "
                                  "dropping recoder generation.");
DEFINE_double(helper_timeout, 1, "Time to wait for more packets before "
                                 "dropping helper generation.");
DEFINE_double(fixed_overshoot, 1.06, "Fixed factor to increase "
                                     "encoder/recoder budgets.");
DEFINE_int32(encoders, 2, "Number of concurrent encoder.");
DEFINE_int32(e1, 10, "Error probability from source to helper in percentage.");
DEFINE_int32(e2, 10, "Error probability from helper to dest in percentage.");
DEFINE_int32(e3, 30, "Error probability from source to dest in percentage.");
DEFINE_int32(ack_interval, 3, "Number of redundant packets to receive before"
                              "repeating an ACK packet.");
DEFINE_double(helper_threshold, 1.0, "Ratio to multiply with helper"
                                     "threshold.");
DEFINE_bool(systematic, true, "Use systematic packets when encoding packets");
DEFINE_double(encoder_threshold, 0.1, "Threshold ratio to start sending credits");
DEFINE_bool(benchmark, false, "Disable any coding done by fox to test raw performance.");

static std::mutex exit_lock;
static std::atomic<bool> running(true), quit(false);
io::pointer io;
counters::pointer counts;
typedef coder_map<key, encoder> encoder_map;
typedef coder_map<key, decoder> decoder_map;
typedef coder_map<key, recoder> recoder_map;
typedef coder_map<key, helper> helper_map;
encoder_map::pointer enc_map;
decoder_map::pointer dec_map;
recoder_map::pointer rec_map;
helper_map::pointer hlp_map;

/**
 * house_keeping_thread() - Visit each coder_map to process coders.
 *
 * Call the process_coders() function in each coder_map to handle timed out
 * coders.
 */
void house_keeping_thread()
{
    std::chrono::milliseconds interval(50);

    while (running) {
        std::this_thread::sleep_for(interval);
        enc_map->process_coders();
        dec_map->process_coders();
        rec_map->process_coders();
        hlp_map->process_coders();
    }
}

/**
 * handle_packet() - Process read packet based on type.
 * @param hdr pointer to header of the read packet.
 *
 * Based on the type of the passed packet header, this function receives either
 * encoder or decoder from the respective coder_map and passes the packet to
 * coder.
 */
bool handle_packet(const uint8_t type, const struct key &k, const uint8_t *data,
                   const uint16_t len, const uint16_t rank, const uint16_t seq)
{
    decoder::pointer d;
    encoder::pointer e;
    recoder::pointer r;
    helper::pointer h;

    switch (type) {
        case PLAIN_PACKET:
            e = enc_map->get_latest_coder(k);
            if (!e)
                break;
            e->add_plain_packet(data, len);
            break;

        case ENC_PACKET:
            d = dec_map->get_coder(k);
            if (!d)
                break;
            d->add_enc_packet(data, len);
            break;

        case REC_PACKET:
            r = rec_map->get_coder(k);
            if (!r)
                break;
            r->add_enc_packet(data, len);
            break;

        case HLP_PACKET:
            h = hlp_map->get_coder(k);
            if (!h)
                break;
            h->add_enc_packet(data, len);
            break;

        case ACK_PACKET:
            e = enc_map->find_coder(k);
            if (e) {
                e->add_ack_packet();
                break;
            }

            r = rec_map->find_coder(k);
            if (r) {
                r->add_ack_packet();
                break;
            }

            h = hlp_map->find_coder(k);
            if (h) {
                h->add_ack_packet();
                break;
            }
            break;

        case REQ_PACKET:
            e = enc_map->find_coder(k);
            if (e) {
                e->add_req_packet(rank, seq);
                break;
            }

            h = hlp_map->find_coder(k);
            if (h) {
                h->add_req_packet(rank, seq);
                break;
            }
            break;

        default:
            LOG(ERROR) << "Unknown packet type: " << type;
            return false;
    }

    return true;
}

/**
 * sigint() - handle SIGINT signal by telling threads to quit
 */
void sigint(int signal)
{
    running = false;

    /* force exit on second signal */
    if (quit)
        exit(1);
    else
        quit = true;
}

/**
 * sighup() - handle SIGHUP signal by printing current counter values
 */
void sigquit(int signal)
{
    counts->print();
}

/**
 * main() - entry function for fox
 */
int main(int argc, char **argv)
{
    mtrace();

    std::string usage("Encode and decode packets with Random Linear "
                      "Network Coding\n");

    FLAGS_logtostderr = true;
    FLAGS_colorlogtostderr = true;
    google::HandleCommandLineCompletions();
    google::SetUsageMessage(usage);
    google::ParseCommandLineFlags(&argc, &argv, true);

    google::InitGoogleLogging(argv[0]);
    google::InstallFailureSignalHandler();

    /* connect signal handlers */
    signal(SIGINT, sigint);
    signal(SIGTERM, sigint);
    signal(SIGQUIT, sigquit);

    uint32_t symbols = FLAGS_generation_size;
    uint32_t symbol_size = FLAGS_packet_size;

    LOG_IF(FATAL, (symbols + symbol_size) > RLNC_MAX_PAYLOAD)
        << "Payload size exceeds MTU: " << (symbols + symbol_size) << " > "
        << RLNC_MAX_PAYLOAD << std::endl << "Try with " << argv[0]
        << " --packet_size=" << (RLNC_MAX_PAYLOAD - symbols) << std::endl;

    srand(static_cast<uint32_t>(time(0)));

    /* create io object depending on whether one or two files should be used */
    io = io::pointer(new class io());
    CHECK(io->open()) << "Failed to open IO";

    /* create counter and map objects */
    semaphore enc_sem(FLAGS_encoders);
    counts = counters::pointer(new counters());
    enc_map = encoder_map::pointer(new encoder_map(symbols, symbol_size));
    dec_map = decoder_map::pointer(new decoder_map(symbols, symbol_size));
    rec_map = recoder_map::pointer(new recoder_map(symbols, symbol_size));
    hlp_map = helper_map::pointer(new helper_map(symbols, symbol_size));

    /* fabricate objects */
    io->set_counts(counts);

    enc_map->set_semaphore(&enc_sem);
    enc_map->set_counts(counts);
    enc_map->set_io(io);

    dec_map->set_counts(counts);
    dec_map->set_io(io);

    rec_map->set_counts(counts);
    rec_map->set_io(io);

    hlp_map->set_counts(counts);
    hlp_map->set_io(io);

    /* start house keeping thread and start reading packets */
    std::thread house_keeping(house_keeping_thread);

    /* wait for thread to finish */
    house_keeping.join();
    counts->print();
    io.reset();

    muntrace();

    return EXIT_SUCCESS;
}
