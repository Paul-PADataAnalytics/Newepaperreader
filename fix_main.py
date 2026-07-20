import re
with open('src/main.cpp', 'r') as f:
    content = f.read()

# Replace the entire int main function
new_main = """int main(int argc, char** argv) {
    setup();
    if (argc > 1 && strcmp(argv[1], "--headless") == 0) {
        printf("Running headless test for markdown...\\n");
        int indexToOpen = 0;
        printf("Library files:\\n");
        for (int i=0; i<libraryFiles.size(); i++) {
            printf(" - %s\\n", libraryFiles[i].name.c_str());
            if (libraryFiles[i].name == "test.md") {
                indexToOpen = i;
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
}"""

content = re.sub(r'int main\(int argc, char\*\* argv\).*', new_main + '\\n#endif', content, flags=re.DOTALL)

with open('src/main.cpp', 'w') as f:
    f.write(content)
