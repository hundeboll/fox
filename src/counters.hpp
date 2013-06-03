/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_COUNTERS_HPP_
#define FOX_COUNTERS_HPP_

#include "fox.hpp"
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <iostream>
#include <string>
#include <functional>
#include <mutex>

#define SHM_NAME "/fox_shared_memory"
#define SHM_MAP_NAME "counters"

using namespace boost::interprocess;


/**
 * class counters - generic counter interface to create, increment, and print counters.
 */
class counters {
    struct shm_remove
    {
        shm_remove() { shared_memory_object::remove(SHM_NAME); }
        ~shm_remove(){ shared_memory_object::remove(SHM_NAME); }
    } m_remover;

  public:
    typedef std::shared_ptr<counters> pointer;
    typedef allocator<char, managed_shared_memory::segment_manager>
        char_allocator;
    typedef basic_string<char, std::char_traits<char>, char_allocator>
        shm_string;
    typedef shm_string key_type;
    typedef size_t mapped_type;
    typedef std::pair<const key_type, mapped_type> value_type;
    typedef allocator<value_type, managed_shared_memory::segment_manager>
        shm_allocator;
    typedef map<key_type, mapped_type, std::less<key_type>, shm_allocator>
        shared_map;

    managed_shared_memory m_segment;
    shm_allocator m_allocator;
    shared_map *m_counter_map;

    std::mutex m_lock;

    counters() :
        m_segment(create_only, SHM_NAME, 65536),
        m_allocator(m_segment.get_segment_manager()),
        m_counter_map(m_segment.construct<shared_map>(SHM_MAP_NAME)
                (std::less<key_type>(), m_allocator))
    {}

    /**
     * increment() - increment a counter by one
     * @group: group that counter belongs to
     * @counter: the counter to increment
     *
     * Increments the specified counter and creates it if needed.
     */
    void increment(const std::string &key)
    {
        guard l(m_lock);
        (*m_counter_map)[shm_string(key.c_str(), m_allocator)]++;
    }

    /**
     * print() - print all created counters by group
     */
    void print()
    {
        for (auto i : *m_counter_map)
            std::cout << i.first << ": " << i.second << std::endl;
    }
};

/**
 * class counter_api - helper functions to use class counters.
 */
class counter_api
{
    counters::pointer m_counts;
    std::string m_group;

  protected:
    /**
     * set_group() - set current group
     * @group: name of the group
     *
     * Stores the group name to use for future increments.
     */
    void set_group(std::string group)
    {
        m_group = group;
    }

    /**
     * inc() - increment a counter
     * @str: counter to increment
     *
     * Increments the specified counter in the already specified group.
     */
    void inc(std::string str)
    {
        m_counts->increment(m_group + " " + str);
    }

  public:
    /**
     * set_counts() - add counter object to class
     * @counts: object to use when counting.
     *
     * Function to be used by factories when creating objects
     * which uses counters.
     */
    void set_counts(counters::pointer counts)
    {
        m_counts = counts;
    }

    counters::pointer counts()
    {
        return m_counts;
    }
};

#endif
