#pragma once
#include <cstdio>
#include <cstdlib>

// Unlike assert(), CHECK always evaluates its operand in Release builds.
#define CHECK(...) do { if (!(__VA_ARGS__)) { \
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
    std::abort(); \
} } while (false)
