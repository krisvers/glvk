linux:
	clang++ $(shell find ./glvk -type f -name "*.cpp") main.c -o glvk -lglfw -lvulkan-1 -std=c++20