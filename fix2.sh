head -n -45 src/main.cpp > src/main.cpp.new
cat << 'INNER_EOF' >> src/main.cpp.new
#else
    delay(100);
#endif
}

#ifdef NATIVE_TESTING
int main(int argc, char** argv) {
    setup();
    if (argc > 1 && strcmp(argv[1], "--headless") == 0) {
        printf("Running headless test for RTF and markdown...\n");
        int indexToOpen = 0;
        int rtfIndex = -1;
        printf("Library files:\n");
        for (int i=0; i<libraryFiles.size(); i++) {
            printf(" - %s\n", libraryFiles[i].name.c_str());
            if (libraryFiles[i].name == "test.md") indexToOpen = i;
            if (libraryFiles[i].name == "test_document.rtf") rtfIndex = i;
        }
        openBook(indexToOpen);
        DisplayHAL::dumpFramebuffer("screenshot_markdown.pgm", framebuffer);
        
        if (rtfIndex != -1) {
            openBook(rtfIndex);
            DisplayHAL::dumpFramebuffer("screenshot_rtf_test.pgm", framebuffer);
        }
        return 0;
    }
    while (true) {
        loop();
    }
    return 0;
}
#endif
INNER_EOF
mv src/main.cpp.new src/main.cpp
