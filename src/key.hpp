/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_KEY_HPP_
#define FOX_KEY_HPP_

#include <string.h>

#include <iostream>
#include <iomanip>

/**
 * struct key - Key to be used in maps and sets in coder_map.
 */
struct key {
    uint8_t raw[ETH_ALEN + ETH_ALEN] = {0};
    uint8_t *src;
    uint8_t *dst;
    size_t block = 0;

    /**
     * key() - Construct empty key.
     */
    key() : src(raw), dst(raw + ETH_ALEN)
    {}

    /**
     * key() - Contruct key with initialized fields.
     * @param s Source address for key.
     * @param d Destination address for key.
     * @param b Block id for key.
     */
    key(const uint8_t *s, const uint8_t *d, const size_t b)
        : src(raw), dst(raw + ETH_ALEN)
    {
        set(s, d, b);
    }

    /**
     * key() - copy constructor for constant keys
     * oth: key to copy from
     */
    key(const key &oth) : src(raw), dst(raw + ETH_ALEN)
    {
        memcpy(dst, oth.dst, ETH_ALEN);
        memcpy(src, oth.src, ETH_ALEN);
        block = oth.block;
    }

    /**
     * set() - Set fields in key.
     * @param s Source address for key.
     * @param d Destination address for key.
     * @param b Block id for key.
     */
    void set(const uint8_t *s, const uint8_t *d, const size_t b)
    {
        if (s)
            memcpy(src, s, ETH_ALEN);
        else
            memset(src, 0, ETH_ALEN);

        if (d)
            memcpy(dst, d, ETH_ALEN);
        else
            memset(dst, 0, ETH_ALEN);

        block = b;
    }

    /**
     * operator=() - Assign key from another key.
     * @param oth Other key to copy data from.
     */
    void operator=(const key &oth)
    {
        memcpy(dst, oth.dst, ETH_ALEN);
        memcpy(src, oth.src, ETH_ALEN);
        block = oth.block;
    }

    /** operator==() - Compare key with another key.
     * @param oth Key to compare with.
     */
    bool operator==(const key &oth) const
    {
        return memcmp(src, oth.src, ETH_ALEN) == 0 &&
            memcmp(dst, oth.dst, ETH_ALEN) == 0 &&
            block == oth.block;
    }

    /** operator!=() - Compare key with another key.
     * @param oth Key to compare with.
     */
    bool operator!=(const key &oth) const
    {
        return memcmp(src, oth.src, ETH_ALEN) != 0 ||
            memcmp(dst, oth.dst, ETH_ALEN) != 0 ||
            block != oth.block;
    }

    /**
     * operator<() - Compare "size" of key with another.
     * @param r Other key to compare with.
     *
     * Returns true if this key is "smaller" than the passed key and
     * false if this key is equal to or greater than the passed key.
     *
     * Uses lexicographical sort by only considering destination if sources
     * were equal and only considering block number if both sources and
     * destination were equal.
     */
    bool operator<(const key &r) const
    {
        /* compare source */
        ssize_t s = memcmp(src, r.src, ETH_ALEN);
        if (s < 0)
            return true;

        /* okay, if sources are equal and destination is smaller, then this
         * key is smaller than other key
         */
        ssize_t d = memcmp(dst, r.dst, ETH_ALEN);
        if (s == 0 && d < 0)
            return true;

        /* if both sources and destinations are equal, then this key is
         * smaller than the other key only if block id is smaller
         */
        if (s == 0 && d == 0 && block < r.block)
            return true;

        /* so this key is _not_ smaller than the other key */
        return false;
    }


    /**
     * operator<<() - nice printing of keys
     */
    friend std::ostream &operator<<(std::ostream &out, const key &x)
    {
        print_eth(out, x.src);
        out << " -> ";
        print_eth(out, x.dst);
        out << " (" << x.block << ")";

        return out;
    }

    /**
     * print_eth() - helper function to << operator
     */
    static void print_eth(std::ostream &out, const uint8_t *val)
    {
        uint8_t i, fill = out.fill();
        std::streamsize width = out.width();

        /* print first byte of an ethernet address as hex with leading zero */
        out << std::setw(2) << std::setfill('0') << std::hex
            << static_cast<int>(val[0] & 0xFF);

        /* print following five bytes as hex with leading zero and colon
         * separator
         */
        for (i = 1; i < ETH_ALEN; i++)
            out << ':' << std::setw(2) << std::setfill('0')
                << std::hex << static_cast<int>(val[i] & 0xFF);

        /* restore state of output buffer */
        out.width(width);
        out.fill(fill);
        out.setf(std::ios::dec, std::ios::basefield);
    }
};

/**
 * class key_api - API used by coder_map to set and get key from coder.
 */
class key_api
{
  protected:
    key _key;

  public:
    /**
     * set_key() - Set member key to passed key.
     * @param k Key to set member key to.
     */
    void set_key(key k)
    {
        _key = k;
    }

    /**
     * get_key() - Get member key.
     */
    key get_key()
    {
        return _key;
    }

    /**
     * get_block() - Get block member from member key.
     */
    size_t get_block()
    {
        return _key.block;
    }
};

#endif
