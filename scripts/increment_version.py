import os

Import("env")

VERSION_FILE = "include/version.h"

def get_version_info():
    major = 0
    minor = 0
    snapshot = 0
    
    if os.path.exists(VERSION_FILE):
        with open(VERSION_FILE, 'r') as f:
            lines = f.readlines()
            for line in lines:
                if "VERSION_MAJOR" in line:
                    major = int(line.split()[-1])
                elif "VERSION_MINOR" in line:
                    minor = int(line.split()[-1])
                elif "VERSION_SNAPSHOT" in line:
                    snapshot = int(line.split()[-1])
    return major, minor, snapshot

def increment_version(source, target, env):
    major, minor, snapshot = get_version_info()
    
    # Increment snapshot
    snapshot += 1
    
    # Create new version string
    version_string = f"{major}.{minor}.{snapshot}"
    
    print(f"Auto-incrementing version to: {version_string}")
    
    with open(VERSION_FILE, 'w') as f:
        f.write("#pragma once\n\n")
        f.write(f"#define VERSION_MAJOR {major}\n")
        f.write(f"#define VERSION_MINOR {minor}\n")
        f.write(f"#define VERSION_SNAPSHOT {snapshot}\n\n")
        f.write(f"#define SOFTWARE_VERSION \"{version_string}\"\n")

# Hook the increment function to the pre-build action
# Actually, running it always on every build might be too much if it runs on valid dependency checks too.
# But for now, let's run it as a pre-script.
increment_version(None, None, None)
