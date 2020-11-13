all: llvmtest

BITCODE_FILE = sqlite3

$(BITCODE_FILE).bc: $(BITCODE_FILE).c
	clang -emit-llvm -c -o $@ $<

llvmtest: llvmtest.cpp $(BITCODE_FILE).bc
	clang++ -g -DBITCODE_FILE=\"$(BITCODE_FILE).bc\" $< -o $@ `llvm-config --cxxflags --ldflags --libs` -lpthread

clean:
	rm -f llvmtest $(BITCODE_FILE).bc
