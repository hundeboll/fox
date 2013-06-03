/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef SEMAPHORE_HPP
#define SEMAPHORE_HPP

#include <mutex>
#include <atomic>
#include <condition_variable>

class semaphore
{
  private:
    std::mutex m_mutex;
    std::deque<std::condition_variable*> m_queue;
    ssize_t m_count, m_wakeups;

  public:
    semaphore() : m_count(0), m_wakeups(0)
    {}

    explicit semaphore(size_t c) : m_count(c), m_wakeups(0)
    {}

    void wait()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_count--;

        if (m_count < 0) {
            std::condition_variable *cond_var(new std::condition_variable);
            m_queue.push_back(cond_var);
            cond_var->wait(lock, [this]() { return m_wakeups > 0; });
            m_wakeups--;
        }
    }

    void notify()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_count++;

        if (m_count <= 0) {
            m_wakeups++;
            std::condition_variable *cond_var(m_queue.front());
            m_queue.pop_front();
            cond_var->notify_one();
            delete cond_var;
        }
    }

    ssize_t count() const
    {
        return m_count;
    }

    friend std::ostream &operator <<(std::ostream &o, const semaphore &s)
    {
        o << s.m_count;
        return o;
    }
};

class semaphore_api
{
    semaphore *m_sem;

  protected:
    void semaphore_wait()
    {
        if (m_sem)
            m_sem->wait();
    }

    void semaphore_notify()
    {
        if (m_sem)
            m_sem->notify();
    }

    ssize_t semaphore_count()
    {
        return m_sem ? m_sem->count() : 0;
    }

    bool has_semaphore()
    {
        return (m_sem != NULL);
    }

    semaphore *get_semaphore()
    {
        return m_sem;
    }

  public:
    void set_semaphore(semaphore *sem)
    {
        m_sem = sem;
    }

    semaphore_api() : m_sem(NULL)
    {}
};

#endif
