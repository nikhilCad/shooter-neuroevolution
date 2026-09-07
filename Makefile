.PHONY: dev sweep

dev:
	cmake -S . -B build && cmake --build build && ./build/raylib_cpp

# Usage: make sweep ARGS="--populations=20,40 --hidden=12,24 --generations=4000 "
sweep:
	cmake -S . -B build && cmake --build build && ./build/raylib_cpp --sweep $(ARGS)
