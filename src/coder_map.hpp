/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_CODER_MAP_HPP_
#define FOX_CODER_MAP_HPP_

#include <mutex>
#include <set>
#include <map>

#include "fox.hpp"
#include "io.hpp"
#include "counters.hpp"
#include "semaphore.hpp"

/**
 * class coder_map - Create, track and free coders.
 * @param Key type to use as key for map and set.
 * @param Coder type to create, track and free.
 *
 * Coders are requested by user and created if not existing. When created, the
 * coder is added to a map indexed by type Key. The map is searched for the key
 * when coders are requested. When a coder is freed, its key is moved to a set
 * of freed coders. This set is checked before new coders are created.
 */
template<class Key, class Coder>
class coder_map
    : public io_api,
      public counter_api,
      public semaphore_api
{
    typedef typename Coder::pointer coder_pointer;
    typedef std::map<Key, coder_pointer> map;
    typedef std::map<Key, size_t> block_map;
    typedef typename map::iterator map_it;
    typedef std::set<Key> set;

    typename Coder::factory m_factory;
    size_t m_symbols, m_symbol_size;
    std::mutex m_lock;
    map m_coders;
    block_map m_blocks;
    set m_invalid;

    coder_pointer create_coder(Key key);

    /**
     * search_coder() - Search m_coders for coder
     * @param key Key of requested coder.
     *
     * Search m_coders for key and return coder if founder.
     *
     * Returns existing coder.
     */
    coder_pointer search_coder(Key key);

    /**
     * get_block() - Find or set latest block for key.
     * @param key Key to use.
     *
     * Returns latest block id for given key.
     */
    size_t get_block(Key key);

    /**
     * set_block() - Update latest block id for key.
     * @param key Key to update; must have block_id == 0.
     * @param block New block id to use.
     *
     * Updates the map of latest block id's and also the passed key.
     */
    Key set_block(Key key, size_t block);

  public:
    typedef std::shared_ptr<coder_map<Key, Coder>> pointer;

    /**
     * coder_map() - Construct new coder_map.
     * @param io IO object to pass to new coders.
     * @param symbols Number of symbols in new coders.
     * @param symbol_size Size of symbols in new coders.
     */
    coder_map(size_t symbols, size_t symbol_size) :
        m_factory(symbols, symbol_size),
        m_symbols(symbols),
        m_symbol_size(symbol_size)
    {}

    /**
     * get_valid_coder() - Find or create a valid coder.
     * @param key Key to use when searching (and creating) coder.
     *
     * Checks if the requested coder is already finished and returns an empty
     * pointer if so. Otherwise m_coders is searched and a matching coder is
     * returned if found. If not, a new coder is created.
     */
    coder_pointer get_coder(Key key);

    coder_pointer find_coder(Key key);

    /* get_latest_coder() - Find or create latest coder.
     * @param key Key to use when searching (and creating) coder.
     *
     * If no valid coder is found, a new is created with incrementet block
     * id.
     */
    coder_pointer get_latest_coder(Key key);

    /**
     * process_coders() - Process all coders and free if coder is done.
     *
     * Iterate through coders in m_coders and call process function for each coder.
     * If the process function returns true, the coder is assumed to be
     * be finished and is freed.
     */
    void process_coders();
};

#endif
