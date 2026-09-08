.PHONY: dev sweep

dev:
	cmake -S . -B build && cmake --build build && ./build/raylib_cpp

# Usage: make sweep ARGS="--populations=20,40 --generations=4000 --repeats=4 --seed=42"
sweep:
	cmake -S . -B build && cmake --build build && ./build/raylib_cpp --sweep $(ARGS)
