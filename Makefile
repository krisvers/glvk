mac:
	clang++ $(shell find ./glvk -type f -name "*.cpp") main.c glvk_gh/glvk_gh_cocoa.mm -o glvk -std=c++20 -Ilib/include -framework IOKit -framework Cocoa -Llib/mac -lMoltenVK
