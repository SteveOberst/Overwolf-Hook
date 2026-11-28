#include <overwolf_hook/overwolf_hook.hpp>
#include <iostream>

int main() {
    try {
        const auto info = overwolf_hook::OverwolfHook::build_info();
        std::cout << "Overwolf Hook " << info.sdk_version
                  << " loaded (build " << info.build_number << ")\n";
        return 0;
    } catch (const overwolf_hook::NativeError &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
