#ifdef main
#undef main
#endif

#include <emscripten.h>

void setup();
void loop();

int main() {
    setup();
    emscripten_set_main_loop(loop, 0, 1);
    return 0;
}
