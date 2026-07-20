with open('src/main.cpp', 'r') as f:
    content = f.read()

main_orig = """        int indexToOpen = 0;
        for (int i=0; i<libraryFiles.size(); i++) {
            if (libraryFiles[i].name == "test.md") {
                indexToOpen = i;
                break;
            }
        }"""

main_new = """        int indexToOpen = 0;
        printf("Library files:\\n");
        for (int i=0; i<libraryFiles.size(); i++) {
            printf(" - %s\\n", libraryFiles[i].name.c_str());
            if (libraryFiles[i].name == "test.md") {
                indexToOpen = i;
            }
        }"""

content = content.replace(main_orig, main_new)
with open('src/main.cpp', 'w') as f:
    f.write(content)
