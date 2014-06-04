#pragma once

#include <stdio.h>

#define ERR(x, ...) do { fprintf(stderr, "Error: " x "\n", ##__VA_ARGS__); } while (0)
