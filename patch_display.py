with open('src/DisplayHAL.cpp', 'r') as f:
    lines = f.readlines()

new_lines = []
for line in lines:
    if 'extern TouchClass touch;' in line or 'static TouchClass touch;' in line or 'touch.h' in line:
        continue
    new_lines.append(line)

new_lines.insert(0, '#ifndef NATIVE_TESTING\n#include "touch.h"\nstatic TouchClass touch;\n#endif\n')

with open('src/DisplayHAL.cpp', 'w') as f:
    f.writelines(new_lines)
