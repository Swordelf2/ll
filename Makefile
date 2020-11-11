all: llvmtest

llvmtest: llvmtest.cpp
	clang++ llvmtest.cpp -o llvmtest `llvm-config --cxxflags --ldflags --libs`

clean:
	rm llvmtest
