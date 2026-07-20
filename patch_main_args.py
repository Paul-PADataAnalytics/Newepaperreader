with open('src/main.cpp', 'r') as f:
    content = f.read()

main_orig = """#ifdef NATIVE_TESTING
int main(int argc, char** argv) {
    setup();
    while (true) {
        loop();
    }
    return 0;
}
#endif"""

main_new = """#ifdef NATIVE_TESTING
int main(int argc, char** argv) {
    setup();
    if (argc > 1 && strcmp(argv[1], "--headless") == 0) {
        printf("Running headless test for markdown...\\n");
        // Let's find test.md
        int indexToOpen = 0;
        for (int i=0; i<libraryFiles.size(); i++) {
            if (libraryFiles[i].name == "test.md") {
                indexToOpen = i;
                break;
            }
        }
        openBook(indexToOpen);
        DisplayHAL::dumpFramebuffer("screenshot_markdown.pgm", framebuffer);
        return 0;
    }
    while (true) {
        loop();
    }
    return 0;
}
#endif"""

content = content.replace(main_orig, main_new)
with open('src/main.cpp', 'w') as f:
    f.write(content)
