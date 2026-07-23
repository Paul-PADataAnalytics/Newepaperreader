import sys

def extract_body(file_path, func_name):
    with open(file_path, 'r') as f:
        lines = f.readlines()
    
    in_func = False
    body = []
    braces = 0
    for line in lines:
        if func_name in line and '{' in line and not in_func:
            in_func = True
            braces = 1
            body.append(line)
            continue
        if in_func:
            body.append(line)
            braces += line.count('{')
            braces -= line.count('}')
            if braces == 0:
                break
    return body

engine = "src/TypographyEngine.cpp"
render_body = extract_body(engine, "void TypographyEngine::renderTextPaged")
find_body = extract_body(engine, "size_t TypographyEngine::findNextPageStart(const char* text")

with open("render_body.txt", "w") as f:
    f.writelines(render_body)
with open("find_body.txt", "w") as f:
    f.writelines(find_body)
