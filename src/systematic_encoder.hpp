/* Copyright 2013 Martin Hundebøll <martin@hundeboll.net> */

#ifndef FOX_SYSTEMATIC_ENCODER_HPP_
#define FOX_SYSTEMATIC_ENCODER_HPP_

namespace kodo
{
    template<class SuperCoder>
    class systematic_encoder_mask : public SuperCoder
    {
      public:
        uint32_t encode(uint8_t *symbol_data, uint8_t *symbol_id)
        {
            assert(symbol_data != 0);
            assert(symbol_id != 0);

            if (SuperCoder::is_systematic_on() &&
                SuperCoder::systematic_count() < SuperCoder::symbols_added())
                return SuperCoder::encode_systematic(symbol_data, symbol_id);
            else
                return SuperCoder::encode_non_systematic(symbol_data,
                                                         symbol_id);
        }
    };
};  // namespace kodo

#endif
