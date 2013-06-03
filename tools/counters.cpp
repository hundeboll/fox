#include "counters.hpp"

int main()
{
    managed_shared_memory segment(open_only, SHM_NAME);
    counters::shared_map *mymap = segment.find<counters::shared_map>(SHM_MAP_NAME).first;

    for (auto i : *mymap)
        std::cout << i.first << ": " << i.second << std::endl;

    return EXIT_SUCCESS;
}
