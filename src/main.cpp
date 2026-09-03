#include <graphic-lab.hpp>

#include <cstdlib>
#include <iostream>
#include <print>
#include <stdexcept>

int main()
{
    try
    {
        auto& graphic_lab = gplab::GraphicLab::getInstance();
        graphic_lab.run();
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
