// Exercise the actual production classifier without creating an engine/game.
#define main fastrun_engine_main
#include "../fastrun.cpp"
#undef main

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        std::ifstream input(argv[i], std::ios::binary);
        if (!input) return 2;
        std::string log((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::printf("%d\n", count_error_lines(log));
    }
    return 0;
}
