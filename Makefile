linux:
	clang++ $(shell find ./glvk -type f -name "*.cpp") main.c glvk_gh/glvk_gh_x11.c -o ./glvk_test -std=c++20 -Ilib/include -lvulkan -lglfw

mac:
	clang++ $(shell find ./glvk -type f -name "*.cpp") main.c glvk_gh/glvk_gh_cocoa.mm -o ./glvk_test -std=c++20 -Ilib/include -framework IOKit -framework Cocoa -rpath lib/mac -Llib/mac -lMoltenVK -lglfw3
