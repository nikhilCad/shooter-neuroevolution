.PHONY: dev

dev:
	cmake -S . -B build && cmake --build build && ./build/raylib_cpp
