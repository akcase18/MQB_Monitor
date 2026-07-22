Import("env")
import sys
import os
import glob

if sys.platform.startswith("darwin"):
    # Apple Silicon Homebrew (/opt/homebrew) and Intel Homebrew (/usr/local)
    # keep the Cellar in different places, so check both. Newer Homebrew
    # also renamed the "sdl2" formula to "sdl2-compat" (an SDL3-backed
    # compatibility shim exposing the same SDL2 API/ABI), so a fresh
    # install may only have that name — check both.
    cellar_roots = ["/opt/homebrew/Cellar", "/usr/local/Cellar"]
    formula_names = ["sdl2", "sdl2-compat"]

    sdl_include = None
    for root in cellar_roots:
        for name in formula_names:
            matches = glob.glob(f"{root}/{name}/*/include")
            if matches:
                sdl_include = matches[0]
                break
        if sdl_include:
            break
    if sdl_include:
        print(f"Found Homebrew SDL include path: {sdl_include}")
        env.Append(
            CPPPATH=sdl_include
        )

    # sdl2-compat doesn't ship a static libSDL2.a, only the dylib, so fall
    # back to that for -lSDL2 to link against dynamically.
    sdl_lib = None
    for root in cellar_roots:
        for name in formula_names:
            matches = glob.glob(f"{root}/{name}/**/libSDL2.a", recursive=True) \
                or glob.glob(f"{root}/{name}/**/libSDL2.dylib", recursive=True)
            if matches:
                sdl_lib = matches[0]
                break
        if sdl_lib:
            break
    if sdl_lib:
        print(f"Found Homebrew SDL lib path: {sdl_lib}")
        env.Append(
            LIBPATH=os.path.dirname(sdl_lib)
        )

elif sys.platform.startswith("win"):
    msys_root = "C:\\msys64\\mingw64"
    sdl_include = os.path.join(msys_root, "include")
    sdl_lib = os.path.join(msys_root, "lib")
    if os.path.exists(sdl_include):
        print(f"Found MSYS2 SDL include path: {sdl_include}")
        env.Append(CPPPATH=[sdl_include])
    libSDL = os.path.join(sdl_lib, "libSDL2.a")
    if os.path.exists(libSDL):
        print(f"Found MSYS2 SDL lib path: {libSDL}")
        env.Append(LIBPATH=[sdl_lib], LIBS=["SDL2"])    
    
#breakpoint()
    
#print('NewENV=====================================')
#print(env.Dump())
#print('DefaultENV=====================================')
#print(DefaultEnvironment().Dump())
