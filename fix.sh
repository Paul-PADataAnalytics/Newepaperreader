#!/bin/bash
sed -i '1i #ifndef NATIVE_TESTING\n#include "touch.h"\nextern TouchClass touch;\n#endif' src/DisplayHAL.cpp
