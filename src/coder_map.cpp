/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#include "coder_map.hpp"
#include "key.hpp"
#include "encoder.hpp"
#include "decoder.hpp"
#include "recoder.hpp"
#include "helper.hpp"

template<typename Key, typename Coder>
typename coder_map<Key, Coder>::coder_pointer
coder_map<Key, Coder>::create_coder(Key key)
{
    coder_pointer c;

    /* Create and return new coder */
    c = m_factory.build();

    c->set_key(key);
    c->set_io(m_io);
    c->set_counts(counts());
    if (has_semaphore())
        c->set_semaphore(get_semaphore());
    c->init();

    m_coders[key] = c;

    VLOG(3) << "Coder map: Created coder";
    return c;
}

template<typename Key, typename Coder>
typename coder_map<Key, Coder>::coder_pointer
coder_map<Key, Coder>::search_coder(Key key)
{
    map_it it;

    /* Return coder if it exists in map and is valid */
    if ((it = m_coders.find(key)) != m_coders.end() &&
        it->second->get_key() == key)
        return it->second;

    return coder_pointer();
}

template<typename Key, typename Coder>
size_t coder_map<Key, Coder>::get_block(Key key)
{
    typename block_map::iterator it;

    if ((it = m_blocks.find(key)) != m_blocks.end())
        return it->second;

    m_blocks[key] = 0;

    return 0;
}

template<typename Key, typename Coder>
Key coder_map<Key, Coder>::set_block(Key key, size_t block)
{
    key.block = 0;
    m_blocks[key] = block;
    key.block = block;

    return key;
}

template<typename Key, typename Coder>
typename coder_map<Key, Coder>::coder_pointer
coder_map<Key, Coder>::get_coder(Key key)
{
    coder_pointer c;

    guard g(m_lock);

    /* Return empty pointer if coder is already freed */
    if (m_invalid.find(key) != m_invalid.end())
        return coder_pointer();

    /* Find or create coder */
    c = search_coder(key);
    if (c)
        return c;

    return create_coder(key);
}

template<typename Key, typename Coder>
typename coder_map<Key, Coder>::coder_pointer
coder_map<Key, Coder>::find_coder(Key key)
{
    guard g(m_lock);

    return search_coder(key);
}

template<typename Key, typename Coder>
typename coder_map<Key, Coder>::coder_pointer
coder_map<Key, Coder>::get_latest_coder(Key key)
{
    coder_pointer c;

    guard g(m_lock);

    /* Get latest block for this src-dst pair. */
    key.block = get_block(key);

    /* Find or create coder */
    c = search_coder(key);
    if (!c || !c->is_valid()) {
        key = set_block(key, ++key.block);
        c = create_coder(key);
    }

    return c;
}

template<typename Key, typename Coder>
void coder_map<Key, Coder>::process_coders()
{
    map_it it;

    guard g(m_lock);
    it = m_coders.begin();

    while (it != m_coders.end()) {
        if (it->second->process()) {
            VLOG(LOG_OBJ) << "Coder map: Erasing coder " << it->second->num();
            m_invalid.insert(it->first);
            m_coders.erase(it++);
        } else {
            ++it;
        }
    }
}

template class coder_map<key, encoder>;
template class coder_map<key, decoder>;
template class coder_map<key, recoder>;
template class coder_map<key, helper>;
