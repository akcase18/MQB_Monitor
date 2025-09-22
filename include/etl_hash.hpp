#include "etl/unordered_map.h"

namespace etl
{
    template<>
    struct hash<etl::pair<uint16_t, uint16_t>>
    {
        size_t operator()(const etl::pair<uint16_t, uint16_t>& p) const
        {
            return static_cast<size_t>((p.first << 16) ^ p.second);
        }
    };
}