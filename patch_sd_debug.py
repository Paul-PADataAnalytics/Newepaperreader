with open('src/mocks/SD.h', 'r') as f:
    content = f.read()

content = content.replace('std::string actualPath = path;', 'std::string actualPath = path; printf("SD.open called with: %s\\n", path);')
content = content.replace('if (f) return File(f, actualPath);', 'if (f) { printf("Opened file: %s\\n", actualPath.c_str()); return File(f, actualPath); }')
content = content.replace('if (d) return File(d, actualPath);', 'if (d) { printf("Opened dir: %s\\n", actualPath.c_str()); return File(d, actualPath); }')

with open('src/mocks/SD.h', 'w') as f:
    f.write(content)
