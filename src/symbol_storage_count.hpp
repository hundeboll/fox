/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_SYMBOL_STORAGE_COUNT_HPP_
#define FOX_SYMBOL_STORAGE_COUNT_HPP_

namespace kodo
{
    template<class SuperCoder>
    class symbol_storage_count : public SuperCoder
    {
      public:
        typedef typename SuperCoder::factory factory;

        void set_symbol(uint32_t index, const sak::const_storage &symbol)
        {
            SuperCoder::set_symbol(index, symbol);
            m_symbols_added++;
        }

        void initialize(const factory &the_factory)
        {
            SuperCoder::initialize(the_factory);

            m_symbols_added = 0;
        }

        uint32_t symbols_added()
        {
            return m_symbols_added;
        }

      protected:
        uint32_t m_symbols_added;
    };
};  // namespace kodo

#endif
