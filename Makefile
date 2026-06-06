CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall

mdcat: mdcat.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f mdcat

.PHONY: clean
