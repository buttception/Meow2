#include <Meow2/Core/Platform.h>
#include <Meow2/Core/Version.h>

#include <cstdio>

int main()
{
    std::printf("Meow2Renderer (Meow2 Engine %s, %s/%s/%s)\n", Meow2::getVersionString(), Meow2::c_platformName,
                Meow2::c_compilerName, Meow2::c_configName);
    return 0;
}
