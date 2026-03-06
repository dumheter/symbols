#include <dc/log.hpp>

int main()
{
    dc::log::init();

    LOG_INFO("Hello, world!");

    dc::log::deinit();
    return 0;
}
