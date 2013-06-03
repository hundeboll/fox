/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef _FOX_STATES_HPP_
#define _FOX_STATES_HPP_

#include <thread>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <functional>
#include <vector>

/**
 * class states - state machine to use in coders
 */
class states
{
    std::condition_variable m_cond_var;
    std::mutex m_cond_lock, m_event_lock;
    typedef std::unique_lock<std::mutex> unique_lock;
    std::atomic<bool> m_running;
    std::thread m_state_thread;
    size_t m_coder_num;

    /**
     * thread_func() - main loop for state thread
     */
    void thread_func()
    {
        while (m_running) {
            m_state_table[m_curr_state]();
            m_curr_state = m_next_state.load();
        }
    }

    /**
     * _stop_waiting() - signal _wait() to stop waiting
     */
    void stop_waiting()
    {
        guard g(m_cond_lock);
        m_cond_var.notify_one();
    }

    void invalid()
    {
        LOG(FATAL) << "Coder " << m_coder_num << ": Entered invalid state";
    }

  protected:
    typedef std::function<void ()> handler_func;
    typedef uint8_t state_type;
    typedef uint8_t event_type;
    enum { __STATE_INVALID = 0, __STATE_WAIT, __STATE_DONE, __STATE_NUM };
    enum { __EVENT_NUM };

    /**
     * states() - construct new state machine object
     *
     * Does an initial allocation for state table and transition table and starts
     * the local thread for changing and running states.
     */
    states() :
        m_running(true),
        m_curr_state(__STATE_WAIT),
        m_next_state(__STATE_WAIT),
        m_state_table(__STATE_NUM)
    {
        /* add default states */
        add_state(__STATE_WAIT, std::bind(&states::wait, this));
        add_state(__STATE_DONE, std::bind(&states::wait, this));
        add_state(__STATE_INVALID, std::bind(&states::invalid, this));

        /* start state thread */
        m_state_thread = std::thread(&states::thread_func, this);
    }

    /**
     * ~states() - destruct state machine by stopping thread and wait for it
     */
    ~states()
    {
        VLOG(LOG_OBJ) << "Coder " << m_coder_num << ": Destructed (state "
                      << static_cast<int>(m_curr_state) << ", next "
                      << static_cast<int>(m_next_state) << ")";
        m_running = false;
        stop_waiting();
        m_state_thread.join();
    }

    /**
     * wait() - wait until signal arrives
     */
    void wait()
    {
        unique_lock l(m_cond_lock);

        while (m_curr_state == m_next_state && m_running)
            m_cond_var.wait(l);
    }

    /* init() - reallocate state and transition tables
     * s_num: number of states to prepare for
     * e_num: number of events for prepare for
     *
     * Resizes the state and transition tables to the passed sizes and
     * initializes all transitions as invalid. Use _add_state() to add
     * states and use _add_trans() to add non-invalid transitions.
     */
    void init(size_t coder_num, state_type s_num, event_type e_num)
    {
        guard g(m_event_lock);

        m_coder_num = coder_num;
        m_state_table.resize(s_num);
        m_trans_table.resize(s_num, std::vector<state_type>(e_num,
                                                            __STATE_INVALID));
    }

    /**
     * add_state() - add a new state to the state table
     * s: number for the added state
     * handler: function to call when entering the state
     */
    void add_state(state_type s, handler_func handler)
    {
        guard g(m_event_lock);

        m_state_table[s] = handler;
    }

    /**
     * add_trans() - mark a transition as valid
     * from: state to come from
     * event: event to change state
     * to: state to enter when receiving the passed event
     *
     * Overrides the entry in the transition table at the passed state and event
     * with the state to enter.
     */
    void add_trans(state_type from, event_type event, state_type to)
    {
        guard g(m_event_lock);

        m_trans_table[from][event] = to;
    }

    /**
     * dispatch_event() - signal state machine to change state
     * event: ID of event to dispatch
     *
     * Reads the next state from the transition table and checks if it is
     * valid and sets the next state accordingly, before signaling the state
     * thread to stop waiting.
     */
    void dispatch_event(event_type event)
    {
        guard g(m_event_lock);

        if (m_curr_state.load() != m_next_state.load())
            return;

        /* read next state */
        m_next_state = m_trans_table[m_curr_state][event];

        /* check if next state if valid */
        if (m_next_state == __STATE_INVALID) {
            LOG(ERROR) << "Coder " << m_coder_num
                        << ": Invalid event: current state "
                        << static_cast<int>(m_curr_state)
                        << ", event: " << static_cast<int>(event);
            m_next_state = __STATE_DONE;
        }

        VLOG(LOG_STATE) << "Coder " << m_coder_num
                        << ": Event: " << static_cast<int>(event)
                        << ", from state: " << static_cast<int>(m_curr_state)
                        << ", to state: " << static_cast<int>(m_next_state);

        /* change to next state */
        stop_waiting();
    }

    /**
     * set_state() - change to a state without using an event
     * s: state to enter
     */
    void set_state(state_type s)
    {
        guard g(m_event_lock);

        m_next_state = s;
        stop_waiting();
    }

  public:
    state_type curr_state()
    {
        guard g(m_event_lock);
        return m_curr_state;
    }

    /**
     * state() - return current state
     */
    state_type next_state()
    {
        guard g(m_event_lock);
        return m_next_state;
    }

  private:
    std::atomic<state_type> m_curr_state, m_next_state;
    std::vector<handler_func> m_state_table;
    std::vector<std::vector<state_type>> m_trans_table;
};

#endif
