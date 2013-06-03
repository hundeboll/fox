/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_TIMEOUT_HPP_
#define FOX_TIMEOUT_HPP_

#include <chrono>
#include <algorithm>
#include <boost/circular_buffer.hpp>

/**
 * class timeout - API used by coders to handle time.
 */
class timeout {
    typedef std::chrono::high_resolution_clock timer;
    typedef timer::time_point timestamp;

    timestamp m_timestamp, m_last;
    double m_timeout, m_pkt_timeout;

  protected:
    void update_timestamp()
    {
        m_timestamp = timer::now();
    }

    void update_packet_timestamp()
    {
        m_last = timer::now();
    }

  public:
    timeout()
    {}

    void init_timeout(const double t)
    {
        m_last = timer::now();
        m_timestamp = timer::now();
        m_timeout = t;
    }

    void set_pkt_timeout(double f)
    {
        m_pkt_timeout = f;
    }

    /**
     * is_timed_out() - Check if timer is timed out.
     */
    bool check_timeout(const timestamp &ts, double t) const
    {
        using std::chrono::duration;
        using std::chrono::duration_cast;

        duration<double> diff;
        timestamp now = timer::now();
        diff = duration_cast<duration<double>>(now - ts);

        return (diff.count() > t);
    }

    bool is_timed_out(double t) const
    {
        return check_timeout(m_timestamp, t);
    }

    bool is_timed_out() const
    {
        return check_timeout(m_timestamp, m_timeout);
    }

    bool packet_timed_out()
    {
        return check_timeout(m_last, m_pkt_timeout);
    }
};

#endif
