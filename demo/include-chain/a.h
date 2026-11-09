#pragma once

#include "b.h"

#ifdef HEADERSCOPE_DEMO_EXTRA
#include "extra.h"
#endif

constexpr int valueFromA()
{
    return valueFromB();
}
