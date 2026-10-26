#include <Meow2/Core/Version.h>

#include <cstdio>

int main()
{
    std::printf("Meow2Player (Meow2 Engine %s)\n", Meow2::getVersionString());
    return 0;
}
