# Creates a new ESP-IDF component in the ../components directory

import os
import sys
import shutil

if __name__ == "__main__":
    if len(sys.argv) > 1:
        component_name = sys.argv[1]
    else:
        print("Usage: python create_component.py <component_name>")
        sys.exit(1)

    script_path = os.path.dirname(os.path.realpath(__file__))

    component_path = os.path.join(script_path, "..", "components", component_name)
    if os.path.exists(component_path):
        print(f"Component {component_name} already exists")
        sys.exit(1)

    os.mkdir(component_path)

    # Create component_name.c and component_name.h as well as CMakeLists.txt, idf_component.yaml and Kconfig.projbuild
    with open(os.path.join(component_path, f"{component_name}.c"), "w") as f:
        f.write(f"#include \"{component_name}.h\"\n")

    with open(os.path.join(component_path, f"{component_name}.h"), "w") as f:
        f.write(f"#ifndef {component_name.upper()}_H\n")
        f.write(f"#define {component_name.upper()}_H\n\n\n\n")
        f.write(f"#endif\n")

    with open(os.path.join(component_path, "CMakeLists.txt"), "w") as f:
        f.write(f"idf_component_register(SRCS \"{component_name}.c\"\n")
        f.write(f"                       INCLUDE_DIRS \".\")\n")

    with open(os.path.join(component_path, "idf_component.yaml"), "w") as f:
        f.write(f"name: \"{component_name}\"\n")

    with open(os.path.join(component_path, "Kconfig.projbuild"), "w") as f:
        f.write(f"menu \"{component_name} Configuration\"\n")
        f.write(f"endmenu\n")

    print(f"Created component {component_name}")
